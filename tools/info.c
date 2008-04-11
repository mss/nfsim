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
extern struct list_head nf_sockopts;

static bool hooks(int argc, char **argv)
{
	int i, j;
	struct nf_hook_ops *hook;

	for (i = 0; i < NPROTO; i++)
		for (j = 0; j < NF_MAX_HOOKS; j++)
			list_for_each_entry(hook, &nf_hooks[i][j], list)
				nfsim_log(LOG_ALWAYS, "hook(%d, %-18s) to %s",
					i, nf_hooknames[i][j],
					hook->owner ?
						hook->owner->name : "nfsim");

	return true;
}

static bool sockopts(int argc, char **argv)
{
	struct nf_sockopt_ops *sockopt;

	list_for_each_entry(sockopt, &nf_sockopts, list)
		nfsim_log(LOG_UI, "sockopt(pf=%d): get (%2d-%2d), "
		            "set (%2d-%2d) handled by %s",
		      sockopt->pf,
		      sockopt->get_optmin, sockopt->get_optmax,
		      sockopt->set_optmin, sockopt->set_optmax, sockopt->owner->name);

	return true;
}

static bool rcaches(int argc, char **argv)
{
	struct rtable *r;

	for (r = rcache; r; r = r->u.rt_next)
		nfsim_log(LOG_ALWAYS,
			"dst: %u.%u.%u.%u, src: %u.%u.%u.%u, "
			"iif: %d, gw: %u.%u.%u.%u",
			NIPQUAD(r->rt_dst), NIPQUAD(r->rt_src),
			r->rt_iif, NIPQUAD(r->rt_gateway));
	return true;
}

static bool timers(int argc, char **argv)
{
	struct timer_list *t;

	list_for_each_entry(t, &__timers, entry)
		nfsim_log(LOG_ALWAYS, "At %9d: %s:%s()", t->expires,
			t->owner->name, t->ownerfunction);

	return true;

}


static void info_help(int argc, char **argv)
{
#include "info-help:info"
/*** XML Help:
    <section id="c:info">
     <title><command>info</command></title>
     <para>Displays information about the kernel simulator.</para>
     <cmdsynopsis>
      <command>info</command>
      <arg choice="req">subject</arg>
     </cmdsynopsis>
     <para>The <replaceable>subject</replaceable> argument to
      <command>info</command> must be one of the following:</para>
     <variablelist>
      <varlistentry>
       <term>hooks</term>
       <listitem>
        <para>displays the currently registered netfilter hooks</para>
       </listitem>
      </varlistentry>
      <varlistentry>
       <term>sockopts</term>
       <listitem>
        <para>displays the currently registered netfilter sockopts</para>
       </listitem>
      </varlistentry>
      <varlistentry>
       <term>timers</term>
       <listitem>
        <para>shows the current kernel timers - when each is set to expire, as
	 well as the function where the timer was registered.</para>
       </listitem>
      </varlistentry>
      <varlistentry>
       <term>rcache</term>
       <listitem>
        <para>the contents of the kernel routing cache</para>
       </listitem>
      </varlistentry>
      <varlistentry>
       <term>failpoints</term>
       <listitem>
        <para>show how many failure points have been passed: with --failtest,
	 each of these alternatives would be attempted.</para>
       </listitem>
      </varlistentry>
     </variablelist>

    </section>
*/
}

static bool print_failpoints(int argc, char **argv)
{
	nfsim_log(LOG_ALWAYS, "failpoints: %u", failpoints);
	return true;
}

static bool info(int argc, char **argv)
{
	if (argc == 1) {
		info_help(argc, argv);
		return false;
	}

	argc--;
	argv++;

	if (streq(argv[0], "hooks"))
		return hooks(argc, argv);
	else if (streq(argv[0], "sockopts"))
		return sockopts(argc, argv);
	else if (streq(argv[0], "rcache"))
		return rcaches(argc, argv);
	else if (streq(argv[0], "timers"))
		return timers(argc, argv);
	else if (streq(argv[0], "failpoints"))
		return print_failpoints(argc, argv);


	nfsim_log(LOG_ALWAYS, "Unknown info command: %s", argv[0]);
	return false;
}

static void init(void)
{
	tui_register_command("info", info, NULL);
}

init_call(init);

