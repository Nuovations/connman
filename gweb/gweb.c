/*
 *
 *  Web service library with GLib integration
 *
 *  Copyright (C) 2009-2013  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <ifaddrs.h>

#include "giognutls.h"
#include "gresolv.h"
#include "gweb.h"

#define DEFAULT_BUFFER_SIZE  2048

#define SESSION_FLAG_USE_TLS	(1 << 0)

enum chunk_state {
	CHUNK_SIZE,
	CHUNK_R_BODY,
	CHUNK_N_BODY,
	CHUNK_DATA,
};

struct _GWebResult {
	guint16 status;
	const guint8 *buffer;
	gsize length;
	bool use_chunk;
	gchar *last_key;
	GHashTable *headers;
};

struct web_session {
	GWeb *web;

	char *address;
	char *host;
	uint16_t port;
	unsigned long flags;
	struct addrinfo *addr;

	char *content_type;

	GIOChannel *transport_channel;
	guint transport_watch;
	guint send_watch;

	guint resolv_action;
	guint address_action;
	char *request;

	guint8 *receive_buffer;
	gsize receive_space;
	GString *send_buffer;
	GString *current_header;
	bool header_done;
	bool body_done;
	bool more_data;
	bool request_started;

	enum chunk_state chunck_state;
	gsize chunk_size;
	gsize chunk_left;
	gsize total_len;

	GWebResult result;

	GWebResultFunc result_func;
	GWebRouteFunc route_func;
	GWebInputFunc input_func;
	int fd;
	gsize length;
	gsize offset;
	gpointer user_data;
};

struct _GWeb {
	int ref_count;

	guint next_query_id;

	int family;

	int index;
	GList *session_list;

	GResolv *resolv;
	char *proxy;
	char *accept_option;
	char *user_agent;
	char *user_agent_profile;
	char *http_version;
	bool close_connection;
	guint connect_timeout_ms;

	GWebDebugFunc debug_func;
	gpointer debug_data;
};

#define debug(web, format, arg...)				\
	_debug(web, __FILE__, __func__, format, ## arg)

static void _debug(GWeb *web, const char *file, const char *caller,
						const char *format, ...)
{
	char str[256];
	va_list ap;
	int len;

	if (!web->debug_func)
		return;

	va_start(ap, format);

	if ((len = snprintf(str, sizeof(str), "%s:%s() web %p ",
						file, caller, web)) > 0) {
		if (vsnprintf(str + len, sizeof(str) - len, format, ap) > 0)
			web->debug_func(str, web->debug_data);
	}

	va_end(ap);
}

static void free_session(struct web_session *session)
{
	GWeb *web;

	if (!session)
		return;

	g_free(session->request);

	web = session->web;

	if (session->address_action > 0)
		g_source_remove(session->address_action);

	if (session->resolv_action > 0)
		g_resolv_cancel_lookup(web->resolv, session->resolv_action);

	if (session->transport_watch > 0)
		g_source_remove(session->transport_watch);

	if (session->send_watch > 0)
		g_source_remove(session->send_watch);

	if (session->transport_channel)
		g_io_channel_unref(session->transport_channel);

	g_free(session->result.last_key);

	if (session->result.headers)
		g_hash_table_destroy(session->result.headers);

	if (session->send_buffer)
		g_string_free(session->send_buffer, TRUE);

	if (session->current_header)
		g_string_free(session->current_header, TRUE);

	g_free(session->receive_buffer);

	g_free(session->content_type);

	g_free(session->host);
	g_free(session->address);
	if (session->addr)
		freeaddrinfo(session->addr);

	g_free(session);
}

static void flush_sessions(GWeb *web)
{
	GList *list;

	for (list = g_list_first(web->session_list);
					list; list = g_list_next(list))
		free_session(list->data);

	g_list_free(web->session_list);
	web->session_list = NULL;
}

GWeb *g_web_new(int index)
{
	GWeb *web;

	if (index < 0)
		return NULL;

	web = g_try_new0(GWeb, 1);
	if (!web)
		return NULL;

	web->ref_count = 1;

	web->next_query_id = 1;

	web->family = AF_UNSPEC;

	web->index = index;
	web->session_list = NULL;

	web->resolv = g_resolv_new(index);
	if (!web->resolv) {
		g_free(web);
		return NULL;
	}

	web->accept_option = g_strdup("*/*");
	web->user_agent = g_strdup_printf("GWeb/%s", VERSION);
	web->close_connection = false;

	return web;
}

GWeb *g_web_ref(GWeb *web)
{
	if (!web)
		return NULL;

	__sync_fetch_and_add(&web->ref_count, 1);

	return web;
}

void g_web_unref(GWeb *web)
{
	if (!web)
		return;

	if (__sync_fetch_and_sub(&web->ref_count, 1) != 1)
		return;

	flush_sessions(web);

	g_resolv_unref(web->resolv);

	g_free(web->proxy);

	g_free(web->accept_option);
	g_free(web->user_agent);
	g_free(web->user_agent_profile);
	g_free(web->http_version);

	g_free(web);
}

bool g_web_supports_tls(void)
{
	return g_io_channel_supports_tls();
}

void g_web_set_debug(GWeb *web, GWebDebugFunc func, gpointer user_data)
{
	if (!web)
		return;

	web->debug_func = func;
	web->debug_data = user_data;

	g_resolv_set_debug(web->resolv, func, user_data);
}

bool g_web_set_proxy(GWeb *web, const char *proxy)
{
	if (!web)
		return false;

	g_free(web->proxy);

	if (!proxy) {
		web->proxy = NULL;
		debug(web, "clearing proxy");
	} else {
		web->proxy = g_strdup(proxy);
		debug(web, "setting proxy %s", web->proxy);
	}

	return true;
}

bool g_web_set_address_family(GWeb *web, int family)
{
	if (!web)
		return false;

	if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
		return false;

	web->family = family;

	g_resolv_set_address_family(web->resolv, family);

	return true;
}

bool g_web_add_nameserver(GWeb *web, const char *address)
{
	if (!web)
		return false;

	g_resolv_add_nameserver(web->resolv, address, 53, 0);

	return true;
}

static bool set_accept_option(GWeb *web, const char *format, va_list args)
{
	g_free(web->accept_option);

	if (!format) {
		web->accept_option = NULL;
		debug(web, "clearing accept option");
	} else {
		web->accept_option = g_strdup_vprintf(format, args);
		debug(web, "setting accept %s", web->accept_option);
	}

	return true;
}

bool g_web_set_accept(GWeb *web, const char *format, ...)
{
	va_list args;
	bool result;

	if (!web)
		return false;

	va_start(args, format);
	result = set_accept_option(web, format, args);
	va_end(args);

	return result;
}

static bool set_user_agent(GWeb *web, const char *format, va_list args)
{
	g_free(web->user_agent);

	if (!format) {
		web->user_agent = NULL;
		debug(web, "clearing user agent");
	} else {
		web->user_agent = g_strdup_vprintf(format, args);
		debug(web, "setting user agent %s", web->user_agent);
	}

	return true;
}

bool g_web_set_user_agent(GWeb *web, const char *format, ...)
{
	va_list args;
	bool result;

	if (!web)
		return false;

	va_start(args, format);
	result = set_user_agent(web, format, args);
	va_end(args);

	return result;
}

bool g_web_set_ua_profile(GWeb *web, const char *profile)
{
	if (!web)
		return false;

	g_free(web->user_agent_profile);

	web->user_agent_profile = g_strdup(profile);
	debug(web, "setting user agent profile %s", web->user_agent);

	return true;
}

bool g_web_set_http_version(GWeb *web, const char *version)
{
	if (!web)
		return false;

	g_free(web->http_version);

	if (!version) {
		web->http_version = NULL;
		debug(web, "clearing HTTP version");
	} else {
		web->http_version = g_strdup(version);
		debug(web, "setting HTTP version %s", web->http_version);
	}

	return true;
}

void g_web_set_close_connection(GWeb *web, bool enabled)
{
	if (!web)
		return;

	web->close_connection = enabled;
}

bool g_web_get_close_connection(GWeb *web)
{
	if (!web)
		return false;

	return web->close_connection;
}

void g_web_set_connect_timeout(GWeb *web, guint timeout_ms)
{
	if (!web)
		return;

	debug(web, "timeout %ums", timeout_ms);

	web->connect_timeout_ms = timeout_ms;
}

guint g_web_get_connect_timeout(const GWeb *web)
{
	guint timeout_ms = 0;

	if (!web)
		goto done;

	timeout_ms = web->connect_timeout_ms;

done:
	return timeout_ms;
}

static inline void call_result_func(struct web_session *session, guint16 status)
{

	if (!session->result_func)
		return;

	if (status != GWEB_HTTP_STATUS_CODE_UNKNOWN)
		session->result.status = status;

	session->result_func(&session->result, session->user_data);

}

static inline void call_route_func(struct web_session *session)
{
	if (session->route_func)
		session->route_func(session->address, session->addr->ai_family,
				session->web->index, session->user_data);
}

static bool process_send_buffer(struct web_session *session)
{
	GString *buf;
	gsize count, bytes_written;
	GIOStatus status;

	if (!session)
		return false;

	buf = session->send_buffer;
	count = buf->len;

	if (count == 0) {
		if (session->request_started &&
					!session->more_data &&
					session->fd == -1)
			session->body_done = true;

		return false;
	}

	status = g_io_channel_write_chars(session->transport_channel,
					buf->str, count, &bytes_written, NULL);

	debug(session->web, "status %u bytes to write %zu bytes written %zu",
					status, count, bytes_written);

	if (status != G_IO_STATUS_NORMAL && status != G_IO_STATUS_AGAIN)
		return false;

	g_string_erase(buf, 0, bytes_written);

	return true;
}

static bool process_send_file(struct web_session *session)
{
	int sk;
	off_t offset;
	ssize_t bytes_sent;

	if (session->fd == -1)
		return false;

	if (!session->request_started || session->more_data)
		return false;

	sk = g_io_channel_unix_get_fd(session->transport_channel);
	if (sk < 0)
		return false;

	offset = session->offset;

	bytes_sent = sendfile(sk, session->fd, &offset, session->length);

	debug(session->web, "errno: %d, bytes to send %zu / bytes sent %zu",
			errno, session->length, bytes_sent);

	if (bytes_sent < 0 && errno != EAGAIN)
		return false;

	session->offset = offset;
	session->length -= bytes_sent;

	if (session->length == 0) {
		session->body_done = true;
		return false;
	}

	return true;
}

static void process_next_chunk(struct web_session *session)
{
	GString *buf = session->send_buffer;
	const guint8 *body;
	gsize length;

	if (!session->input_func) {
		session->more_data = false;
		return;
	}

	session->more_data = session->input_func(&body, &length,
						session->user_data);

	if (length > 0) {
		g_string_append_printf(buf, "%zx\r\n", length);
		g_string_append_len(buf, (char *) body, length);
		g_string_append(buf, "\r\n");
	}

	if (!session->more_data)
		g_string_append(buf, "0\r\n\r\n");
}

static void start_request(struct web_session *session)
{
	GString *buf = session->send_buffer;
	const char *version;
	const guint8 *body;
	gsize length;

	debug(session->web, "request %s from %s",
					session->request, session->host);

	g_string_truncate(buf, 0);

	if (!session->web->http_version)
		version = "1.1";
	else
		version = session->web->http_version;

	if (!session->content_type)
		g_string_append_printf(buf, "GET %s HTTP/%s\r\n",
						session->request, version);
	else
		g_string_append_printf(buf, "POST %s HTTP/%s\r\n",
						session->request, version);

	g_string_append_printf(buf, "Host: %s\r\n", session->host);

	if (session->web->user_agent)
		g_string_append_printf(buf, "User-Agent: %s\r\n",
						session->web->user_agent);

	if (session->web->user_agent_profile) {
		g_string_append_printf(buf, "x-wap-profile: %s\r\n",
				       session->web->user_agent_profile);
	}

	if (session->web->accept_option)
		g_string_append_printf(buf, "Accept: %s\r\n",
						session->web->accept_option);

	if (session->content_type) {
		g_string_append_printf(buf, "Content-Type: %s\r\n",
							session->content_type);
		if (!session->input_func) {
			session->more_data = false;
			length = session->length;
		} else
			session->more_data = session->input_func(&body, &length,
							session->user_data);
		if (!session->more_data)
			g_string_append_printf(buf, "Content-Length: %zu\r\n",
									length);
		else
			g_string_append(buf, "Transfer-Encoding: chunked\r\n");
	}

	if (session->web->close_connection)
		g_string_append(buf, "Connection: close\r\n");

	g_string_append(buf, "\r\n");

	if (session->content_type && length > 0) {
		if (session->more_data) {
			g_string_append_printf(buf, "%zx\r\n", length);
			g_string_append_len(buf, (char *) body, length);
			g_string_append(buf, "\r\n");
		} else if (session->fd == -1)
			g_string_append_len(buf, (char *) body, length);
	}
}

static gboolean send_data(GIOChannel *channel, GIOCondition cond,
						gpointer user_data)
{
	struct web_session *session = user_data;

	if (cond & (G_IO_NVAL | G_IO_ERR | G_IO_HUP)) {
		session->send_watch = 0;
		return FALSE;
	}

	if (process_send_buffer(session))
		return TRUE;

	if (process_send_file(session))
		return TRUE;

	if (!session->request_started) {
		session->request_started = true;
		start_request(session);
	} else if (session->more_data)
		process_next_chunk(session);

	process_send_buffer(session);

	if (session->body_done) {
		session->send_watch = 0;
		return FALSE;
	}

	return TRUE;
}

static int decode_chunked(struct web_session *session,
					const guint8 *buf, gsize len)
{
	const guint8 *ptr = buf;
	gsize counter;

	while (len > 0) {
		guint8 *pos;
		gsize count;
		char *str;

		switch (session->chunck_state) {
		case CHUNK_SIZE:
			pos = memchr(ptr, '\n', len);
			if (!pos) {
				g_string_append_len(session->current_header,
						(gchar *) ptr, len);
				return 0;
			}

			count = pos - ptr;
			if (count < 1 || ptr[count - 1] != '\r')
				return -EILSEQ;

			g_string_append_len(session->current_header,
						(gchar *) ptr, count);

			len -= count + 1;
			ptr = pos + 1;

			str = session->current_header->str;

			counter = strtoul(str, NULL, 16);
			if ((counter == 0 && errno == EINVAL) ||
						counter == ULONG_MAX)
				return -EILSEQ;

			session->chunk_size = counter;
			session->chunk_left = counter;

			session->chunck_state = CHUNK_DATA;
			break;
		case CHUNK_R_BODY:
			if (*ptr != '\r')
				return -EILSEQ;
			ptr++;
			len--;
			session->chunck_state = CHUNK_N_BODY;
			break;
		case CHUNK_N_BODY:
			if (*ptr != '\n')
				return -EILSEQ;
			ptr++;
			len--;
			session->chunck_state = CHUNK_SIZE;
			break;
		case CHUNK_DATA:
			if (session->chunk_size == 0) {
				debug(session->web, "Download Done in chunk");
				g_string_truncate(session->current_header, 0);
				return 0;
			}

			if (session->chunk_left <= len) {
				session->result.buffer = ptr;
				session->result.length = session->chunk_left;
				call_result_func(session,
					GWEB_HTTP_STATUS_CODE_UNKNOWN);

				len -= session->chunk_left;
				ptr += session->chunk_left;

				session->total_len += session->chunk_left;
				session->chunk_left = 0;

				g_string_truncate(session->current_header, 0);
				session->chunck_state = CHUNK_R_BODY;
				break;
			}
			/* more data */
			session->result.buffer = ptr;
			session->result.length = len;
			call_result_func(session,
				GWEB_HTTP_STATUS_CODE_UNKNOWN);

			session->chunk_left -= len;
			session->total_len += len;

			len -= len;
			ptr += len;
			break;
		}
	}

	return 0;
}

static int handle_body(struct web_session *session,
				const guint8 *buf, gsize len)
{
	int err;

	debug(session->web, "[body] length %zu", len);

	if (!session->result.use_chunk) {
		if (len > 0) {
			session->result.buffer = buf;
			session->result.length = len;
			call_result_func(session,
				GWEB_HTTP_STATUS_CODE_UNKNOWN);
		}
		return 0;
	}

	err = decode_chunked(session, buf, len);
	if (err < 0) {
		debug(session->web, "Error in chunk decode %d", err);

		session->result.buffer = NULL;
		session->result.length = 0;
		call_result_func(session, GWEB_HTTP_STATUS_CODE_BAD_REQUEST);
	}

	return err;
}

static void handle_multi_line(struct web_session *session)
{
	gsize count;
	char *str;
	gchar *value;

	if (!session->result.last_key)
		return;

	str = session->current_header->str;

	if (str[0] != ' ' && str[0] != '\t')
		return;

	while (str[0] == ' ' || str[0] == '\t')
		str++;

	count = str - session->current_header->str;
	if (count > 0) {
		g_string_erase(session->current_header, 0, count);
		g_string_insert_c(session->current_header, 0, ' ');
	}

	value = g_hash_table_lookup(session->result.headers,
					session->result.last_key);
	if (value) {
		g_string_insert(session->current_header, 0, value);

		str = session->current_header->str;

		g_hash_table_replace(session->result.headers,
					g_strdup(session->result.last_key),
					g_strdup(str));
	}
}

static void add_header_field(struct web_session *session)
{
	gsize count;
	guint8 *pos;
	char *str;
	gchar *value;
	gchar *key;

	str = session->current_header->str;

	pos = memchr(str, ':', session->current_header->len);
	if (pos) {
		*pos = '\0';
		pos++;

		key = g_strdup(str);

		/* remove preceding white spaces */
		while (*pos == ' ')
			pos++;

		count = (char *) pos - str;

		g_string_erase(session->current_header, 0, count);

		value = g_hash_table_lookup(session->result.headers, key);
		if (value) {
			g_string_insert_c(session->current_header, 0, ' ');
			g_string_insert_c(session->current_header, 0, ';');

			g_string_insert(session->current_header, 0, value);
		}

		str = session->current_header->str;
		g_hash_table_replace(session->result.headers, key,
							g_strdup(str));

		g_free(session->result.last_key);
		session->result.last_key = g_strdup(key);
	}
}

static gboolean received_data(GIOChannel *channel, GIOCondition cond,
							gpointer user_data)
{
	struct web_session *session = user_data;
	guint8 *ptr = session->receive_buffer;
	gsize bytes_read;
	GIOStatus status;

	if (cond & (G_IO_NVAL | G_IO_ERR | G_IO_HUP)) {
		session->transport_watch = 0;
		session->result.buffer = NULL;
		session->result.length = 0;
		call_result_func(session, GWEB_HTTP_STATUS_CODE_BAD_REQUEST);
		return FALSE;
	}

	status = g_io_channel_read_chars(channel,
				(gchar *) session->receive_buffer,
				session->receive_space - 1, &bytes_read, NULL);

	debug(session->web, "bytes read %zu", bytes_read);

	if (status != G_IO_STATUS_NORMAL && status != G_IO_STATUS_AGAIN) {
		session->transport_watch = 0;
		session->result.buffer = NULL;
		session->result.length = 0;
		call_result_func(session, GWEB_HTTP_STATUS_CODE_UNKNOWN);
		return FALSE;
	}

	session->receive_buffer[bytes_read] = '\0';

	if (session->header_done) {
		if (handle_body(session, session->receive_buffer,
							bytes_read) < 0) {
			session->transport_watch = 0;
			return FALSE;
		}
		return TRUE;
	}

	while (bytes_read > 0) {
		guint8 *pos;
		gsize count;
		char *str;

		pos = memchr(ptr, '\n', bytes_read);
		if (!pos) {
			g_string_append_len(session->current_header,
						(gchar *) ptr, bytes_read);
			return TRUE;
		}

		*pos = '\0';
		count = pos - ptr;
		if (count > 0 && ptr[count - 1] == '\r') {
			ptr[--count] = '\0';
			bytes_read--;
		}

		g_string_append_len(session->current_header,
						(gchar *) ptr, count);

		bytes_read -= count + 1;
		if (bytes_read > 0)
			ptr = pos + 1;
		else
			ptr = NULL;

		if (session->current_header->len == 0) {
			char *val;

			session->header_done = true;

			val = g_hash_table_lookup(session->result.headers,
							"Transfer-Encoding");
			if (val) {
				val = g_strrstr(val, "chunked");
				if (val) {
					session->result.use_chunk = true;

					session->chunck_state = CHUNK_SIZE;
					session->chunk_left = 0;
					session->total_len = 0;
				}
			}

			if (handle_body(session, ptr, bytes_read) < 0) {
				session->transport_watch = 0;
				return FALSE;
			}
			break;
		}

		str = session->current_header->str;

		if (session->result.status == 0) {
			unsigned int code;

			if (sscanf(str, "HTTP/%*s %u %*s", &code) == 1)
				session->result.status = code;
		}

		debug(session->web, "[header] %s", str);

		/* handle multi-line header */
		if (str[0] == ' ' || str[0] == '\t')
			handle_multi_line(session);
		else
			add_header_field(session);

		g_string_truncate(session->current_header, 0);
	}

	return TRUE;
}

static int bind_to_address(int sk, const char *interface, int family)
{
	struct ifaddrs *ifaddr_list, *ifaddr;
	int size, err = -1;

	if (getifaddrs(&ifaddr_list) < 0)
		return err;

	for (ifaddr = ifaddr_list; ifaddr; ifaddr = ifaddr->ifa_next) {
		if (g_strcmp0(ifaddr->ifa_name, interface) != 0)
			continue;

		if (!ifaddr->ifa_addr ||
				ifaddr->ifa_addr->sa_family != family)
			continue;

		switch (family) {
		case AF_INET:
			size = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			size = sizeof(struct sockaddr_in6);
			break;
		default:
			continue;
		}

		err = bind(sk, (struct sockaddr *) ifaddr->ifa_addr, size);
		break;
	}

	freeifaddrs(ifaddr_list);
	return err;
}

static inline int bind_socket(int sk, int index, int family)
{
	char interface[IF_NAMESIZE];
	int err;

	if (!if_indextoname(index, interface))
		return -1;

	err = setsockopt(sk, SOL_SOCKET, SO_BINDTODEVICE,
					interface, IF_NAMESIZE);
	if (err < 0)
		err = bind_to_address(sk, interface, family);

	return err;
}

static int connect_session_transport(struct web_session *session)
{
	GIOFlags flags;
	int sk;

	sk = socket(session->addr->ai_family, SOCK_STREAM | SOCK_CLOEXEC,
			IPPROTO_TCP);
	if (sk < 0)
		return -EIO;

	if (session->web->index > 0) {
		if (bind_socket(sk, session->web->index,
					session->addr->ai_family) < 0) {
			debug(session->web, "bind() %s", strerror(errno));
			close(sk);
			return -EIO;
		}
	}

	if (session->flags & SESSION_FLAG_USE_TLS) {
		debug(session->web, "using TLS encryption");
		session->transport_channel = g_io_channel_gnutls_new(sk);
	} else {
		debug(session->web, "no encryption");
		session->transport_channel = g_io_channel_unix_new(sk);
	}

	if (!session->transport_channel) {
		debug(session->web, "channel missing");
		close(sk);
		return -ENOMEM;
	}

	flags = g_io_channel_get_flags(session->transport_channel);
	g_io_channel_set_flags(session->transport_channel,
					flags | G_IO_FLAG_NONBLOCK, NULL);

	g_io_channel_set_encoding(session->transport_channel, NULL, NULL);
	g_io_channel_set_buffered(session->transport_channel, FALSE);

	g_io_channel_set_close_on_unref(session->transport_channel, TRUE);

	if (connect(sk, session->addr->ai_addr,
			session->addr->ai_addrlen) < 0) {
		if (errno != EINPROGRESS) {
			debug(session->web, "connect() %s", strerror(errno));
			return -EIO;
		}
	}

	session->transport_watch = g_io_add_watch(session->transport_channel,
				G_IO_IN | G_IO_HUP | G_IO_NVAL | G_IO_ERR,
						received_data, session);

	session->send_watch = g_io_add_watch(session->transport_channel,
				G_IO_OUT | G_IO_HUP | G_IO_NVAL | G_IO_ERR,
						send_data, session);

	return 0;
}

static int create_transport(struct web_session *session)
{
	int err;

	err = connect_session_transport(session);
	if (err < 0)
		return err;

	debug(session->web, "creating session %s:%u",
					session->address, session->port);

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the scheme component from a URL with an
 *    optional default fallback.
 *
 *  This attempts to parse the scheme component from the specified URL
 *  of the provided length at the specified cursor point in the
 *  URL. If a scheme cannot be parsed, the provided optional default
 *  fallback is used. If provided, the parsed scheme is copied and
 *  assigned to @a scheme.
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to @a *scheme, if provided, on success.
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the scheme component.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the scheme. On
 *                                  success, this is updated to the
 *                                  first byte past the parsed scheme.
 *  @param[in,out]  scheme          An optional pointer to storage to
 *                                  assign a copy of the parsed scheme
 *                                  on success.
 *  @param[in]      default_scheme  An optional pointer to the immutable
 *                                  null-terminated C string to use of
 *                                  a scheme cannot be parsed from @a
 *                                  url at @a cursor.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, or @a
 *                    cursor was null.
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    scheme.
 *
 *  @sa parse_url_scheme
 *  @sa parse_url_components
 *
 */
static int parse_url_scheme_with_default(const char *url, size_t url_length,
						const char **cursor,
						char **scheme,
						const char *default_scheme)
{
	static const char * const scheme_delimiter = "://";
	static const size_t scheme_delimiter_length = 3;
	const char *result;
	size_t remaining_length;
	size_t scheme_length = 0;

	if (!url || !url_length || !cursor)
		return -EINVAL;

	remaining_length = (url_length - (size_t)(*cursor - url));
	if (remaining_length) {
		result = (const char *)memmem(*cursor,
							remaining_length,
							scheme_delimiter,
							scheme_delimiter_length);
		if (result) {
			scheme_length = (size_t)(result - *cursor);

			if (scheme) {
				*scheme = g_strndup(*cursor, scheme_length);
				if (!*scheme)
					return -ENOMEM;
			}

			*cursor += scheme_length + scheme_delimiter_length;
		} else if (scheme && default_scheme) {
			scheme_length = strlen(default_scheme);

			*cursor += scheme_length;

			*scheme = g_strndup(default_scheme, scheme_length);
			if (!*scheme)
				return -ENOMEM;
		} else if (scheme)
			*scheme = NULL;
	} else if (scheme)
		*scheme = NULL;

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the scheme component from a URL.
 *
 *  This attempts to parse the scheme component from the specified URL
 *  of the provided length at the specified cursor point in the
 *  URL. If provided, the parsed scheme is copied and
 *  assigned to @a scheme.
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to @a *scheme, if provided, on success.
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the scheme component.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the scheme. On
 *                                  success, this is updated to the
 *                                  first byte past the parsed scheme.
 *  @param[in,out]  scheme          An optional pointer to storage to
 *                                  assign a copy of the parsed scheme
 *                                  on success.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, or @a
 *                    cursor was null.
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    scheme.
 *
 *  @sa parse_url_scheme_with_default
 *  @sa parse_url_components
 *
 */
static int parse_url_scheme(const char *url, size_t url_length,
						const char **cursor,
						char **scheme)
{
	const char * const default_scheme = NULL;

	return parse_url_scheme_with_default(url,
		url_length,
		cursor,
		scheme,
		default_scheme);
}

/**
 *  @brief
 *    Attempt to parse the host component from a URL.
 *
 *  This attempts to parse the host component from the specified
 *  URL of the provided length at the specified cursor point in the
 *  URL. If provided, the parsed host is copied and assigned to @a
 *  host.
 *
 *  Compliant with RFC 2732, the format of the host component of the
 *  URL may be one of the following:
 *
 *    1. "[\<IPv6 Address>]"
 *    2. "[\<IPv6 Address>]:<Port>"
 *    4. "\<IPv4 Address>"
 *    5. "\<IPv4 Address>:<Port>"
 *    6. "\<Host Name>"
 *    7. "\<Host Name>:<Port>"
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to @a *host, if provided, on success.
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the host component.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the host. On
 *                                  success, this is updated to the
 *                                  first byte past the parsed host.
 *  @param[in,out]  host            An optional pointer to storage to
 *                                  assign a copy of the parsed host
 *                                  on success.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, @a
 *                    cursor was null, or if the host portion of @a
 *                    url is malformed.
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    host.
 *
 *  @sa parse_url_host_and_port
 *  @sa parse_url_components
 *
 */
static int parse_url_host(const char *url, size_t url_length,
						const char **cursor,
						char **host)
{
	size_t remaining_length;
	size_t host_length	= 0;
	const char *result;
	const char *opening_bracket;
	const char *closing_bracket;

	if (!url || !url_length || !cursor)
		return -EINVAL;

	// Since it's the easiest to detect, first rule out an IPv6
	// address. The only reliably way to do so is to search for the
	// delimiting '[' and ']'. Searching for ':' may yield one of the
	// other forms above (for example, (2), (5), or (7).

	remaining_length = (url_length - (size_t)(*cursor - url));

	opening_bracket = (const char *)memchr(*cursor, '[', remaining_length);
	if (opening_bracket) {
		// We found an opening bracket; this might be an IPv6
		// address. Search for its peer closing bracket.

		remaining_length = (url_length - (size_t)(opening_bracket - url));

		closing_bracket = (const char *)memchr(opening_bracket,
									']',
									remaining_length);
		if (!closing_bracket)
			return -EINVAL;

		// Assign the first character of the IPv6 address after the
		// opening bracket up to, but not including, the closing
		// bracket to the host name.

		host_length = (size_t)(closing_bracket - opening_bracket) - 1;

		if (host) {
			*host = g_strndup(opening_bracket + 1, host_length);
			if (!*host)
				return -ENOMEM;
		}

		// Move the parsing cursor past the closing bracket.

		*cursor = closing_bracket + 1;
	} else {
		// At this point, we either have an IPv4 address or a host
		// name, maybe with a port.
		//
		// Whether we have a port or not, we definitively know where
		// the IPv4 address or host name ends. If we have a port, it
		// ends at the port delimiter, ':'. If we don't have a port,
		// then it ends at the end of the string or at the path
		// delimiter, if any.

		result = (const char *)memchr(*cursor, ':', remaining_length);
		if (result) {
			// There is a port and an IPv4 address or a host name.
			// Assign the latter to the host and handle the port
			// later.

			host_length = (size_t)(result - *cursor);

			if (host) {
				*host = g_strndup(*cursor, host_length);
				if (!*host)
					return -ENOMEM;
			}

			*cursor += host_length;
		} else {
			// There is no port, just an IPv4 address or a host name,
			// potentially followed by a path.

			result = (const char *)memchr(*cursor, '/', remaining_length);
			if (result)
				host_length = (size_t)(result - *cursor);
			else
				host_length = remaining_length;

			if (host) {
				*host = g_strndup(*cursor, host_length);
				if (!*host)
					return -ENOMEM;
			}

			*cursor += host_length;
		}
	}

	if (!host_length)
		return -EINVAL;

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the port component from a URL.
 *
 *  This attempts to parse the port component from the specified URL
 *  of the provided length at the specified cursor point in the
 *  URL. If provided, the parsed port is assigned to @a port.
 *
 *  Compliant with RFC 2732, the format of the host component of the
 *  URL may be one of the following:
 *
 *    1. "[\<IPv6 Address>]"
 *    2. "[\<IPv6 Address>]:<Port>"
 *    4. "\<IPv4 Address>"
 *    5. "\<IPv4 Address>:<Port>"
 *    6. "\<Host Name>"
 *    7. "\<Host Name>:<Port>"
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the port component.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the port. On
 *                                  success, this is updated to the
 *                                  first byte past the parsed port.
 *  @param[in,out]  port            An optional pointer to storage to
 *                                  assign the parsed port on
 *                                  success. On failure or absence of
 *                                  a port to parsed, this is assigned
 *                                  -1.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, @a
 *                    cursor was null, or if there were no characters
 *                    to parse after the port delimiter (':').
 *  @retval  -ERANGE  If the parsed port was outside of the range [0,
 *                    65535], inclusive.
 *
 *  @sa parse_url_host_and_port
 *  @sa parse_url_components
 *
 */
static int parse_url_port(const char *url, size_t url_length,
						const char **cursor,
						int16_t *port)
{
	static char port_delimiter = ':';
	static const size_t port_delimiter_length = 1;
	const char *result;
	size_t remaining_length;
	size_t port_length = 0;
	char *end;
	unsigned long tmp_port;

	if (!url || !url_length || !cursor)
		return -EINVAL;

	remaining_length = (url_length - (size_t)(*cursor - url));

	result = (const char *)memchr(*cursor, port_delimiter, remaining_length);
	if (result) {
		tmp_port = strtoul(result + port_delimiter_length, &end, 10);
		if (tmp_port == ULONG_MAX)
			return -ERANGE;
		else if (tmp_port < 0 || tmp_port > UINT16_MAX)
			return -ERANGE;
		else if (result + 1 == end)
			return -EINVAL;

		if (*end != '\0')
			port_length = remaining_length;
		else
			port_length = end - (result + port_delimiter_length);

		*cursor += port_length;
	} else
		tmp_port = -1;

	if (port)
		*port = (int16_t)tmp_port;

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the host and port components from a URL.
 *
 *  This attempts to parse the host and port components from the
 *  specified URL of the provided length at the specified cursor point
 *  in the URL. If provided, the parsed host is copied and assigned to
 *  @a host and, if provided, the parsed port is assigned to @a port.
 *
 *  Compliant with RFC 2732, the format of the host component of the
 *  URL may be one of the following:
 *
 *    1. "[\<IPv6 Address>]"
 *    2. "[\<IPv6 Address>]:<Port>"
 *    4. "\<IPv4 Address>"
 *    5. "\<IPv4 Address>:<Port>"
 *    6. "\<Host Name>"
 *    7. "\<Host Name>:<Port>"
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to @a *host, if provided, on success.
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the host and port components.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the host and
 *                                  port. On success, this is updated
 *                                  to the first byte past the parsed
 *                                  host or port, if present.
 *  @param[in,out]  host            An optional pointer to storage to
 *                                  assign a copy of the parsed host
 *                                  on success.
 *  @param[in,out]  port            An optional pointer to storage to
 *                                  assign the parsed port on
 *                                  success. On failure or absence of
 *                                  a port to parsed, this is assigned
 *                                  -1.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, @a
 *                    cursor was null, if the host portion of @a url
 *                    is malformed, or if there were no characters to
 *                    parse after the port delimiter (':').
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    host.
 *  @retval  -ERANGE  If the parsed port was outside of the range [0,
 *                    65535], inclusive.
 *
 *  @sa parse_url_host
 *  @sa parse_url_port
 *  @sa parse_url_components
 *
 */
static int parse_url_host_and_port(const char *url, size_t url_length,
						const char **cursor,
						char **host,
						int16_t *port)
{
	int err;

	if (!url || !url_length || !cursor)
		return -EINVAL;

	// Attempt to handle the host component.

	err = parse_url_host(url, url_length, cursor, host);
	if (err != 0)
		return err;

	// Attempt to handle the port component.

	err = parse_url_port(url, url_length, cursor, port);
	if (err != 0) {
		if (host) {
			g_free(*host);
			*host = NULL;
		}

		return err;
	}

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the path component from a URL.
 *
 *  This attempts to parse the path component from the specified
 *  URL of the provided length at the specified cursor point in the
 *  URL. If provided, the parsed path is copied and assigned to @a
 *  path.
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to @a *path, if provided, on success.
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the path component.
 *  @param[in]      url_length      The length, in bytes, of @a url.
 *  @param[in,out]  cursor          A pointer to the current parsing
 *                                  position within @a url at which to
 *                                  start parsing the path. On
 *                                  success, this is updated to the
 *                                  first byte past the parsed path.
 *  @param[in,out]  path            An optional pointer to storage to
 *                                  assign a copy of the parsed path
 *                                  on success.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url_length was zero, or @a
 *                    cursor was null.
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    path.
 *
 *  @sa parse_url_components
 *
 */
static int parse_url_path(const char *url, size_t url_length,
						const char **cursor,
						char **path)
{
	static char path_delimiter = '/';
	static const size_t path_delimiter_length = 1;
	const char *result;
	size_t remaining_length;
	size_t path_length = 0;

	if (!url || !url_length || !cursor)
		return -EINVAL;

	remaining_length = (url_length - (size_t)(*cursor - url));

	result = (const char *)memchr(*cursor, path_delimiter, remaining_length);
	if (result) {
		path_length = (url_length - (size_t)(result + path_delimiter_length - url));

		if (path) {
			*path = g_strndup(result + path_delimiter_length, path_length);
			if (!*path)
				return -ENOMEM;
		}

		*cursor += path_length + path_delimiter_length;
	} else if (path)
		*path = NULL;

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the scheme, host, port, and path components
 *    from a URL.
 *
 *  This attempts to parse the scheme, host, port, and path components
 *  from the specified URL. If provided, the parsed scheme, host and
 *  path are copied and assigned to @a scheme, @a host, and @a path,
 *  respective and the parsed port is assigned to @a port.
 *
 *  Compliant with RFC 2732, the format of the host component of the
 *  URL may be one of the following:
 *
 *    1. "[\<IPv6 Address>]"
 *    2. "[\<IPv6 Address>]:<Port>"
 *    4. "\<IPv4 Address>"
 *    5. "\<IPv4 Address>:<Port>"
 *    6. "\<Host Name>"
 *    7. "\<Host Name>:<Port>"
 *
 *  @param[in]      url             A pointer to the immutable null-
 *                                  terminated C string from which to
 *                                  parse the scheme component.
 *  @param[in,out]  scheme          An optional pointer to storage to
 *                                  assign a copy of the parsed scheme
 *                                  on success.
 *  @param[in,out]  host            An optional pointer to storage to
 *                                  assign a copy of the parsed host
 *                                  on success.
 *  @param[in,out]  port            An optional pointer to storage to
 *                                  assign the parsed port on
 *                                  success. On failure or absence of
 *                                  a port to parsed, this is assigned
 *                                  -1.
 *  @param[in,out]  path            An optional pointer to storage to
 *                                  assign a copy of the parsed path
 *                                  on success.
 *
 *  @retval  0        If successful.
 *  @retavl  -EINVAL  If @a url was null, @a url length was zero, if
 *                    the host portion of @a url is malformed, or if
 *                    there were no characters to parse after the port
 *                    delimiter (':').
 *  @retval  -ENOMEM  If memory could not be allocated for the parsed
 *                    scheme, host, or path.
 *  @retval  -ERANGE  If the parsed port was outside of the range [0,
 *                    65535], inclusive.
 *
 *  @sa parse_url_scheme_with_default
 *  @sa parse_url_scheme
 *  @sa parse_url_host
 *  @sa parse_url_port
 *  @sa parse_url_host_and_port
 *  @sa parse_url_path
 *
 */
static int parse_url_components(const char *url,
						char **scheme,
						char **host,
						int16_t *port,
						char **path)
{
	size_t total_length;
	const char *p;
	int err;

	if (!url)
		return -EINVAL;

	p = url;

	total_length = strlen(p);
	if (!total_length)
		return -EINVAL;

	// Skip any leading space, if any.

	while ((p < (url + total_length)) && isspace(*p))
		p++;

	// Attempt to handle the scheme component.

	err = parse_url_scheme(url, total_length, &p, scheme);
	if (err != 0)
		return err;

	// Attempt to handle the host component.

	err = parse_url_host_and_port(url, total_length, &p, host, port);
	if (err != 0) {
		if (scheme) {
			g_free(*scheme);
			*scheme = NULL;
		}

		return err;
	}

	// Attempt to handle the path component.

	err = parse_url_path(url, total_length, &p, path);
	if (err != 0) {
		if (scheme) {
			g_free(*scheme);
			*scheme = NULL;
		}

		if (host) {
			g_free(*host);
			*host = NULL;
		}

		return err;
	}

	return 0;
}

/**
 *  @brief
 *    Attempt to parse the request and proxy URLs for the web request
 *    session.
 *
 *  This attempts to parse the specified request and optional proxy
 *  URL for the specified web request session. From the request URL,
 *  the scheme is parsed, mapped and assigned to the @a session port
 *  field and the host and path are parsed, copied, and assigned to
 *  the host and request fields, respectively. From the proxy URL, if
 *  present, the port component is parsed and assigned to the @a
 *  session port field and the host component is parsed, copied, and
 *  assigned to the address field.
 *
 *  Compliant with RFC 2732, the format of the host component of the
 *  request and proxy URLs may be one of the following:
 *
 *    1. "[\<IPv6 Address>]"
 *    2. "[\<IPv6 Address>]:<Port>"
 *    4. "\<IPv4 Address>"
 *    5. "\<IPv4 Address>:<Port>"
 *    6. "\<Host Name>"
 *    7. "\<Host Name>:<Port>"
 *
 *  @note
 *    The caller is responsible for deallocating the memory assigned
 *    to the @a session host, request, and address fields.
 *
 *  @param[in,out]  session  A pointer to the mutable web session request
 *                           object to be populated from @a url and,
 *                           if provided, @a proxy. On success, the
 *                           session port, host, request, and address
 *                           fields will be populated from the parsed
 *                           request URL and/or proxy URLs.
 *  @param[in]      url      A pointer to the immutable null-terminated
 *                           C string containing the request URL to
 *                           parse.
 *  @param[in]      proxy    An optional pointer to the immutable null-
 *                           terminated C string containing the web
 *                           proxy URL, if any, to parse.
 *
 *  @retval  0         If successful.
 *  @retval  -EINVAL  If @url was not a valid URL.
 *
 */
static int parse_request_and_proxy_urls(struct web_session *session,
				const char *url, const char *proxy)
{
	size_t proxy_length;
	const char *p;
	char *scheme = NULL;
	char *host = NULL;
	char *path = NULL;
	int16_t port = -1;
	int err;

	if (!session || !url)
		return -EINVAL;

	p = url;

	// Parse the URL components.

	err = parse_url_components(url,
							   &scheme,
							   &host,
							   &port,
							   &path);
	if (err != 0) {
		g_free(scheme);
		g_free(host);
		g_free(path);

		return err;
	}

	// Handle the URL scheme, if any, for the session, defaulting to
	// the "http" scheme and port 80.

	if (scheme) {
		if (strcasecmp(scheme, "https") == 0) {
			session->port = 443;
		} else if (strcasecmp(scheme, "http") == 0) {
			session->port = 80;
		} else {
			g_free(scheme);
			g_free(host);
			g_free(path);
			return -EINVAL;
		}

		g_free(scheme);
	} else {
		session->port = 80;
	}

	// Handle the URL host and port, if any, for the session.

	if (port != -1) {
		session->port = port;

		if (!proxy) {
			session->host = g_strdup(host);
			if (!session->host) {
				g_free(host);
				g_free(path);
				return -ENOMEM;
			}
		} else {
			session->host = g_strdup_printf("%s:%u", host, port);
			if (!session->host) {
				g_free(host);
				g_free(path);
				return -ENOMEM;
			}
		}
	} else {
		session->host = g_strdup(host);
		if (!session->host) {
			g_free(host);
			g_free(path);
			return -ENOMEM;
		}
	}

	// Handle the URL path, if any, for the session.

	if (!proxy) {
		session->request = g_strdup_printf("/%s", path ? path : "");
		if (!session->request) {
			g_free(host);
			g_free(path);
			return -ENOMEM;
		}
	} else {
		session->request = g_strdup(url);
		if (!session->request) {
			g_free(host);
			g_free(path);
			return -ENOMEM;
		}
	}

	g_free(host);
	g_free(path);

	if (!proxy)
		return 0;

	// Parse the proxy scheme, host, and port, the only three
	// components we care about.

	p = proxy;
	proxy_length = strlen(p);

	err = parse_url_scheme(proxy,
						   proxy_length,
						   &p,
						   &scheme);
	if (err != 0)
		return err;

	err = parse_url_host_and_port(proxy,
								  proxy_length,
								  &p,
								  &host,
								  &port);
	if (err != 0) {
		g_free(scheme);

		return err;
	}

	// Handle the proxy URL scheme, if any, for the session. Only
	// "http" is allowed.

	if (scheme) {
		if (strcasecmp(scheme, "http") != 0) {
			g_free(scheme);
			g_free(host);
			return -EINVAL;
		}

		g_free(scheme);
	}

	// Handle the proxy URL host and port for the session.

	if (host)
		session->address = host;

	if (port != -1)
		session->port = port;

	return 0;
}

static void handle_resolved_address(struct web_session *session)
{
	struct addrinfo hints;
	char *port;
	int ret;

	debug(session->web, "address %s", session->address);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = session->web->family;

	if (session->addr) {
		freeaddrinfo(session->addr);
		session->addr = NULL;
	}

	port = g_strdup_printf("%u", session->port);
	ret = getaddrinfo(session->address, port, &hints, &session->addr);
	g_free(port);
	if (ret != 0 || !session->addr) {
		call_result_func(session, GWEB_HTTP_STATUS_CODE_BAD_REQUEST);
		return;
	}

	call_route_func(session);

	if (create_transport(session) < 0) {
		call_result_func(session, GWEB_HTTP_STATUS_CODE_CONFLICT);
		return;
	}
}

static gboolean already_resolved(gpointer data)
{
	struct web_session *session = data;

	session->address_action = 0;
	handle_resolved_address(session);

	return FALSE;
}

static void resolv_result(GResolvResultStatus status,
					char **results, gpointer user_data)
{
	struct web_session *session = user_data;

	if (!results || !results[0]) {
		call_result_func(session, GWEB_HTTP_STATUS_CODE_NOT_FOUND);
		return;
	}

	g_free(session->address);
	session->address = g_strdup(results[0]);

	handle_resolved_address(session);
}

static bool is_ip_address(const char *host)
{
	struct addrinfo hints;
	struct addrinfo *addr;
	int result;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_NUMERICHOST;
	addr = NULL;

	result = getaddrinfo(host, NULL, &hints, &addr);
	if(!result)
		freeaddrinfo(addr);

	return result == 0;
}

static guint do_request(GWeb *web, const char *url,
				const char *type, GWebInputFunc input,
				int fd, gsize length, GWebResultFunc func,
				GWebRouteFunc route, gpointer user_data)
{
	struct web_session *session;
	const gchar *host;

	if (!web || !url)
		return 0;

	debug(web, "request %s", url);

	session = g_try_new0(struct web_session, 1);
	if (!session)
		return 0;

	if (parse_request_and_proxy_urls(session, url, web->proxy) < 0) {
		free_session(session);
		return 0;
	}

	debug(web, "proxy host %s", session->address);
	debug(web, "port %u", session->port);
	debug(web, "host %s", session->host);
	debug(web, "flags %lu", session->flags);
	debug(web, "request %s", session->request);

	if (type) {
		session->content_type = g_strdup(type);

		debug(web, "content-type %s", session->content_type);
	}

	session->web = web;

	session->result_func = func;
	session->route_func = route;
	session->input_func = input;
	session->fd = fd;
	session->length = length;
	session->offset = 0;
	session->user_data = user_data;

	session->receive_buffer = g_try_malloc(DEFAULT_BUFFER_SIZE);
	if (!session->receive_buffer) {
		free_session(session);
		return 0;
	}

	session->result.headers = g_hash_table_new_full(g_str_hash, g_str_equal,
							g_free, g_free);
	if (!session->result.headers) {
		free_session(session);
		return 0;
	}

	session->receive_space = DEFAULT_BUFFER_SIZE;
	session->send_buffer = g_string_sized_new(0);
	session->current_header = g_string_sized_new(0);
	session->header_done = false;
	session->body_done = false;

	host = session->address ? session->address : session->host;
	if (is_ip_address(host)) {
		if (session->address != host) {
			g_free(session->address);
			session->address = g_strdup(host);
		}
		session->address_action = g_idle_add(already_resolved, session);
	} else {
		session->resolv_action = g_resolv_lookup_hostname(web->resolv,
					host, resolv_result, session);
		if (session->resolv_action == 0) {
			free_session(session);
			return 0;
		}
	}

	web->session_list = g_list_append(web->session_list, session);

	return web->next_query_id++;
}

guint g_web_request_get(GWeb *web, const char *url, GWebResultFunc func,
		GWebRouteFunc route, gpointer user_data)
{
	return do_request(web, url, NULL, NULL, -1, 0, func, route, user_data);
}

guint g_web_request_post(GWeb *web, const char *url,
				const char *type, GWebInputFunc input,
				GWebResultFunc func, gpointer user_data)
{
	return do_request(web, url, type, input, -1, 0, func, NULL, user_data);
}

guint g_web_request_post_file(GWeb *web, const char *url,
				const char *type, const char *file,
				GWebResultFunc func, gpointer user_data)
{
	struct stat st;
	int fd;
	guint ret;

	if (stat(file, &st) < 0)
		return 0;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		return 0;

	ret = do_request(web, url, type, NULL, fd, st.st_size, func, NULL,
			user_data);
	if (ret == 0)
		close(fd);

	return ret;
}

bool g_web_cancel_request(GWeb *web, guint id)
{
	if (!web)
		return false;

	return true;
}

guint16 g_web_result_get_status(GWebResult *result)
{
	if (!result)
		return 0;

	return result->status;
}

bool g_web_result_get_chunk(GWebResult *result,
				const guint8 **chunk, gsize *length)
{
	if (!result)
		return false;

	if (!chunk)
		return false;

	*chunk = result->buffer;

	if (length)
		*length = result->length;

	return true;
}

bool g_web_result_get_header(GWebResult *result,
				const char *header, const char **value)
{
	if (!result)
		return false;

	if (!value)
		return false;

	*value = g_hash_table_lookup(result->headers, header);

	if (!*value)
		return false;

	return true;
}

struct _GWebParser {
	gint ref_count;
	char *begin_token;
	char *end_token;
	const char *token_str;
	size_t token_len;
	size_t token_pos;
	bool intoken;
	GString *content;
	GWebParserFunc func;
	gpointer user_data;
};

GWebParser *g_web_parser_new(const char *begin, const char *end,
				GWebParserFunc func, gpointer user_data)
{
	GWebParser *parser;

	if (!begin || !end)
		return NULL;

	parser = g_try_new0(GWebParser, 1);
	if (!parser)
		return NULL;

	parser->ref_count = 1;

	parser->begin_token = g_strdup(begin);
	parser->end_token = g_strdup(end);
	parser->func = func;
	parser->user_data = user_data;

	parser->token_str = parser->begin_token;
	parser->token_len = strlen(parser->token_str);
	parser->token_pos = 0;

	parser->intoken = false;
	parser->content = g_string_sized_new(0);

	return parser;
}

GWebParser *g_web_parser_ref(GWebParser *parser)
{
	if (!parser)
		return NULL;

	__sync_fetch_and_add(&parser->ref_count, 1);

	return parser;
}

void g_web_parser_unref(GWebParser *parser)
{
	if (!parser)
		return;

	if (__sync_fetch_and_sub(&parser->ref_count, 1) != 1)
		return;

	g_string_free(parser->content, TRUE);

	g_free(parser->begin_token);
	g_free(parser->end_token);
	g_free(parser);
}

void g_web_parser_feed_data(GWebParser *parser,
				const guint8 *data, gsize length)
{
	const guint8 *ptr = data;

	if (!parser)
		return;

	while (length > 0) {
		guint8 chr = parser->token_str[parser->token_pos];

		if (parser->token_pos == 0) {
			guint8 *pos;

			pos = memchr(ptr, chr, length);
			if (!pos) {
				if (parser->intoken)
					g_string_append_len(parser->content,
							(gchar *) ptr, length);
				break;
			}

			if (parser->intoken)
				g_string_append_len(parser->content,
						(gchar *) ptr, (pos - ptr) + 1);

			length -= (pos - ptr) + 1;
			ptr = pos + 1;

			parser->token_pos++;
			continue;
		}

		if (parser->intoken)
			g_string_append_c(parser->content, ptr[0]);

		if (ptr[0] != chr) {
			length--;
			ptr++;

			parser->token_pos = 0;
			continue;
		}

		length--;
		ptr++;

		parser->token_pos++;

		if (parser->token_pos == parser->token_len) {
			if (!parser->intoken) {
				g_string_append(parser->content,
							parser->token_str);

				parser->intoken = true;
				parser->token_str = parser->end_token;
				parser->token_len = strlen(parser->end_token);
				parser->token_pos = 0;
			} else {
				char *str;
				str = g_string_free(parser->content, FALSE);
				parser->content = g_string_sized_new(0);
				if (parser->func)
					parser->func(str, parser->user_data);
				g_free(str);

				parser->intoken = false;
				parser->token_str = parser->begin_token;
				parser->token_len = strlen(parser->begin_token);
				parser->token_pos = 0;
			}
		}
	}
}

void g_web_parser_end_data(GWebParser *parser)
{
	if (!parser)
		return;
}
