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

#include <core.h>
#include <tui.h>
#include <log.h>
#include <utils.h>
#include "gen_ip.h"

static void tcpsession_help(int argc, char **argv)
{
#include "tcpsession-help:tcpsession"
/*** XML Help:
    <section id="c:tcpsession">
     <title><command>tcpsession</command></title>
     <para>Generate TCP session</para>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">OPEN</arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">srcpt</arg>
      <arg choice="req">dstpt</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">OPEN</arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">srcpt</arg>
      <arg choice="req">dstpt</arg>
      <arg choice="req">reply-src</arg>
      <arg choice="req">reply-dest</arg>
      <arg choice="req">reply-srcpt</arg>
      <arg choice="req">reply-dstpt</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">DATA</arg>
      <arg choice="req">direction</arg>
      <arg choice="req"><replaceable>args</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">LENCHANGE</arg>
      <arg choice="req"><replaceable>number</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">CLOSE</arg>
      <arg choice="req">direction</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">RESET</arg>
      <arg choice="req">direction</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>tcpsession</command>
      <arg choice="req">ABANDON</arg>
     </cmdsynopsis>

     <para><command>tcpsession</command> is shorthand for creating a
     TCP session using multiple gen_ip commands.  The four-argument
     <arg>OPEN</arg> form creates a simple connection (no NAT
     expected), the eight-argument form creates a connection which
     might be NATted.  You can only have one open connection at a
     time: this is used for all operations.  Sequence numbers will be
     1001 for the sender, and 2001 for the recipient.  Initial packet
     comes in eth0, replies come in eth1. </para>

     <para>The <arg>DATA</arg> form sends data on the current
     connection, in a single packet (and sends an ACK in reply).
     <arg>direction</arg> is either 'original' or 'reply'.</para>

     <para>The <arg>LENCHANGE</arg> command sets expected change in
     length of the next packet.  It is used to tell
     <command>tcpsession</command> that the next packet is going to
     expected to be shortened or lengthened.</para>

     <para>The <arg>CLOSE</arg> form sends the required FIN and ACK
     packets to close the connection: <arg>direction</arg> specifies
     who initiates the close.</para>

     <para>The <arg>ABANDON</arg> argument causes the connection to
    simply be forgotten, so you can open a new one.</para> </section>
*/
}

struct tcp_endpoint
{
	char *interface;
	char *src, *dst, *spt, *dpt;
	u32 seq, ack;
};

struct tcpsession
{
	struct tcp_endpoint original, reply;
	int lenchange;
};
static struct tcpsession *curr = NULL;

static bool tcp_send(struct tcp_endpoint *in, struct tcp_endpoint *out,
		     int datalen, const char *flags, const char *win,
		     int datanum, char *data[])
{
	char *ctx = talloc(NULL, char);
	char *argv[1024];
	int argc;

	argc = 0;
	argv[argc++] = talloc_strdup(ctx, "expect");
	argv[argc++] = talloc_strdup(ctx, "gen_ip");
	argv[argc++] = talloc_asprintf(ctx, "send:%s", out->interface);
	argv[argc++] = talloc_asprintf(ctx, "{IPv4 %s %s %i 6 %s %s %s"
				       " SEQ=%u ACK=%u*}",
				       out->dst, out->src,
				       datalen + curr->lenchange,
				       out->dpt, out->spt, flags,
				       out->ack, out->seq);
	argv[argc] = NULL;

	/* Don't have it abort, do that at top level. */
	if (!tui_do_command(argc, argv, false))
		goto fail;

	argc = 0;
	argv[argc++] = talloc_strdup(ctx, "gen_ip");
	argv[argc++] = talloc_asprintf(ctx, "IF=%s", in->interface);
	argv[argc++] = talloc_strdup(ctx, in->src);
	argv[argc++] = talloc_strdup(ctx, in->dst);
	argv[argc++] = talloc_asprintf(ctx, "%i", datalen);
	argv[argc++] = talloc_strdup(ctx, "6");
	argv[argc++] = talloc_strdup(ctx, in->spt);
	argv[argc++] = talloc_strdup(ctx, in->dpt);
	argv[argc++] = talloc_strdup(ctx, flags);
	argv[argc++] = talloc_asprintf(ctx, "SEQ=%u", in->seq);
	argv[argc++] = talloc_asprintf(ctx, "ACK=%u", in->ack);
	if (win)
		argv[argc++] = talloc_strdup(ctx, win);
	if (datanum) {
		unsigned int i;
		argv[argc++] = talloc_strdup(ctx, "DATA");
		for (i = 0; i < datanum; i++)
			argv[argc++] = talloc_strdup(ctx, data[i]);
	}
	argv[argc] = NULL;

	if (!tui_do_command(argc, argv, false))
		goto fail;

	talloc_free(ctx);
	return true;
fail:
	talloc_free(ctx);
	return false;
}

static bool open_session(const char *src, const char *dst,
			 const char *spt, const char *dpt,
			 const char *reply_src, const char *reply_dst,
			 const char *reply_spt, const char *reply_dpt)
{
	curr = talloc(NULL, struct tcpsession);

	curr->original.src = talloc_strdup(curr, src);
	curr->original.dst = talloc_strdup(curr, dst);
	curr->original.spt = talloc_strdup(curr, spt);
	curr->original.dpt = talloc_strdup(curr, dpt);
	curr->original.interface = talloc_strdup(curr, "eth0");
	curr->original.seq = 1000;
	curr->original.ack = 2000;

	curr->reply.src = talloc_strdup(curr, reply_src);
	curr->reply.dst = talloc_strdup(curr, reply_dst);
	curr->reply.spt = talloc_strdup(curr, reply_spt);
	curr->reply.dpt = talloc_strdup(curr, reply_dpt);
	curr->reply.interface = talloc_strdup(curr, "eth1");
	curr->reply.seq = 2000;
	curr->reply.ack = 1000;

	curr->lenchange = 0;

	if (!tcp_send(&curr->original, &curr->reply, 0, "SYN", "WIN=512",
		      0, NULL))
		goto fail;
	curr->original.seq++;
	curr->reply.ack++;
	if (!tcp_send(&curr->reply, &curr->original, 0, "SYN/ACK", "WIN=512",
		      0, NULL))
		goto fail;
	curr->reply.seq++;
	curr->original.ack++;
	if (!tcp_send(&curr->original, &curr->reply, 0, "ACK", "WIN=512",
		      0, NULL))
		goto fail;
	return true;
fail:
	talloc_free(curr);
	curr = NULL;
	return false;
}

static bool send_data(struct tcp_endpoint *in, struct tcp_endpoint *out,
		      int datanum, char *data[])
{
	int len = gen_ip_data_length(datanum, data);

	if (!tcp_send(in, out, len, "ACK", NULL,
		      datanum, data))
		return false;
	in->seq += len;
	out->ack += len + curr->lenchange;
	curr->lenchange = 0;

	/* Send ACK. */
	return tcp_send(out, in, 0, "ACK", "WIN=512", 0, NULL);
}

static bool close_session(struct tcp_endpoint *in, struct tcp_endpoint *out)
{
	if (!tcp_send(in, out, 0, "FIN/ACK", NULL, 0, NULL))
		return false;
	in->seq++;
	out->ack++;
	if (!tcp_send(out, in, 0, "FIN/ACK", NULL, 0, NULL))
		return false;
	out->seq++;
	in->ack++;
	if (!tcp_send(in, out, 0, "ACK", NULL, 0, NULL))
		return false;
	talloc_free(curr);
	curr = NULL;
	return true;
}

static bool reset_session(struct tcp_endpoint *in, struct tcp_endpoint *out)
{
	if (!tcp_send(in, out, 0, "RST", NULL, 0, NULL))
		return false;
	talloc_free(curr);
	curr = NULL;
	return true;
}

static bool tcpsession(int argc, char *argv[])
{
	if (argc < 2) {
		tcpsession_help(argc, argv);
		return false;
	}
	if (streq(argv[1], "OPEN")) {
		if (curr) {
			nfsim_log(LOG_ALWAYS, "Session already open!");
			return false;
		}
		if (argc == 6)
			return open_session(argv[2],argv[3],argv[4],argv[5],
					    argv[3],argv[2],argv[5],argv[4]);
		if (argc == 10)
			return open_session(argv[2],argv[3],argv[4],argv[5],
					    argv[6],argv[7],argv[8],argv[9]);
		tcpsession_help(argc, argv);
		return false;
	}
	if (streq(argv[1], "DATA")) {
		if (!curr) {
			nfsim_log(LOG_ALWAYS, "Session not open!");
			return false;
		}
		if (argc < 3) {
			tcpsession_help(argc, argv);
			return false;
		}
		if (streq(argv[2], "original"))
			return send_data(&curr->original, &curr->reply,
					 argc-3, argv+3);
		if (streq(argv[2], "reply"))
			return send_data(&curr->reply, &curr->original,
					 argc-3, argv+3);
		tcpsession_help(argc, argv);
		return false;
	}
	if (streq(argv[1], "LENCHANGE")) {
		int change;
		if (argc != 3) {
			tcpsession_help(argc, argv);
			return false;
		}
		change = atoi(argv[2]);
		if (!change) {
			tcpsession_help(argc, argv);
			return false;
		}
		curr->lenchange = change;
		return true;
	}
	if (streq(argv[1], "CLOSE")) {
		if (!curr) {
			nfsim_log(LOG_ALWAYS, "Session not open!");
			return false;
		}
		if (argc != 3) {
			tcpsession_help(argc, argv);
			return false;
		}
		if (streq(argv[2], "original"))
			return close_session(&curr->original, &curr->reply);
		if (streq(argv[2], "reply"))
			return close_session(&curr->reply, &curr->original);
	}
	if (streq(argv[1], "RESET")) {
		if (!curr) {
			nfsim_log(LOG_ALWAYS, "Session not open!");
			return false;
		}
		if (argc != 3) {
			tcpsession_help(argc, argv);
			return false;
		}
		if (streq(argv[2], "original"))
			return reset_session(&curr->original, &curr->reply);
		if (streq(argv[2], "reply"))
			return reset_session(&curr->reply, &curr->original);
	}
	if (streq(argv[1], "ABANDON")) {
		if (!curr) {
			nfsim_log(LOG_ALWAYS, "Session not open!");
			return false;
		}
		if (argc != 2) {
			tcpsession_help(argc, argv);
			return false;
		}
		talloc_free(curr);
		curr = NULL;
		return true;
	}
	tcpsession_help(argc, argv);
	return false;
}

static void init(void)
{
	tui_register_command("tcpsession", tcpsession, tcpsession_help);
}

init_call(init);

