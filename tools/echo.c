/*

Copyright (c) 2005 Max Kellermann <max@duempel.org>

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <tui.h>
#include <core.h>
#include <utils.h>

#include <sys/types.h>
#include <unistd.h>

static void echo_help(int argc, char **argv)
{
#include "echo-help:echo"
/*** XML Help:
    <section id="c:echo">
     <title><command>echo</command></title>
     <para>Print a message</para>
     <cmdsynopsis>
      <command>echo</command>
      <arg choice="req"><replaceable>text...</replaceable></arg>
     </cmdsynopsis>
     <para><command>echo</command> prints a message.</para>
    </section>
*/
}

static bool echo(int argc, char **argv)
{
	char *buffer;
	int i;

	buffer = talloc_strdup(argv, "");
	for (i = 1; i < argc; i++)
		buffer = talloc_asprintf_append(buffer, "%s%s",
						i == 1 ? "" : " ", argv[i]);
	nfsim_log(LOG_ALWAYS, "%s", buffer);

	return true;
}

static void init(void)
{
	tui_register_command("echo", echo, echo_help);
}

init_call(init);
