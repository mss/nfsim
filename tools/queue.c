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

static bool queue(int argc, char **argv)
{
	struct nfsim_queueitem *i;

	if (argc == 1) {
		list_for_each_entry(i, &nfsim_queue, list)
			nfsim_log(LOG_ALWAYS, "%d:%s", i->id,
				describe_packet(i->skb));
		return 0;
	}

	if ((argc <= 3)) {
		int packetno = 0;
		int verdict;

		if ((verdict = nf_retval_by_name(argv[1])) < 0) {
			nfsim_log(LOG_ALWAYS, "Invalid verdict '%s'", argv[1]);
			return false;
		}

		if (argc == 3)
			packetno = atoi(argv[2]);
		else {
			i = list_entry(nfsim_queue.next,
				struct nfsim_queueitem, list);
			goto inject;
		}

		list_for_each_entry(i, &nfsim_queue, list)
			if (i->id == packetno)
				goto inject;

		nfsim_log(LOG_ALWAYS, "No packet found in the queue matching "
			"number '%d'", packetno);
		return false;
inject:
		nf_reinject(i->skb, i->info, verdict);
		list_del(&i->list);
		return true;
	}

	return false;
}

static void queue_help(int argc, char **argv)
{
#include "queue-help:queue"
/*** XML Help:
    <section id="c:queue">
     <title><command>queue</command></title>
     <para>Manage the userspace packet queue</para>
     <cmdsynopsis>
      <command>queue</command>
      <sbr/>
      <command>queue</command>
      <arg choice="req"><replaceable>verdict</replaceable></arg>
      <arg choice="opt"><replaceable>id</replaceable></arg>
     </cmdsynopsis>
     <para>When a netfitler hook returns <constant>NF_QUEUE</constant>, it is
      stored within the simulator's own packet queue. When a packet enters this
      queue, a logging message is printed:
      <screen><![CDATA[> gen_ip IF=eth0 192.168.0.2 192.168.0.1 50 udp 10 20
rcv:eth0
queue:added {IPv4 192.168.0.2 192.168.0.1 50 17 10 20}]]></screen>
      The packet is assigned a unique id number (monotonically increasing over
      the life of the simulator session), which can be used to reinject packets
      in any order</para>
     <para>The contents of the packet queue can be printed with the
     <command>queue</command> command with no arguments. Each packet in the
     queue is prefixed with its id:
      <screen><![CDATA[> queue
0: {IPv4 192.168.0.2 192.168.0.1 50 17 10 20}
1: {IPv4 192.168.1.10 192.168.0.1 50 6 1025 80 SYN}]]></screen></para>
     <para>To reinject a packet from the queue, specify a verdict (one of the
      netfilter hook return values, for example <constant>NF_ACCEPT</constant>),
      and optionally, the id of the packet. If no id is specified, the packet
      at the front of the queue is reinjected.</para>
    </section>
*/
}

static void init(void)
{
	tui_register_command("queue", queue, queue_help);
}

init_call(init);

