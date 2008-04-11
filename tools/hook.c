/*

Copyright (c) 2003,2004 Rusty Russell

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

struct hook_list
{
	struct list_head list;
	struct nf_hook_ops ops;
};
static LIST_HEAD(hooks);

static unsigned int hook_accept(unsigned int hooknum,
				struct sk_buff **skb,
				const struct net_device *in,
				const struct net_device *out,
				int (*okfn)(struct sk_buff *))
{
	return NF_ACCEPT;
}

static unsigned int hook_drop(unsigned int hooknum,
			      struct sk_buff **skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{
	return NF_DROP;
}

static unsigned int hook_queue(unsigned int hooknum,
			       struct sk_buff **skb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	return NF_QUEUE;
}

static void print_hook(struct nf_hook_ops *ops)
{
	nfsim_log(LOG_ALWAYS, "%s %i %s",
	    ops->hook == hook_accept ? "ACCEPT"
	    : ops->hook == hook_drop ? "DROP"
	    : ops->hook == hook_queue ? "QUEUE" : "UNKNOWN",
	    ops->priority,
	    ops->pf != PF_INET ? "Non-IPv4"
	    : ops->hooknum == NF_IP_PRE_ROUTING ? "IP_PRE_ROUTING"
	    : ops->hooknum == NF_IP_POST_ROUTING ? "IP_PRE_ROUTING"
	    : ops->hooknum == NF_IP_FORWARD ? "IP_FORWARD"
	    : ops->hooknum == NF_IP_LOCAL_IN ? "IP_LOCAL_IN"
	    : ops->hooknum == NF_IP_LOCAL_OUT ? "IP_LOCAL_OUT"
	    : "UNKNOWN");
}

static bool hook_post_cleanup(const char *command)
{
	struct hook_list *l, *next;

	/* A little hacky, but it works: only clean up after gen_ip. */
	if (!streq(command, "gen_ip"))
		return true;

	list_for_each_entry_safe(l, next, &hooks, list) {
		nf_unregister_hook(&l->ops);
		list_del(&l->list);
		free(l);
	}
	return true;
}

static bool hook_cmd(int argc, char **argv)
{
	struct hook_list *l;

	if (argc == 1) {
		list_for_each_entry(l, &hooks, list)
			print_hook(&l->ops);
		return true;
	}

	if (argc != 4) {
		nfsim_log(LOG_ALWAYS, "Bad hook command");
		return false;
	}

	l = talloc(NULL, struct hook_list);
	l->ops.owner = THIS_MODULE;
	l->ops.priority = atoi(argv[2]);

	if (streq(argv[1], "IP_PRE_ROUTING")) {
		l->ops.pf = PF_INET;
		l->ops.hooknum = NF_IP_PRE_ROUTING;
	} else if (streq(argv[1], "IP_POST_ROUTING")) {
		l->ops.pf = PF_INET;
		l->ops.hooknum = NF_IP_POST_ROUTING;
	} else if (streq(argv[1], "IP_LOCAL_IN")) {
		l->ops.pf = PF_INET;
		l->ops.hooknum = NF_IP_LOCAL_IN;
	} else if (streq(argv[1], "IP_LOCAL_OUT")) {
		l->ops.pf = PF_INET;
		l->ops.hooknum = NF_IP_LOCAL_OUT;
	} else if (streq(argv[1], "IP_FORWARD")) {
		l->ops.pf = PF_INET;
		l->ops.hooknum = NF_IP_FORWARD;
	} else {
		nfsim_log(LOG_ALWAYS, "Unknown hook %s", argv[1]);
		talloc_free(l);
		return false;
	}

	if (streq(argv[3], "DROP"))
		l->ops.hook = hook_drop;
	else if (streq(argv[3], "ACCEPT"))
		l->ops.hook = hook_accept;
	else if (streq(argv[3], "QUEUE"))
		l->ops.hook = hook_queue;
	else {
		nfsim_log(LOG_ALWAYS, "Unknown verdict %s", argv[3]);
		talloc_free(l);
		return false;
	}

	list_add(&l->list, &hooks);
	__nf_register_hook(&l->ops);
	return true;
}

static void hook_help(int argc, char **argv)
{
#include "hook-help:hook"
/*** XML Help:
    <section id="c:hook">
     <title><command>hook</command></title>
     <para>Insert simple netfilter hooks</para>
     <cmdsynopsis>
      <command>hook</command>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>hook</command>
      <arg choice="req"><replaceable>hookname</replaceable></arg>
      <arg choice="req"><replaceable>priority</replaceable></arg>
      <arg choice="req"><replaceable>verdict</replaceable></arg>
     </cmdsynopsis>
     <para>The hook command allows new hooks to be inserted. These hooks will
      simply return the specified <replaceable>verdict</replaceable> for all
      packets.</para>

     <para>All hooks are cleared after the next
     <command>gen_ip</command> command.</para>

     <para>The <replaceable>hookname</replaceable> argument specifies which list
      to insert the hook into. One of
      <simplelist type="inline">
       <member>IP_PRE_ROUTING</member>
       <member>IP_INPUT</member>
       <member>IP_FORWARD</member>
       <member>IP_OUTPUT</member>
       <member>IP_POST_ROUTING</member>
      </simplelist>
     </para>
     <para>The <replaceable>priority</replaceable> argument specifies the
      priority of the hook, from <varname>INT_MIN</varname> to
      <varname>INT_MAX</varname>.</para>
     <para>The <replaceable>verdict</replaceable> argument specifies the
      verdict to return from the hook itself:
      <simplelist type="inline">
       <member>ACCEPT</member>
       <member>DROP</member>
       <member>QUEUE</member>
       <member>STOLEN</member>
       <member>REPEAT</member>
      </simplelist>
     </para>
    </section>
*/
}

static void init(void)
{
	tui_register_command("hook", hook_cmd, hook_help);
	tui_register_pre_post_hook(NULL, hook_post_cleanup);
}

init_call(init);

