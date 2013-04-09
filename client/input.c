/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012-2013  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <gdbus.h>
#include "input.h"
#include "commands.h"

static DBusConnection *connection;
static GMainLoop *main_loop;
static bool interactive = false;

static bool save_input;
static char *saved_line;
static int saved_point;

void __connmanctl_quit(void)
{
	if (main_loop != NULL)
		g_main_loop_quit(main_loop);
}

bool __connmanctl_is_interactive(void)
{
	return interactive;
}

void __connmanctl_save_rl(void)
{
	if (interactive == false)
		return;

	save_input = !RL_ISSTATE(RL_STATE_DONE);

	if (save_input) {
		saved_point = rl_point;
		saved_line = rl_copy_text(0, rl_end);
		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	}
}

void __connmanctl_redraw_rl(void)
{
	if (interactive == false)
		return;

	if (save_input) {
		rl_restore_prompt();
		rl_replace_line(saved_line, 0);
		rl_point = saved_point;
		rl_redisplay();
		free(saved_line);
	}

	save_input = 0;
}

static void rl_handler(char *input)
{
	char **args;
	int num, err;

	if (input == NULL) {
		rl_newline(1, '\n');
		g_main_loop_quit(main_loop);
		return;
	}
	if (*input != '\0')
		add_history(input);

	args = g_strsplit(input, " ", 0);
	num = g_strv_length(args);

	err = commands(connection, args, num);

	g_strfreev(args);

	if (err > 0)
		g_main_loop_quit(main_loop);
}

static gboolean input_handler(GIOChannel *channel, GIOCondition condition,
		gpointer user_data)
{
	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_main_loop_quit(main_loop);
		return FALSE;
	}

	rl_callback_read_char();
	return TRUE;
}

int __connmanctl_input_init(int argc, char *argv[])
{
	char *help[] = {
		"help",
		NULL
	};
	guint source = 0;
	int err;
	DBusError dbus_err;

	dbus_error_init(&dbus_err);
	connection = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, &dbus_err);

	if (dbus_error_is_set(&dbus_err)) {
		fprintf(stderr, "Error: %s\n", dbus_err.message);
		dbus_error_free(&dbus_err);
		return 1;
	}

	if (argc < 2) {
		GIOChannel *channel;

		interactive = true;

		channel = g_io_channel_unix_new(fileno(stdin));
		source = g_io_add_watch(channel,
				G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
				input_handler, NULL);
		g_io_channel_unref(channel);

		rl_callback_handler_install("connmanctl> ", rl_handler);
		err = -EINPROGRESS;

	} else {
		interactive = false;

		if (strcmp(argv[1], "--help") == 0 ||
				strcmp(argv[1], "-h") == 0)
			err = commands(connection, help, 1);
		else
			err = commands(connection, argv + 1, argc -1);
	}

	if (err == -EINPROGRESS) {
		main_loop = g_main_loop_new(NULL, FALSE);
		g_main_loop_run(main_loop);

		if (source > 0)
			g_source_remove(source);

		err = 0;
	}

	if (interactive == true) {
		rl_callback_handler_remove();
		rl_message("");
	}

	dbus_connection_unref(connection);
	if (main_loop != NULL)
		g_main_loop_unref(main_loop);

	if (err < 0)
		err = -err;
	else
		err = 0;

	return err;
}