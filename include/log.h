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

#ifndef __CONNMAN_LOG_H
#define __CONNMAN_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SECTION:log
 * @title: Logging premitives
 * @short_description: Functions for logging error and debug information
 */

void connman_info(const char *format, ...)
				__attribute__((format(printf, 1, 2)));
void connman_warn(const char *format, ...)
				__attribute__((format(printf, 1, 2)));
void connman_error(const char *format, ...)
				__attribute__((format(printf, 1, 2)));
void connman_debug(const char *format, ...)
				__attribute__((format(printf, 1, 2)));

#define connman_warn_once(fmt, arg...) do {		\
	static bool printed;				\
	if (!printed) {					\
		connman_warn(fmt, ## arg);		\
		printed = true;				\
	}						\
} while (0)

struct connman_debug_desc {
	const char *name;
	const char *file;
#define CONNMAN_DEBUG_FLAG_DEFAULT (0)
#define CONNMAN_DEBUG_FLAG_PRINT   (1 << 0)
#define CONNMAN_DEBUG_FLAG_ALIAS   (1 << 1)
	unsigned int flags;
} __attribute__((aligned(8)));

#define CONNMAN_DEBUG_DESC_INSTANTIATE(symbol, _name, _file, _flags) \
	static struct connman_debug_desc symbol \
	__attribute__((used, section("__debug"), aligned(8))) = { \
		.name = _name, .file = _file, .flags = _flags \
	}

#define CONNMAN_DEBUG_ALIAS(suffix) \
	CONNMAN_DEBUG_DESC_INSTANTIATE(__debug_alias_##suffix, \
		#suffix, \
		__FILE__, \
		CONNMAN_DEBUG_FLAG_ALIAS)

#define CONNMAN_DEBUG(function, fmt, args...) do { \
	CONNMAN_DEBUG_DESC_INSTANTIATE(__connman_debug_desc, \
		0, \
		__FILE__, \
		CONNMAN_DEBUG_FLAG_DEFAULT); \
		if (__connman_debug_desc.flags & CONNMAN_DEBUG_FLAG_PRINT) \
			connman_debug("%s:%s() " fmt, \
					__FILE__, function, ##args); \
	} while (0)

/**
 * DBG:
 * @fmt: format string
 * @arg...: list of arguments
 *
 * Simple macro around connman_debug() which also include the function
 * name it is called in.
 */
#define DBG(fmt, args...) CONNMAN_DEBUG(__func__, fmt, ##args)

#ifdef __cplusplus
}
#endif

#endif /* __CONNMAN_LOG_H */
