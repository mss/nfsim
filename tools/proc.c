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
#include <core.h>
#include <utils.h>

#include <sys/types.h>
#include <unistd.h>

static int proc_tree(struct proc_dir_entry *dir, int i) {
	char indent[80];
	struct proc_dir_entry *child;

	assert(i < 80);

	memset(indent, ' ', 80);
	*(indent + i) = '\0';

	nfsim_log(LOG_ALWAYS, "%s-%s", indent, *dir->name == '/' ? dir->name + 1
	                                                     : dir->name);

	for (child = dir->subdir; child; child = child->next) {
		proc_tree(child, i+2);
	}

	return 0;
}

static void proc_help(int argc, char **argv)
{
#include "proc-help:proc"
/*** XML Help:
    <section id="c:proc">
     <title><command>proc</command></title>
     <para>Displays and set <filename>/proc</filename> filesystem information</para>
     <cmdsynopsis>
      <command>proc</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>proc</command>
      <arg choice="req">cat</arg>
      <arg choice="req"><replaceable>file</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>proc</command>
      <arg choice="req">write</arg>
      <arg choice="req"><replaceable>file</replaceable></arg>
      <arg choice="req"><replaceable>args...</replaceable></arg>
     </cmdsynopsis>
     <para><command>proc</command> with no arguments will print the heirachy of
      the currently-registered proc files.</para>
     <para><command>proc cat <replaceable>file</replaceable></command> will
      display the contents of the proc file specified.</para>
     <para><command>proc write <replaceable>file</replaceable> <replaceable>args..</replaceable> </command> will write args to the proc file.</para>
    </section>
*/
}

static bool proc(int argc, char **argv)
{

	if (argc == 1) {
		proc_tree(&proc_root, 0);
		return true;
	}

	if (streq(argv[1], "cat")) {
		if (argc != 3) {
			proc_help(argc, argv);
			return false;
		}
		return nfsim_proc_cat(argv[2]);
	} else if (streq(argv[1], "write")) {
		if (argc < 4) {
			proc_help(argc, argv);
			return false;
		}
		return nfsim_proc_write(argv[2], argv+3);
	}

	return true;
}

static void init(void)
{
	tui_register_command("proc", proc, proc_help);
}

init_call(init);

