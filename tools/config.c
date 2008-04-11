/*

Copyright (c) 2005 Rusty Russell, IBM Corporation

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

/* Simple command to insert a given hook with given verdict (one shot). */
#include <tui.h>
#include <log.h>
#include <kernelenv.h>
#include <linux/netfilter_ipv4.h>
#include <utils.h>
#include "generated_config_string.c"

static bool config_cmd(int argc, char **argv)
{
	if (argc != 1) {
		nfsim_log(LOG_ALWAYS, "Config: I HAVE NO OPTIONS!");
		return false;
	}

	nfsim_log(LOG_ALWAYS, "%s", config_string);
	return true;
}

static void config_help(int argc, char **argv)
{
#include "config-help:config"
/*** XML Help:
    <section id="c:config">
     <title><command>config</command></title>
     <para>Print out the kernel configuration used to build nfsim</para>
     <cmdsynopsis>
      <command>config</command>
     </cmdsynopsis>

     <para>This command is mainly used for scripts to determine what
     modules and/or features are available.</para>

    </section>
*/
}

static void init(void)
{
	tui_register_command("config", config_cmd, config_help);
}

init_call(init);

