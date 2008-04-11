/*

Copyright (c) 2003,2004 Jeremy Kerr & Rusty Russell

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
#include <kernelenv.h>
#include <utils.h>

extern struct list_head __timers;

static int find_last_timer(void)
{
       if (list_empty(&__timers))
               return 0;

       return list_entry(__timers.prev, struct timer_list, entry)->expires;
}

static bool time_cmd(int argc, char **argv)
{
	if (argc == 1) {
		nfsim_log(LOG_ALWAYS, "Current time: %d", jiffies/HZ);
		return true;
	}

	if (argc == 2) {
		if (streq(argv[1], "+infinity")) {
			increment_time(find_last_timer() - jiffies);
		} else if (*argv[1] == '+') {
			increment_time(atoi(argv[1]+1) * HZ);
		} else if (*argv[1] == '-') {
			nfsim_log(LOG_ALWAYS, "Backwards time travel not "
				"implemented");
			return false;
		} else {
			if (atoi(argv[1]) == 0) {
				nfsim_log(LOG_ALWAYS,
					  "Bad time argument %s", argv[1]);
				return false;
			}
			if (atoi(argv[1]) * HZ < jiffies) {
				nfsim_log(LOG_ALWAYS,
					  "Flares alert: Backwards time travel not "
					  "implemented");
				return false;
			}
			increment_time(atoi(argv[1]) * HZ - jiffies);
		}
		return true;
	}

	return false;

}

static void time_help(int argc, char **argv)
{
#include "time-help:time"
/*** XML Help:
    <section id="c:time">
     <title><command>time</command></title>
     <para>Query or advance the kernel time</para>
     <cmdsynopsis>
      <command>time</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>time</command>
      <arg choice="req"><replaceable>time</replaceable></arg>
      <arg choice="req">+<replaceable>advance</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>time</command>
      <arg choice="req">+infinity</arg>
     </cmdsynopsis>
     <para><command>time</command> with no arguments will show the current
      kernel time (in seconds).</para>
     <para><command>time <replaceable>time</replaceable></command> will set the
      kernel time to <replaceable>time</replaceable> - which must be greater
      than the current time value. <command>time
      +<replaceable>advance</replaceable></command> will increment the time by
      the value of advance, which must be positive.</para>
     <para>When the kernel time is incremented, kernel timers may be
      triggered. If so, the simulator will show logging messages:
<screen><![CDATA[> time +30
running timer to ip_conntrack_core.c:init_conntrack()
>]]></screen></para>
     <para>
	The <command>time
	+<replaceable>infinity</replaceable></command> command will
	forward time past all the currently pending timers, leaving a
	clean slate.
	</para>

     </section>
*/
}


static void init(void)
{
	tui_register_command("time", time_cmd, time_help);
}

init_call(init);

