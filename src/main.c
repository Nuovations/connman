/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2007-2013  Intel Corporation. All rights reserved.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <net/if.h>
#include <netdb.h>

#include <gdbus.h>

#include "connman.h"

#define CONF_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]) - 1)

#define DEFAULT_INPUT_REQUEST_TIMEOUT (120 * 1000)
#define DEFAULT_BROWSER_LAUNCH_TIMEOUT (300 * 1000)

#define DEFAULT_ONLINE_CHECK_IPV4_URL "http://ipv4.connman.net/online/status.html"
#define DEFAULT_ONLINE_CHECK_IPV6_URL "http://ipv6.connman.net/online/status.html"

#define DEFAULT_ONLINE_CHECK_CONNECT_TIMEOUT (0 * 1000)
/*
 * We set the integer to 1 sec so that we have a chance to get
 * necessary IPv6 router advertisement messages that might have
 * DNS data etc.
 */
#define DEFAULT_ONLINE_CHECK_INITIAL_INTERVAL 1
#define DEFAULT_ONLINE_CHECK_MAX_INTERVAL 12

#define DEFAULT_ONLINE_CHECK_FAILURES_THRESHOLD 6
#define DEFAULT_ONLINE_CHECK_SUCCESSES_THRESHOLD 6

#define ONLINE_CHECK_INTERVAL_STYLE_FIBONACCI "fibonacci"
#define ONLINE_CHECK_INTERVAL_STYLE_GEOMETRIC "geometric"

#define DEFAULT_ONLINE_CHECK_INTERVAL_STYLE ONLINE_CHECK_INTERVAL_STYLE_GEOMETRIC

#define DEFAULT_LOCALTIME "/etc/localtime"

#define MAINFILE "main.conf"
#define CONFIGMAINFILE CONFIGDIR "/" MAINFILE

#define GENERAL_GROUP "General"

static char *default_auto_connect[] = {
	"wifi",
	"ethernet",
	"cellular",
	NULL
};

static char * default_enabled_techs[] = {
	"ethernet",
	NULL
};

static char *default_favorite_techs[] = {
	"ethernet",
	NULL
};

static char *default_blacklist[] = {
	"vmnet",
	"vboxnet",
	"virbr",
	"ifb",
	"ve-",
	"vb-",
	"ham",
	"veth",
	NULL
};

static struct {
	bool bg_scan;
	char **pref_timeservers;
	unsigned int *auto_connect;
	unsigned int *enabled_techs;
	unsigned int *favorite_techs;
	unsigned int *preferred_techs;
	unsigned int *always_connected_techs;
	char **fallback_nameservers;
	unsigned int timeout_inputreq;
	unsigned int timeout_browserlaunch;
	char **blacklisted_interfaces;
	bool allow_hostname_updates;
	bool allow_domainname_updates;
	bool single_tech;
	char **tethering_technologies;
	bool persistent_tethering_mode;
	bool enable_6to4;
	char *vendor_class_id;
	bool enable_online_check;
	bool enable_online_to_ready_transition;
	enum service_online_check_mode online_check_mode;
	char *online_check_ipv4_url;
	char *online_check_ipv6_url;
	unsigned int online_check_connect_timeout_ms;
	unsigned int online_check_initial_interval;
	unsigned int online_check_max_interval;
	unsigned int online_check_failures_threshold;
	unsigned int online_check_successes_threshold;
	char *online_check_interval_style;
	bool auto_connect_roaming_services;
	bool acd;
	bool use_gateways_as_timeservers;
	char *localtime;
	bool regdom_follows_timezone;
	char *resolv_conf;
} connman_settings  = {
	.bg_scan = true,
	.pref_timeservers = NULL,
	.auto_connect = NULL,
	.enabled_techs = NULL,
	.favorite_techs = NULL,
	.preferred_techs = NULL,
	.always_connected_techs = NULL,
	.fallback_nameservers = NULL,
	.timeout_inputreq = DEFAULT_INPUT_REQUEST_TIMEOUT,
	.timeout_browserlaunch = DEFAULT_BROWSER_LAUNCH_TIMEOUT,
	.blacklisted_interfaces = NULL,
	.allow_hostname_updates = true,
	.allow_domainname_updates = true,
	.single_tech = false,
	.tethering_technologies = NULL,
	.persistent_tethering_mode = false,
	.enable_6to4 = false,
	.vendor_class_id = NULL,
	.enable_online_check = true,
	.enable_online_to_ready_transition = false,
	.online_check_mode = CONNMAN_SERVICE_ONLINE_CHECK_MODE_UNKNOWN,
	.online_check_ipv4_url = NULL,
	.online_check_ipv6_url = NULL,
	.online_check_connect_timeout_ms = DEFAULT_ONLINE_CHECK_CONNECT_TIMEOUT,
	.online_check_initial_interval = DEFAULT_ONLINE_CHECK_INITIAL_INTERVAL,
	.online_check_max_interval = DEFAULT_ONLINE_CHECK_MAX_INTERVAL,
	.online_check_failures_threshold =
		DEFAULT_ONLINE_CHECK_FAILURES_THRESHOLD,
	.online_check_successes_threshold =
		DEFAULT_ONLINE_CHECK_SUCCESSES_THRESHOLD,
	.online_check_interval_style = NULL,
	.auto_connect_roaming_services = false,
	.acd = false,
	.use_gateways_as_timeservers = false,
	.localtime = NULL,
	.resolv_conf = NULL,
};

#define CONF_BG_SCAN                    "BackgroundScanning"
#define CONF_PREF_TIMESERVERS           "FallbackTimeservers"
#define CONF_AUTO_CONNECT_TECHS         "DefaultAutoConnectTechnologies"
#define CONF_ENABLED_TECHS              "DefaultEnabledTechnologies"
#define CONF_FAVORITE_TECHS             "DefaultFavoriteTechnologies"
#define CONF_ALWAYS_CONNECTED_TECHS     "AlwaysConnectedTechnologies"
#define CONF_PREFERRED_TECHS            "PreferredTechnologies"
#define CONF_FALLBACK_NAMESERVERS       "FallbackNameservers"
#define CONF_TIMEOUT_INPUTREQ           "InputRequestTimeout"
#define CONF_TIMEOUT_BROWSERLAUNCH      "BrowserLaunchTimeout"
#define CONF_BLACKLISTED_INTERFACES     "NetworkInterfaceBlacklist"
#define CONF_ALLOW_HOSTNAME_UPDATES     "AllowHostnameUpdates"
#define CONF_ALLOW_DOMAINNAME_UPDATES   "AllowDomainnameUpdates"
#define CONF_SINGLE_TECH                "SingleConnectedTechnology"
#define CONF_TETHERING_TECHNOLOGIES      "TetheringTechnologies"
#define CONF_PERSISTENT_TETHERING_MODE  "PersistentTetheringMode"
#define CONF_ENABLE_6TO4                "Enable6to4"
#define CONF_VENDOR_CLASS_ID            "VendorClassID"
#define CONF_ENABLE_ONLINE_CHECK        "EnableOnlineCheck"
#define CONF_ENABLE_ONLINE_TO_READY_TRANSITION "EnableOnlineToReadyTransition"
#define CONF_ONLINE_CHECK_MODE          "OnlineCheckMode"
#define CONF_ONLINE_CHECK_IPV4_URL      "OnlineCheckIPv4URL"
#define CONF_ONLINE_CHECK_IPV6_URL      "OnlineCheckIPv6URL"
#define CONF_ONLINE_CHECK_CONNECT_TIMEOUT "OnlineCheckConnectTimeout"
#define CONF_ONLINE_CHECK_INITIAL_INTERVAL "OnlineCheckInitialInterval"
#define CONF_ONLINE_CHECK_MAX_INTERVAL     "OnlineCheckMaxInterval"
#define CONF_ONLINE_CHECK_FAILURES_THRESHOLD "OnlineCheckFailuresThreshold"
#define CONF_ONLINE_CHECK_SUCCESSES_THRESHOLD "OnlineCheckSuccessesThreshold"
#define CONF_ONLINE_CHECK_INTERVAL_STYLE "OnlineCheckIntervalStyle"
#define CONF_AUTO_CONNECT_ROAMING_SERVICES "AutoConnectRoamingServices"
#define CONF_ACD                        "AddressConflictDetection"
#define CONF_USE_GATEWAYS_AS_TIMESERVERS "UseGatewaysAsTimeservers"
#define CONF_LOCALTIME                  "Localtime"
#define CONF_REGDOM_FOLLOWS_TIMEZONE    "RegdomFollowsTimezone"
#define CONF_RESOLV_CONF                "ResolvConf"

static const char *supported_options[] = {
	CONF_BG_SCAN,
	CONF_PREF_TIMESERVERS,
	CONF_AUTO_CONNECT_TECHS,
	CONF_ENABLED_TECHS,
	CONF_FAVORITE_TECHS,
	CONF_ALWAYS_CONNECTED_TECHS,
	CONF_PREFERRED_TECHS,
	CONF_FALLBACK_NAMESERVERS,
	CONF_TIMEOUT_INPUTREQ,
	CONF_TIMEOUT_BROWSERLAUNCH,
	CONF_BLACKLISTED_INTERFACES,
	CONF_ALLOW_HOSTNAME_UPDATES,
	CONF_ALLOW_DOMAINNAME_UPDATES,
	CONF_SINGLE_TECH,
	CONF_TETHERING_TECHNOLOGIES,
	CONF_PERSISTENT_TETHERING_MODE,
	CONF_ENABLE_6TO4,
	CONF_VENDOR_CLASS_ID,
	CONF_ENABLE_ONLINE_CHECK,
	CONF_ENABLE_ONLINE_TO_READY_TRANSITION,
	CONF_ONLINE_CHECK_MODE,
	CONF_ONLINE_CHECK_IPV4_URL,
	CONF_ONLINE_CHECK_IPV6_URL,
	CONF_ONLINE_CHECK_CONNECT_TIMEOUT,
	CONF_ONLINE_CHECK_INITIAL_INTERVAL,
	CONF_ONLINE_CHECK_MAX_INTERVAL,
	CONF_ONLINE_CHECK_FAILURES_THRESHOLD,
	CONF_ONLINE_CHECK_SUCCESSES_THRESHOLD,
	CONF_ONLINE_CHECK_INTERVAL_STYLE,
	CONF_AUTO_CONNECT_ROAMING_SERVICES,
	CONF_ACD,
	CONF_USE_GATEWAYS_AS_TIMESERVERS,
	CONF_LOCALTIME,
	CONF_REGDOM_FOLLOWS_TIMEZONE,
	CONF_RESOLV_CONF,
	NULL
};

static GKeyFile *load_config(const char *file)
{
	GError *err = NULL;
	GKeyFile *keyfile;

	keyfile = g_key_file_new();

	g_key_file_set_list_separator(keyfile, ',');

	if (!g_key_file_load_from_file(keyfile, file, 0, &err)) {
		if (err->code != G_FILE_ERROR_NOENT) {
			connman_error("Parsing %s failed: %s", file,
								err->message);
		}

		g_error_free(err);
		g_key_file_free(keyfile);
		return NULL;
	}

	return keyfile;
}

static uint *parse_service_types(char **str_list, gsize len)
{
	unsigned int *type_list;
	int i, j;
	enum connman_service_type type;

	type_list = g_try_new0(unsigned int, len + 1);
	if (!type_list)
		return NULL;

	i = 0;
	j = 0;
	while (str_list[i]) {
		type = __connman_service_string2type(str_list[i]);

		if (type != CONNMAN_SERVICE_TYPE_UNKNOWN) {
			type_list[j] = type;
			j += 1;
		}
		i += 1;
	}

	type_list[j] = CONNMAN_SERVICE_TYPE_UNKNOWN;

	return type_list;
}

static char **parse_fallback_nameservers(char **nameservers, gsize len)
{
	char **servers;
	int i, j;

	servers = g_try_new0(char *, len + 1);
	if (!servers)
		return NULL;

	i = 0;
	j = 0;
	while (nameservers[i]) {
		if (connman_inet_check_ipaddress(nameservers[i]) > 0) {
			servers[j] = g_strdup(nameservers[i]);
			j += 1;
		}
		i += 1;
	}

	return servers;
}

static void check_config(GKeyFile *config, const char *file)
{
	char **keys;
	int j;

	if (!config || !file)
		return;

	keys = g_key_file_get_groups(config, NULL);

	for (j = 0; keys && keys[j]; j++) {
		if (g_strcmp0(keys[j], GENERAL_GROUP) != 0)
			connman_warn("Unknown group %s in %s",
						keys[j], file);
	}

	g_strfreev(keys);

	keys = g_key_file_get_keys(config, GENERAL_GROUP, NULL, NULL);

	for (j = 0; keys && keys[j]; j++) {
		bool found;
		int i;

		found = false;
		for (i = 0; supported_options[i]; i++) {
			if (g_strcmp0(keys[j], supported_options[i]) == 0) {
				found = true;
				break;
			}
		}
		if (!found && !supported_options[i])
			connman_warn("Unknown option %s in %s",
						keys[j], file);
	}

	g_strfreev(keys);
}

static void online_check_mode_set_from_deprecated(void)
{
	connman_settings.online_check_mode =
		connman_settings.enable_online_check ?
		connman_settings.enable_online_to_ready_transition ?
			CONNMAN_SERVICE_ONLINE_CHECK_MODE_CONTINUOUS :
			CONNMAN_SERVICE_ONLINE_CHECK_MODE_ONE_SHOT :
		CONNMAN_SERVICE_ONLINE_CHECK_MODE_NONE;
}

static void online_check_mode_set_to_deprecated(void)
{
	switch (connman_settings.online_check_mode) {
	case CONNMAN_SERVICE_ONLINE_CHECK_MODE_NONE:
		connman_settings.enable_online_check = false;
		connman_settings.enable_online_to_ready_transition = false;
		break;
	case CONNMAN_SERVICE_ONLINE_CHECK_MODE_ONE_SHOT:
		connman_settings.enable_online_check = true;
		connman_settings.enable_online_to_ready_transition = false;
		break;
	case CONNMAN_SERVICE_ONLINE_CHECK_MODE_CONTINUOUS:
		connman_settings.enable_online_check = true;
		connman_settings.enable_online_to_ready_transition = true;
		break;
	default:
		break;
	}
}

static void online_check_settings_log(void)
{
	connman_info("Online check mode \"%s\"",
				 __connman_service_online_check_mode2string(
					connman_settings.online_check_mode));

	if (connman_settings.online_check_mode ==
			CONNMAN_SERVICE_ONLINE_CHECK_MODE_NONE)
		return;

	connman_info("Online check IPv4 URL \"%s\"",
		connman_settings.online_check_ipv4_url);

	connman_info("Online check IPv6 URL \"%s\"",
		connman_settings.online_check_ipv6_url);

	connman_info("Online check interval style \"%s\"",
		connman_settings.online_check_interval_style);

	connman_info("Online check interval range [%u, %u]",
		connman_settings.online_check_initial_interval,
		connman_settings.online_check_max_interval);

	if (connman_settings.online_check_connect_timeout_ms)
		connman_info("Online check connect timeout %u ms",
			connman_settings.online_check_connect_timeout_ms);

	if (connman_settings.online_check_mode !=
			CONNMAN_SERVICE_ONLINE_CHECK_MODE_CONTINUOUS)
		return;

	connman_info("Online check continuous mode failures threshold %d",
		connman_settings.online_check_failures_threshold);

	connman_info("Online check continuous mode successes threshold %d",
		connman_settings.online_check_successes_threshold);
}

static void parse_config(GKeyFile *config, const char *file)
{
	GError *error = NULL;
	bool boolean;
	char **timeservers;
	char **interfaces;
	char **str_list;
	char **tethering;
	char *string;
	gsize len;
	int integer;
	double real;

	if (!config || !file) {
		connman_settings.auto_connect =
			parse_service_types(default_auto_connect,
					CONF_ARRAY_SIZE(default_auto_connect));
		connman_settings.enabled_techs =
			parse_service_types(default_enabled_techs,
					CONF_ARRAY_SIZE(default_enabled_techs));
		connman_settings.favorite_techs =
			parse_service_types(default_favorite_techs,
					CONF_ARRAY_SIZE(default_favorite_techs));
		connman_settings.blacklisted_interfaces =
			g_strdupv(default_blacklist);
		connman_settings.online_check_ipv4_url =
			g_strdup(DEFAULT_ONLINE_CHECK_IPV4_URL);
		connman_settings.online_check_ipv6_url =
			g_strdup(DEFAULT_ONLINE_CHECK_IPV6_URL);
		connman_settings.online_check_interval_style =
			g_strdup(DEFAULT_ONLINE_CHECK_INTERVAL_STYLE);
		return;
	}

	DBG("parsing %s", file);

	boolean = g_key_file_get_boolean(config, GENERAL_GROUP,
						CONF_BG_SCAN, &error);
	if (!error)
		connman_settings.bg_scan = boolean;

	g_clear_error(&error);

	timeservers = __connman_config_get_string_list(config, GENERAL_GROUP,
					CONF_PREF_TIMESERVERS, NULL, &error);
	if (!error)
		connman_settings.pref_timeservers = timeservers;

	g_clear_error(&error);

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_AUTO_CONNECT_TECHS, &len, &error);

	if (!error)
		connman_settings.auto_connect =
			parse_service_types(str_list, len);
	else
		connman_settings.auto_connect =
			parse_service_types(default_auto_connect, CONF_ARRAY_SIZE(default_auto_connect));

	g_strfreev(str_list);

	g_clear_error(&error);

	/* DefaultEnabledTechnologies */

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_ENABLED_TECHS, &len, &error);

	if (!error)
		connman_settings.enabled_techs =
			parse_service_types(str_list, len);
	else
		connman_settings.enabled_techs =
			parse_service_types(default_enabled_techs, CONF_ARRAY_SIZE(default_enabled_techs));

	g_strfreev(str_list);

	g_clear_error(&error);

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_FAVORITE_TECHS, &len, &error);

	if (!error)
		connman_settings.favorite_techs =
			parse_service_types(str_list, len);
	else
		connman_settings.favorite_techs =
			parse_service_types(default_favorite_techs, CONF_ARRAY_SIZE(default_favorite_techs));

	g_strfreev(str_list);

	g_clear_error(&error);

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_PREFERRED_TECHS, &len, &error);

	if (!error)
		connman_settings.preferred_techs =
			parse_service_types(str_list, len);

	g_strfreev(str_list);

	g_clear_error(&error);

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_ALWAYS_CONNECTED_TECHS, &len, &error);

	if (!error)
		connman_settings.always_connected_techs =
			parse_service_types(str_list, len);

	g_strfreev(str_list);

	g_clear_error(&error);

	str_list = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_FALLBACK_NAMESERVERS, &len, &error);

	if (!error)
		connman_settings.fallback_nameservers =
			parse_fallback_nameservers(str_list, len);

	g_strfreev(str_list);

	g_clear_error(&error);

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_TIMEOUT_INPUTREQ, &error);
	if (!error && integer >= 0)
		connman_settings.timeout_inputreq = integer * 1000;

	g_clear_error(&error);

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_TIMEOUT_BROWSERLAUNCH, &error);
	if (!error && integer >= 0)
		connman_settings.timeout_browserlaunch = integer * 1000;

	g_clear_error(&error);

	interfaces = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_BLACKLISTED_INTERFACES, &len, &error);

	if (!error)
		connman_settings.blacklisted_interfaces = interfaces;
	else
		connman_settings.blacklisted_interfaces =
			g_strdupv(default_blacklist);

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
					CONF_ALLOW_HOSTNAME_UPDATES,
					&error);
	if (!error)
		connman_settings.allow_hostname_updates = boolean;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
					CONF_ALLOW_DOMAINNAME_UPDATES,
					&error);
	if (!error)
		connman_settings.allow_domainname_updates = boolean;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
			CONF_SINGLE_TECH, &error);
	if (!error)
		connman_settings.single_tech = boolean;

	g_clear_error(&error);

	tethering = __connman_config_get_string_list(config, GENERAL_GROUP,
			CONF_TETHERING_TECHNOLOGIES, &len, &error);

	if (!error)
		connman_settings.tethering_technologies = tethering;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
					CONF_PERSISTENT_TETHERING_MODE,
					&error);
	if (!error)
		connman_settings.persistent_tethering_mode = boolean;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
					CONF_ENABLE_6TO4, &error);
	if (!error)
		connman_settings.enable_6to4 = boolean;

	g_clear_error(&error);

	string = __connman_config_get_string(config, GENERAL_GROUP,
					CONF_VENDOR_CLASS_ID, &error);
	if (!error)
		connman_settings.vendor_class_id = string;

	g_clear_error(&error);

	/* EnableOnlineCheck */

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
					CONF_ENABLE_ONLINE_CHECK, &error);
	if (!error) {
		connman_warn("\"%s\" is deprecated; use \"%s\" instead.",
			CONF_ENABLE_ONLINE_CHECK,
			CONF_ONLINE_CHECK_MODE);

		connman_settings.enable_online_check = boolean;
	}

	g_clear_error(&error);

	/* EnableOnlineToReadyTransition */

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
			CONF_ENABLE_ONLINE_TO_READY_TRANSITION, &error);
	if (!error) {
		connman_warn("\"%s\" is deprecated; use \"%s\" instead.",
			CONF_ENABLE_ONLINE_TO_READY_TRANSITION,
			CONF_ONLINE_CHECK_MODE);

		connman_settings.enable_online_to_ready_transition = boolean;
	}

	g_clear_error(&error);

	/* OnlineCheckMode */

	string = __connman_config_get_string(config, GENERAL_GROUP,
				CONF_ONLINE_CHECK_MODE, &error);
	if (!error) {
		connman_settings.online_check_mode =
			__connman_service_online_check_string2mode(string);
		if (connman_settings.online_check_mode ==
			CONNMAN_SERVICE_ONLINE_CHECK_MODE_UNKNOWN) {
			connman_error("Invalid online check mode \"%s\"",
				string);

			online_check_mode_set_from_deprecated();
		} else
			online_check_mode_set_to_deprecated();
	} else
		online_check_mode_set_from_deprecated();

	g_clear_error(&error);

	/* OnlineCheckConnectTimeout */

	real = g_key_file_get_double(config, GENERAL_GROUP,
			CONF_ONLINE_CHECK_CONNECT_TIMEOUT, &error);
	if (!error) {
		if (real < 0) {
			connman_warn("Incorrect online check connect timeout %f",
				real);
			connman_settings.online_check_connect_timeout_ms =
				DEFAULT_ONLINE_CHECK_CONNECT_TIMEOUT;
		} else
			connman_settings.online_check_connect_timeout_ms =
				real * 1000;
	}

	g_clear_error(&error);

	/* OnlineCheckIPv4URL */

	string = __connman_config_get_string(config, GENERAL_GROUP,
					CONF_ONLINE_CHECK_IPV4_URL, &error);
	if (!error)
		connman_settings.online_check_ipv4_url = string;
	else
		connman_settings.online_check_ipv4_url =
			g_strdup(DEFAULT_ONLINE_CHECK_IPV4_URL);

	g_clear_error(&error);

	/* OnlineCheckIPv6URL */

	string = __connman_config_get_string(config, GENERAL_GROUP,
					CONF_ONLINE_CHECK_IPV6_URL, &error);
	if (!error)
		connman_settings.online_check_ipv6_url = string;
	else
		connman_settings.online_check_ipv6_url =
			g_strdup(DEFAULT_ONLINE_CHECK_IPV6_URL);

	g_clear_error(&error);

	/* OnlineCheck{Initial,Max}Interval */

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_ONLINE_CHECK_INITIAL_INTERVAL, &error);
	if (!error && integer >= 0)
		connman_settings.online_check_initial_interval = integer;

	g_clear_error(&error);

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_ONLINE_CHECK_MAX_INTERVAL, &error);
	if (!error && integer >= 0)
		connman_settings.online_check_max_interval = integer;

	g_clear_error(&error);

	if (connman_settings.online_check_initial_interval < 1 ||
		connman_settings.online_check_initial_interval >
		connman_settings.online_check_max_interval) {
		connman_warn("Incorrect online check intervals [%u, %u]",
				connman_settings.online_check_initial_interval,
				connman_settings.online_check_max_interval);
		connman_settings.online_check_initial_interval =
			DEFAULT_ONLINE_CHECK_INITIAL_INTERVAL;
		connman_settings.online_check_max_interval =
			DEFAULT_ONLINE_CHECK_MAX_INTERVAL;
	}

	/* OnlineCheckFailuresThreshold */

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_ONLINE_CHECK_FAILURES_THRESHOLD, &error);
	if (!error && integer >= 0)
		connman_settings.online_check_failures_threshold = integer;

	if (connman_settings.online_check_failures_threshold < 1) {
		connman_warn("Incorrect online check failures threshold [%d]",
			connman_settings.online_check_failures_threshold);
		connman_settings.online_check_failures_threshold =
			DEFAULT_ONLINE_CHECK_FAILURES_THRESHOLD;
	}

	g_clear_error(&error);

	/* OnlineCheckSuccessesThreshold */

	integer = g_key_file_get_integer(config, GENERAL_GROUP,
			CONF_ONLINE_CHECK_SUCCESSES_THRESHOLD, &error);
	if (!error && integer >= 0)
		connman_settings.online_check_successes_threshold = integer;

	if (connman_settings.online_check_successes_threshold < 1) {
		connman_warn("Incorrect online check successes threshold [%d]",
			connman_settings.online_check_successes_threshold);
		connman_settings.online_check_successes_threshold =
			DEFAULT_ONLINE_CHECK_SUCCESSES_THRESHOLD;
	}

	g_clear_error(&error);

	/* OnlineCheckIntervalStyle */

	string = __connman_config_get_string(config, GENERAL_GROUP,
					CONF_ONLINE_CHECK_INTERVAL_STYLE, &error);
	if (!error) {
		if ((g_strcmp0(string, ONLINE_CHECK_INTERVAL_STYLE_FIBONACCI) == 0) ||
			(g_strcmp0(string, ONLINE_CHECK_INTERVAL_STYLE_GEOMETRIC) == 0)) {
			connman_settings.online_check_interval_style = string;
		} else {
			connman_warn("Incorrect online check interval style [%s]",
				string);
			connman_settings.online_check_interval_style =
				g_strdup(DEFAULT_ONLINE_CHECK_INTERVAL_STYLE);
		}
	} else
		connman_settings.online_check_interval_style =
			g_strdup(DEFAULT_ONLINE_CHECK_INTERVAL_STYLE);

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
				CONF_AUTO_CONNECT_ROAMING_SERVICES, &error);
	if (!error)
		connman_settings.auto_connect_roaming_services = boolean;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
				CONF_ACD, &error);
	if (!error)
		connman_settings.acd = boolean;

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
				CONF_USE_GATEWAYS_AS_TIMESERVERS, &error);
	if (!error)
		connman_settings.use_gateways_as_timeservers = boolean;

	g_clear_error(&error);

	string = __connman_config_get_string(config, GENERAL_GROUP,
				CONF_LOCALTIME, &error);
	if (!error)
		connman_settings.localtime = string;
	else
		g_free(string);

	g_clear_error(&error);

	boolean = __connman_config_get_bool(config, GENERAL_GROUP,
				CONF_REGDOM_FOLLOWS_TIMEZONE, &error);
	if (!error)
		connman_settings.regdom_follows_timezone = boolean;

	g_clear_error(&error);

	string = __connman_config_get_string(config, GENERAL_GROUP,
				CONF_RESOLV_CONF, &error);
	if (!error)
		connman_settings.resolv_conf = string;
	else
		g_free(string);

	g_clear_error(&error);

	online_check_settings_log();
}

static int config_init(const char *file)
{
	GKeyFile *config;

	config = load_config(file);
	check_config(config, file);
	parse_config(config, file);
	if (config)
		g_key_file_free(config);

	return 0;
}

static GMainLoop *main_loop = NULL;

static unsigned int __terminated = 0;

static gboolean signal_handler(GIOChannel *channel, GIOCondition cond,
							gpointer user_data)
{
	struct signalfd_siginfo si;
	ssize_t result;
	int fd;

	if (cond & (G_IO_NVAL | G_IO_ERR | G_IO_HUP))
		return FALSE;

	fd = g_io_channel_unix_get_fd(channel);

	result = read(fd, &si, sizeof(si));
	if (result != sizeof(si))
		return FALSE;

	switch (si.ssi_signo) {
	case SIGINT:
	case SIGTERM:
		if (__terminated == 0) {
			connman_info("Terminating");
			g_main_loop_quit(main_loop);
		}

		__terminated = 1;
		break;
	}

	return TRUE;
}

static guint setup_signalfd(void)
{
	GIOChannel *channel;
	guint source;
	sigset_t mask;
	int fd;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		perror("Failed to set signal mask");
		return 0;
	}

	fd = signalfd(-1, &mask, 0);
	if (fd < 0) {
		perror("Failed to create signal descriptor");
		return 0;
	}

	channel = g_io_channel_unix_new(fd);

	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	source = g_io_add_watch(channel,
				G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
				signal_handler, NULL);

	g_io_channel_unref(channel);

	return source;
}

static void disconnect_callback(DBusConnection *conn, void *user_data)
{
	connman_error("D-Bus disconnect");

	g_main_loop_quit(main_loop);
}

static gchar *option_config = NULL;
static gchar *option_debug = NULL;
static gchar *option_device = NULL;
static gchar *option_plugin = NULL;
static gchar *option_nodevice = NULL;
static gchar *option_noplugin = NULL;
static gchar *option_wifi = NULL;
static gboolean option_detach = TRUE;
static gboolean option_dnsproxy = TRUE;
static gboolean option_backtrace = TRUE;
static gboolean option_version = FALSE;

static bool parse_debug(const char *key, const char *value,
					gpointer user_data, GError **error)
{
	if (value) {
		if (option_debug) {
			char *prev = option_debug;

			option_debug = g_strconcat(prev, ",", value, NULL);
			g_free(prev);
		} else {
			option_debug = g_strdup(value);
		}
	} else {
		g_free(option_debug);
		option_debug = g_strdup("*");
	}

	return true;
}

static bool parse_noplugin(const char *key, const char *value,
					gpointer user_data, GError **error)
{
	if (option_noplugin) {
		char *prev = option_noplugin;

		option_noplugin = g_strconcat(prev, ",", value, NULL);
		g_free(prev);
	} else {
		option_noplugin = g_strdup(value);
	}

	return true;
}

static GOptionEntry options[] = {
	{ "config", 'c', 0, G_OPTION_ARG_STRING, &option_config,
				"Load the specified configuration file "
				"instead of " CONFIGMAINFILE, "FILE" },
	{ "debug", 'd', G_OPTION_FLAG_OPTIONAL_ARG,
				G_OPTION_ARG_CALLBACK, parse_debug,
				"Specify debug options to enable", "DEBUG" },
	{ "device", 'i', 0, G_OPTION_ARG_STRING, &option_device,
			"Specify networking devices or interfaces", "DEV,..." },
	{ "nodevice", 'I', 0, G_OPTION_ARG_STRING, &option_nodevice,
			"Specify networking interfaces to ignore", "DEV,..." },
	{ "plugin", 'p', 0, G_OPTION_ARG_STRING, &option_plugin,
				"Specify plugins to load", "NAME,..." },
	{ "noplugin", 'P', 0, G_OPTION_ARG_CALLBACK, &parse_noplugin,
				"Specify plugins not to load", "NAME,..." },
	{ "wifi", 'W', 0, G_OPTION_ARG_STRING, &option_wifi,
				"Specify driver for WiFi/Supplicant", "NAME" },
	{ "nodaemon", 'n', G_OPTION_FLAG_REVERSE,
				G_OPTION_ARG_NONE, &option_detach,
				"Don't fork daemon to background" },
	{ "nodnsproxy", 'r', G_OPTION_FLAG_REVERSE,
				G_OPTION_ARG_NONE, &option_dnsproxy,
				"Don't support DNS resolving" },
	{ "nobacktrace", 0, G_OPTION_FLAG_REVERSE,
				G_OPTION_ARG_NONE, &option_backtrace,
				"Don't print out backtrace information" },
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
				"Show version information and exit" },
	{ NULL },
};

char *connman_setting_get_string(const char *key)
{
	if (g_str_equal(key, CONF_VENDOR_CLASS_ID))
		return connman_settings.vendor_class_id;

	if (g_str_equal(key, CONF_ONLINE_CHECK_IPV4_URL))
		return connman_settings.online_check_ipv4_url;

	if (g_str_equal(key, CONF_ONLINE_CHECK_IPV6_URL))
		return connman_settings.online_check_ipv6_url;

	if (g_strcmp0(key, "wifi") == 0) {
		if (!option_wifi)
			return "nl80211,wext";
		else
			return option_wifi;
	}

	if (g_str_equal(key, CONF_LOCALTIME))
		return connman_settings.localtime ?
			connman_settings.localtime : DEFAULT_LOCALTIME;

	if (g_str_equal(key, CONF_RESOLV_CONF))
		return connman_settings.resolv_conf;

	if (g_str_equal(key, CONF_ONLINE_CHECK_INTERVAL_STYLE))
		return connman_settings.online_check_interval_style;

	return NULL;
}

bool connman_setting_get_bool(const char *key)
{
	if (g_str_equal(key, CONF_BG_SCAN))
		return connman_settings.bg_scan;

	if (g_str_equal(key, CONF_ALLOW_HOSTNAME_UPDATES))
		return connman_settings.allow_hostname_updates;

	if (g_str_equal(key, CONF_ALLOW_DOMAINNAME_UPDATES))
		return connman_settings.allow_domainname_updates;

	if (g_str_equal(key, CONF_SINGLE_TECH))
		return connman_settings.single_tech;

	if (g_str_equal(key, CONF_PERSISTENT_TETHERING_MODE))
		return connman_settings.persistent_tethering_mode;

	if (g_str_equal(key, CONF_ENABLE_6TO4))
		return connman_settings.enable_6to4;

	if (g_str_equal(key, CONF_ENABLE_ONLINE_CHECK))
		return connman_settings.enable_online_check;

	if (g_str_equal(key, CONF_ENABLE_ONLINE_TO_READY_TRANSITION))
		return connman_settings.enable_online_to_ready_transition;

	if (g_str_equal(key, CONF_AUTO_CONNECT_ROAMING_SERVICES))
		return connman_settings.auto_connect_roaming_services;

	if (g_str_equal(key, CONF_ACD))
		return connman_settings.acd;

	if (g_str_equal(key, CONF_USE_GATEWAYS_AS_TIMESERVERS))
		return connman_settings.use_gateways_as_timeservers;

	if (g_str_equal(key, CONF_REGDOM_FOLLOWS_TIMEZONE))
		return connman_settings.regdom_follows_timezone;

	return false;
}

unsigned int connman_setting_get_uint(const char *key)
{
	if (g_str_equal(key, CONF_ONLINE_CHECK_CONNECT_TIMEOUT))
		return connman_settings.online_check_connect_timeout_ms;

	if (g_str_equal(key, CONF_ONLINE_CHECK_INITIAL_INTERVAL))
		return connman_settings.online_check_initial_interval;

	if (g_str_equal(key, CONF_ONLINE_CHECK_MAX_INTERVAL))
		return connman_settings.online_check_max_interval;

	if (g_str_equal(key, CONF_ONLINE_CHECK_MODE))
		return connman_settings.online_check_mode;

	if (g_str_equal(key, CONF_ONLINE_CHECK_FAILURES_THRESHOLD))
		return connman_settings.online_check_failures_threshold;

	if (g_str_equal(key, CONF_ONLINE_CHECK_SUCCESSES_THRESHOLD))
		return connman_settings.online_check_successes_threshold;

	return 0;
}

char **connman_setting_get_string_list(const char *key)
{
	if (g_str_equal(key, CONF_PREF_TIMESERVERS))
		return connman_settings.pref_timeservers;

	if (g_str_equal(key, CONF_FALLBACK_NAMESERVERS))
		return connman_settings.fallback_nameservers;

	if (g_str_equal(key, CONF_BLACKLISTED_INTERFACES))
		return connman_settings.blacklisted_interfaces;

	if (g_str_equal(key, CONF_TETHERING_TECHNOLOGIES))
		return connman_settings.tethering_technologies;

	return NULL;
}

unsigned int *connman_setting_get_uint_list(const char *key)
{
	if (g_str_equal(key, CONF_AUTO_CONNECT_TECHS))
		return connman_settings.auto_connect;

	if (g_str_equal(key, CONF_ENABLED_TECHS))
		return connman_settings.enabled_techs;

	if (g_str_equal(key, CONF_FAVORITE_TECHS))
		return connman_settings.favorite_techs;

	if (g_str_equal(key, CONF_PREFERRED_TECHS))
		return connman_settings.preferred_techs;

	if (g_str_equal(key, CONF_ALWAYS_CONNECTED_TECHS))
		return connman_settings.always_connected_techs;

	return NULL;
}

unsigned int connman_timeout_input_request(void)
{
	return connman_settings.timeout_inputreq;
}

unsigned int connman_timeout_browser_launch(void)
{
	return connman_settings.timeout_browserlaunch;
}

int main(int argc, char *argv[])
{
	GOptionContext *context;
	GError *error = NULL;
	DBusConnection *conn;
	DBusError err;
	guint signal;

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, options, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		if (error) {
			g_printerr("%s\n", error->message);
			g_error_free(error);
		} else
			g_printerr("An unknown error occurred\n");
		exit(1);
	}

	g_option_context_free(context);

	if (option_version) {
		printf("%s\n", VERSION);
		exit(0);
	}

	if (option_detach) {
		if (daemon(0, 0)) {
			perror("Can't start daemon");
			exit(1);
		}
	}

	if (mkdir(STORAGEDIR, S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0) {
		if (errno != EEXIST)
			perror("Failed to create storage directory");
	}

	umask(0077);

	main_loop = g_main_loop_new(NULL, FALSE);

	signal = setup_signalfd();

	dbus_error_init(&err);

	conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, CONNMAN_SERVICE, &err);
	if (!conn) {
		if (dbus_error_is_set(&err)) {
			fprintf(stderr, "%s\n", err.message);
			dbus_error_free(&err);
		} else
			fprintf(stderr, "Can't register with system bus\n");
		exit(1);
	}

	g_dbus_set_disconnect_function(conn, disconnect_callback, NULL, NULL);

	__connman_log_init(argv[0], option_debug, option_detach,
			option_backtrace, "Connection Manager", VERSION);

	__connman_dbus_init(conn);

	if (!option_config)
		config_init(CONFIGMAINFILE);
	else
		config_init(option_config);

	__connman_util_init();
	__connman_inotify_init();
	__connman_technology_init();
	__connman_notifier_init();
	__connman_agent_init();
	__connman_service_init();
	__connman_peer_service_init();
	__connman_peer_init();
	__connman_provider_init();
	__connman_network_init();
	__connman_config_init();
	__connman_device_init(option_device, option_nodevice);

	__connman_ippool_init();
	__connman_firewall_init();
	__connman_nat_init();
	__connman_tethering_init();
	__connman_counter_init();
	__connman_manager_init();
	__connman_stats_init();
	__connman_clock_init();

	__connman_ipconfig_init();
	__connman_rtnl_init();
	__connman_task_init();
	__connman_proxy_init();
	__connman_detect_init();
	__connman_session_init();
	__connman_timeserver_init();
	__connman_gateway_init();

	__connman_plugin_init(option_plugin, option_noplugin);

	__connman_resolver_init(option_dnsproxy);
	__connman_rtnl_start();
	__connman_dhcp_init();
	__connman_dhcpv6_init();
	__connman_wpad_init();
	__connman_wispr_init();
	__connman_rfkill_init();
	__connman_machine_init();

	g_free(option_config);
	g_free(option_device);
	g_free(option_plugin);
	g_free(option_nodevice);
	g_free(option_noplugin);

	g_main_loop_run(main_loop);

	g_source_remove(signal);

	__connman_machine_cleanup();
	__connman_rfkill_cleanup();
	__connman_wispr_cleanup();
	__connman_wpad_cleanup();
	__connman_dhcpv6_cleanup();
	__connman_session_cleanup();
	__connman_plugin_cleanup();
	__connman_provider_cleanup();
	__connman_gateway_cleanup();
	__connman_timeserver_cleanup();
	__connman_detect_cleanup();
	__connman_proxy_cleanup();
	__connman_task_cleanup();
	__connman_rtnl_cleanup();
	__connman_resolver_cleanup();

	__connman_clock_cleanup();
	__connman_stats_cleanup();
	__connman_config_cleanup();
	__connman_manager_cleanup();
	__connman_counter_cleanup();
	__connman_tethering_cleanup();
	__connman_nat_cleanup();
	__connman_firewall_cleanup();
	__connman_peer_service_cleanup();
	__connman_peer_cleanup();
	__connman_ippool_cleanup();
	__connman_device_cleanup();
	__connman_network_cleanup();
	__connman_dhcp_cleanup();
	__connman_service_cleanup();
	__connman_agent_cleanup();
	__connman_ipconfig_cleanup();
	__connman_notifier_cleanup();
	__connman_technology_cleanup();
	__connman_inotify_cleanup();

	__connman_util_cleanup();
	__connman_dbus_cleanup();

	__connman_log_cleanup(option_backtrace);

	dbus_connection_unref(conn);

	g_main_loop_unref(main_loop);

	if (connman_settings.pref_timeservers)
		g_strfreev(connman_settings.pref_timeservers);

	g_free(connman_settings.auto_connect);
	g_free(connman_settings.favorite_techs);
	g_free(connman_settings.preferred_techs);
	g_strfreev(connman_settings.fallback_nameservers);
	g_strfreev(connman_settings.blacklisted_interfaces);
	g_strfreev(connman_settings.tethering_technologies);
	g_free(connman_settings.vendor_class_id);
	g_free(connman_settings.online_check_ipv4_url);
	g_free(connman_settings.online_check_ipv6_url);
	g_free(connman_settings.localtime);

	g_free(option_debug);
	g_free(option_wifi);

	return 0;
}
