/*

Copyright (c) 2004 Rusty Russell

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

#include <linux/version.h>
#include <config.h>
#include <log.h>
#include <tui.h>
#include <kernelenv.h>
#include <utils.h>

static void version_help(int argc, char **argv)
{
#include "version-help:version"
/*** XML Help:
    <section id="c:version">
     <title><command>version</command></title>
     <para>Displays version information about the kernel simulator.</para>
     <cmdsynopsis>
      <command>version</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>version kernel</command>
     </cmdsynopsis>
     <para>The <command>version</command> displays information about
     the nfsim version and the version of the kernel it's simulating.
     With the <arg>kernel</arg> argument, it only displays the version
     of the kernel (useful for testsuites).
     </para>
    </section>
*/
}

static bool version(int argc, char **argv)
{
	if (argc == 2 && streq(argv[1], "kernel")) {
		nfsim_log(LOG_ALWAYS, "%s", UTS_RELEASE);
		return true;
	} else if (argc != 1) {
		version_help(argc, argv);
		return false;
	}
	nfsim_log(LOG_UI, "nfsim version %s", VERSION);
	nfsim_log(LOG_UI, "kernel version %s", UTS_RELEASE);
	return true;
}

static void init(void)
{
	tui_register_command("version", version, NULL);
}

init_call(init);

