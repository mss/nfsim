/*

Copyright (c) 2004 Rusty Russell, IBM Corporation

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
#include <core.h>
#include <tui.h>
#include <utils.h>

bool strace;

static void strace_help(int argc, char **argv)
{
#include "strace-help:strace"
/*** XML Help:
    <section id="c:strace">
     <title><command>strace</command></title>
     <para>Report information about all system calls and user-kernel copies</para>
     <cmdsynopsis>
      <command>strace</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>strace</command>
      <arg choice="req">off</arg>
     </cmdsynopsis>

     <para><command>strace</command> will report all system calls from
     <command>iptables</command>, and all
     <command>copy_to_user</command> and
     <command>copy_from_user</command> results.  This is useful for
     debugging iptables, and checking that the kernel is returning the
     right error values.
     </para>

     <para>Tracing will continue until the <arg>off</arg> argument is
     given.
     </para>
     </section>
*/
}

static bool parse_strace(int argc, char *argv[])
{
	if (argc == 1) {
		if (strace) {
			nfsim_log(LOG_ALWAYS, "stracing already on!");
			return false;
		}
		strace = true;
		return true;
	}

	if (argc == 2 && streq(argv[1], "off")) {
		if (!strace) {
			nfsim_log(LOG_ALWAYS, "stracing already off!");
			return false;
		}
		strace = false;
		return true;
	}
	strace_help(argc, argv);
	return false;
}

static void init(void)
{
	tui_register_command("strace", parse_strace, strace_help);
}

init_call(init);
