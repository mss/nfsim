/*

Copyright (c) 2003,2004 Jeremy Kerr, Rusty Russell and Harald Welte

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
#include <log.h>
#include <ipv4/ipv4.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "utils.h"
#include "message.h"

static bool run_command(int argc, char **argv)
{
	int status = -1;
	char *prefix, *name, *output;
	char buf[4096] = { 0 };

	prefix = getenv("NFSIM_IPTABLES_PREFIX");
	if (prefix)
		name = talloc_asprintf(NULL, "%s/%s", prefix, argv[0]);
	else
		name = talloc_strdup(NULL, argv[0]);

	start_program(name, argc, argv);

	while ((output = wait_for_output(&status)) != NULL) {
		/* Take output from iptables, and feed through logging. */
		nfsim_log_partial(LOG_USERSPACE, buf, sizeof(buf), "%s",
				  output);
		talloc_free(output);
	}
	talloc_free(name);

	/* If parent died without returning status, this can happen. */
	if (status == -1)
		return false;

	return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void run_command_help(int argc, char **argv)
{
#include "iptables-help:iptables"
/*** XML Help:
    <section id="c:iptables">
     <title><command>iptables</command></title>
     <para>Run the iptables command on the simulator</para>
     <cmdsynopsis>
      <command>iptables</command>
      <arg choice="opt"><replaceable>options</replaceable></arg>
     </cmdsynopsis>

     <para>The external <command>iptables</command> binary will be
     invoked, with its operations redirected to the simulator.  If the
     <varname>NFSIM_IPTABLES_PREFIX</varname> environment variable is set,
     the command in that directory will be executed (useful for
     testing specific variants).  Otherwise, the current
     <varname>PATH</varname> is searched, then
     <filename>/usr/local/sbin</filename>, <filename>/sbin</filename>,
     <filename>/usr/sbin</filename>.</para>

     <para>You do not need to be root to use
     <command>iptables</command> in this way.  If the
     <command>iptables</command> command fails, that will be reported
     as <screen><![CDATA[iptables: command failed]]></screen>; with
     the <arg>-e</arg> to <command>nfsim</command>,
     <command>nfsim</command> will abort.
     </para>
    </section>
*/
}

static void init(void)
{
	char *path = getenv("PATH");

	/* Some people don't have sbin etc. in their path: append. */
	path = talloc_asprintf(NULL, "%s:/usr/local/sbin:/sbin:/usr/sbin",
			       getenv("PATH"));
	setenv("PATH", path, 1);
	talloc_free(path);

	tui_register_command("iptables", run_command, run_command_help);
	tui_register_command("iptables-save", run_command, run_command_help);
	tui_register_command("iptables-restore", run_command,
			     run_command_help);

#ifdef HAVE_IPV6
	tui_register_command("ip6tables", run_command, run_command_help);
	tui_register_command("ip6tables-save", run_command, run_command_help);
	tui_register_command("ip6tables-restore", run_command, run_command_help);
#endif
}

init_call(init);

