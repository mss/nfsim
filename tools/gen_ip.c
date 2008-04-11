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

/* Simple program to generate and send ip packet.  */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>

#include <core.h>
#include <tui.h>
#include <log.h>
#include <utils.h>
#include <field.h>
#include "gen_ip.h"

struct protoent *getprotobyname(const char *name);
struct protoent
{
  char *p_name;                 /* Official protocol name.  */
  char **p_aliases;             /* Alias list.  */
  int p_proto;                  /* Protocol number.  */
};

/* A few hardcoded protocols for 'all' and in case the user has no
   /etc/protocols */
struct pprot {
	char *name;
	unsigned short num;
};

static const struct pprot chain_protos[] = {
	{ "all", 0 },
	{ "tcp", IPPROTO_TCP },
	{ "udp", IPPROTO_UDP },
	{ "icmp", IPPROTO_ICMP },
	{0}
};

unsigned int string_to_number(const char *s, unsigned int min, unsigned int max)
{
	unsigned int number;
	char *end;

	/* Handle hex, octal, etc. */
	number = (unsigned int)strtoul(s, &end, 0);
	if (*end == '\0' && end != s) {
		/* we parsed a number, let's see if we want this */
		if (min <= number && number <= max)
			return number;
		else
			return -1;
	} else
		return -1;
}

/* static u_int16_t*/
static int
parse_protocol(const char *s)
{
#if 0
	struct protoent *pent;
#endif
	const struct pprot *pp;

	int proto = string_to_number(s, 0, 65535);

	if (proto != -1) return (unsigned short)proto;

#if 0				/* Messes up valgrind for some reason? */
	pent = getprotobyname(s);
	if (pent != NULL)
		return pent->p_proto;
#endif
	for (pp = &chain_protos[0]; pp->name; pp++)
		if (!strcasecmp(s, pp->name))
			return pp->num;

	nfsim_log(LOG_UI, "gen_ip: unknown protocol `%s' specified", s);
	return -1;
}

struct in_addr *dotted_to_addr(const char *dotted)
{
	static struct in_addr addr;
	unsigned char *addrp;
	char *p, *q;
	int onebyte, i;
	char buf[20];

	/* copy dotted string, because we need to modify it */
	strncpy(buf, dotted, sizeof(buf) - 1);
	addrp = (unsigned char *) &(addr.s_addr);

	p = buf;
	for (i = 0; i < 3; i++) {
		if ((q = strchr(p, '.')) == NULL)
			return (struct in_addr *) NULL;
		else {
			*q = '\0';
			if ((onebyte = string_to_number(p, 0, 255)) == -1)
				return (struct in_addr *) NULL;
			else
				addrp[i] = (unsigned char) onebyte;
		}
		p = q + 1;
	}

	/* we've checked 3 bytes, now we check the last one */
	if ((onebyte = string_to_number(p, 0, 255)) == -1)
		return (struct in_addr *) NULL;
	else
		addrp[3] = (unsigned char) onebyte;

	return &addr;
}

static bool hexchar(char *dst, const char *str)
{
	int c;

	if (sscanf(str, "%02x", &c) != 1)
		return false;
	*dst = c;
	return true;
}

/* Collapse simple control characters. */
static char *
copy_printable(char *dst, const char *str)
{
	unsigned int i;

	for (i = 0; str[i]; i++, dst++) {
		if (str[i] == '\\') {
			i++;
			switch (str[i]) {
			case 'n': *dst = '\n'; break;
			case 'r': *dst = '\r'; break;
			case 't': *dst = '\t'; break;
			case '0': *dst = '\0'; break;
			case '\\': *dst = '\\'; break;
			case 'x':
				if (!hexchar(dst, str+i+1)) return NULL;
				i += 2;
				break;
			default: return NULL;
			}
		} else
			*dst = str[i];
	}
	return dst;
}

int gen_ip_data_length(unsigned int datanum, char *data[])
{
	char buf[1024], *p;
	unsigned int i, len = 0;

	for (i = 0; i < datanum; i++) {
		p = copy_printable(buf, data[i]);
		if (!p)
			return -1;
		len += p - buf;
		if (i > 0)
			len++;
	}
	return len;
}

static int parse_header(struct packet *packet,
			const char *srcip, const char *dstip,
			const char *lenstr, const char *protocol,
			int tos, int ttl)
{
	int proto, len;
	u_int32_t saddr, daddr;
	const struct in_addr *addr;

	len = string_to_number(lenstr, 0, 65535);
	if (len < 0) {
		nfsim_log(LOG_UI, "gen_ip: bad length `%s' specified", lenstr);
		return -1;
	}

	if ((proto = parse_protocol(protocol)) < 0)
		return -1;

	addr = dotted_to_addr(srcip);
	if (!addr) {
		nfsim_log(LOG_UI, "Bad src address `%s'", srcip);
		return -1;
	}
	saddr = addr->s_addr;

	addr = dotted_to_addr(dstip);
	if (!addr) {
		nfsim_log(LOG_UI, "Bad dst address `%s'", dstip);
		return -1;
	}
	daddr = addr->s_addr;

	packet->iph = ((struct iphdr)
		      {
#if __BYTE_ORDER == __LITTLE_ENDIAN
			      5, 4,
#else
			      4, 5,
#endif
			      tos, 0,
			      0, 0, ttl, proto, 0,
			      saddr, daddr });
	return len;
}

/* TYPE CODE [ID SEQUENCE] */
static int
parse_icmp(struct packet *packet, int datalen, char *args[])
{
	int type, code;

	if (!args[0] || (type = string_to_number(args[0], 0, 255)) < 0) {
		nfsim_log(LOG_UI, "Bad icmp type: `%s'", args[0]);
		return -1;
	}

	if (!args[1] || (code = string_to_number(args[1], 0, 255)) < 0) {
		nfsim_log(LOG_UI, "Bad icmp code: `%s'", args[1]);
		return -1;
	}

	packet->u.icmph.type = type;
	packet->u.icmph.code = code;
	packet->u.icmph.checksum = 0;

	if (type == 0 || type == 8) {
		int id, seq;

		if (!args[2] || (id = string_to_number(args[2], 0, 255)) < 0) {
			nfsim_log(LOG_UI, "Bad icmp id: `%s'", args[2]);
			return -1;
		}

		if (!args[3] || (seq = string_to_number(args[3], 0, 255)) < 0) {
			nfsim_log(LOG_UI, "Bad icmp seq: `%s'", args[3]);
			return -1;
		}

		if (args[4]) {
			nfsim_log(LOG_UI, "Extra argument: `%s'", args[3]);
			return -1;
		}

		packet->u.icmph.un.echo.id = htons(id);
		packet->u.icmph.un.echo.sequence = htons(seq);
	} else if (type == ICMP_PARAMETERPROB) {
		int param;

		if (!args[2] || (param = string_to_number(args[2], 0, 255)) < 0) {
			nfsim_log(LOG_UI, "Bad param problem: `%s'", args[2]);
			return -1;
		}

		packet->u.icmph.un.gateway = htonl(param << 24);
	} else if (args[2]) {
		nfsim_log(LOG_UI, "Extra argument: `%s'", args[2]);
		return -1;
	}

	packet->u.icmph.checksum
		= csum_fold(csum_partial(&packet->u,
					 sizeof(packet->u.icmph) + datalen,
					 0));
	return sizeof(packet->u.icmph) + datalen;
}

/* SRCPT DSTPT [DATA] */
static int
parse_udp(struct packet *packet, int datalen, char *args[], char **dump_flags)
{
	int spt, dpt;
	u_int16_t udplen = sizeof(packet->u.udph) + datalen;
	struct {
		u_int32_t srcip, dstip;
		u_int8_t mbz, protocol;
		u_int16_t proto_len;
	} pseudo_header = { packet->iph.saddr, packet->iph.daddr,
		0, IPPROTO_UDP, htons(udplen) };

	if (!args[0] || (spt = string_to_number(args[0], 0, 65535)) < 0) {
		nfsim_log(LOG_UI, "Bad udp spt: `%s'", args[0]);
		return -1;
	}

	if (!args[1] || (dpt = string_to_number(args[1], 0, 65535)) < 0) {
		nfsim_log(LOG_UI, "Bad udp dpt: `%s'", args[1]);
		return -1;
	}

	if (args[2] && streq(args[2], "DATA")) {
		/* Remaining args are  DATA for packet. */
		char *data, **arg;

		*dump_flags = talloc_asprintf_append(*dump_flags, "data");
		data = (void *)&packet->u.udph + sizeof(packet->u.udph);
		for (arg = args+3; *arg; arg++) {
			data = copy_printable(data, *arg);
			if (!data)
				return -1;
			if (arg[1]) {
				*data = ' ';
				data++;
			}
		}
		*data = '\0';
		args++;
	} else if (args[2]) {
		nfsim_log(LOG_UI, "Extra argument: `%s'", args[3]);
		return -1;
	}

	packet->u.udph = (struct udphdr){ htons(spt), htons(dpt),
				htons(udplen), 0 };

	packet->u.udph.check
		= csum_fold(csum_partial(&packet->u.udph, udplen,
					 csum_partial(&pseudo_header,
						      sizeof(pseudo_header),
						      0)));
	return udplen;
}

static int
parse_flags(const char *string, struct tcphdr *tcph)
{
	memset(tcph, 0, sizeof(*tcph));

	if (streq(string, "NONE"))
		return 0;

	do {
		if (strstarts(string, "SYN"))
			tcph->syn = 1;
		else if (strstarts(string, "ACK"))
			tcph->ack = 1;
		else if (strstarts(string, "RST"))
			tcph->rst = 1;
		else if (strstarts(string, "FIN"))
			tcph->fin = 1;
		else if (strstarts(string, "CWR"))
			tcph->cwr = 1;
		else if (strstarts(string, "ECE"))
			tcph->ece = 1;
		else if (strstarts(string, "PSH"))
			tcph->psh = 1;
		else if (strstarts(string, "URG"))
			tcph->urg = 1;
		else {
			nfsim_log(LOG_UI, "Bad tcp flags `%s'", string);
			return 1;
		}
		string += 3;
	} while (*(string++) == '/');

	if (string[-1] != '\0') {
		nfsim_log(LOG_UI, "Bad tcp flags `%s'", string);
		return 1;
	}
	return 0;
}

static int
parse_tcp_options(char *opts, struct tcphdr *tcph)
{
	char *tok;
	u_int8_t options[15 * 4 - sizeof(struct tcphdr)];
	size_t i;

	/* Options are simple comma-separated number list. */
	for (i = 0, tok=strtok(opts, ",");
	     tok && i < sizeof(options);
	     tok=strtok(NULL, ","), i++)
		options[i] = atol(tok);

	if (tok) {
		nfsim_log(LOG_UI, "Too many TCP options: %s", opts);
		return 1;
	}
	if (i % 4 != 0) {
		nfsim_log(LOG_UI, "TCP options must pad to 4 bytes");
		return 1;
	}
	tcph->doff = (sizeof(struct tcphdr) + i) / 4;
	memcpy(tcph+1, options, i);
	return 0;
}

static int
parse_tcpnumber(const char *number, u_int32_t *val)
{
	unsigned int i;

	i = string_to_number(number, 0, UINT_MAX);
	if (i < 0) {
		nfsim_log(LOG_UI, "Invalid tcp number `%s'", number);
		return 1;
	}

	*val = htonl((u_int32_t)i);
	return 0;
}

/* SRCPT DSTPT FLAGS [SEQ=] [ACK=] [WIN=] [OPT=] [DATA] */
static int
parse_tcp(struct packet *packet, int datalen, char *args[], char **dump_flags)
{
	int spt, dpt, window = 0;
	u_int16_t tcplen;
	struct {
		u_int32_t srcip, dstip;
		u_int8_t mbz, protocol;
		u_int16_t proto_len;
	} pseudo_header = { packet->iph.saddr, packet->iph.daddr,
		0, IPPROTO_TCP };

	packet->u.tcph.seq = packet->u.tcph.ack_seq = window = 0;

	if (!args[0] || (spt = string_to_number(args[0], 0, 65535)) < 0) {
		nfsim_log(LOG_UI, "Bad tcp spt: `%s'", args[0]);
		return -1;
	}

	if (!args[1] || (dpt = string_to_number(args[1], 0, 65535)) < 0) {
		nfsim_log(LOG_UI, "Bad tcp dpt: `%s'", args[1]);
		return -1;
	}

	if (!args[2]) {
                nfsim_log(LOG_UI, "No tcp flags were set!");
                return -1;
        }

	if (parse_flags(args[2], &packet->u.tcph))
		return -1;

	if (args[3] && strncmp(args[3], "SEQ=", 4) == 0) {
		if (parse_tcpnumber(args[3] + 4, &packet->u.tcph.seq))
			return -1;
		args++;
	}

	if (args[3] && strncmp(args[3], "ACK=", 4) == 0) {
		if (parse_tcpnumber(args[3] + 4, &packet->u.tcph.ack_seq))
			return -1;
		args++;
	}

	if (args[3] && strncmp(args[3], "WIN=", 4) == 0) {
		if ((window = string_to_number(args[3] + 4, 0, 65535)) < 0) {
			nfsim_log(LOG_UI, "Bad tcp window: `%s'", args[3]);
			return -1;
		} else
			args++;
	}

	if (args[3] && strncmp(args[3], "OPT=", 4) == 0) {
		if (parse_tcp_options(args[3] + 4, &packet->u.tcph))
			return -1;
		args++;
	} else {
		packet->u.tcph.doff = sizeof(struct tcphdr) / 4;
	}

	if (args[3] && strcmp(args[3], "DATA") == 0) {
		/* Remaining args are  DATA for packet. */
		char *data, **arg;

		*dump_flags = talloc_asprintf_append(*dump_flags, "data");
		data = (void *)&packet->u.tcph + packet->u.tcph.doff * 4;
		for (arg = args+4; *arg; arg++) {
			data = copy_printable(data, *arg);
			if (!data)
				return -1;
			if (arg[1]) {
				*data = ' ';
				data++;
			}
		}
		*data = '\0';
		args++;
	} else if (args[3]) {
		nfsim_log(LOG_UI, "Extra argument: `%s'", args[3]);
		return -1;
	}

	packet->u.tcph.source = htons(spt);
	packet->u.tcph.dest = htons(dpt);
	packet->u.tcph.window = htons(window);
	packet->u.tcph.check = packet->u.tcph.urg_ptr = 0;

	tcplen = packet->u.tcph.doff * 4 + datalen;
	pseudo_header.proto_len = htons(tcplen);
	packet->u.tcph.check
		= csum_fold(csum_partial(&packet->u.tcph, tcplen,
					 csum_partial(&pseudo_header,
						      sizeof(pseudo_header),
						      0)));
	return tcplen;
}

static int
parse_mac(const char *str, u_int8_t *mac)
{
	unsigned int i = 0;

	if (strlen(str) != ETH_ALEN*3-1)
		return 0;

	for (i = 0; i < ETH_ALEN; i++, mac++) {
		long number;
		char *end;

		number = strtol(str + i*3, &end, 16);

		if (end == str + i*3 + 2
		    && number >= 0
		    && number <= 255)
			*mac = (u_int8_t)number;
		else
			return 0;
	}
	return 1;
}

bool send_packet(const struct packet *packet, const char *interface,
		 char *dump_flags)
{
	struct sk_buff *skb;
	int len;

	/* Ethernet header and IPv4 headers are linear for netfilter hooks. */
	len = offsetof(struct packet, iph) + packet->iph.ihl*4;
	skb = nfsim_nonlinear_skb(packet, len, (void *)packet + len,
				  offsetof(struct packet, iph)
				  + ntohs(packet->iph.tot_len) - len);
	nfsim_check_packet(skb);

	skb->mac.ethernet = (void *)skb->data + offsetof(struct packet, ehdr);
	skb->nh.iph       = (void *)skb->data + offsetof(struct packet, iph);
	skb->protocol = skb->mac.ethernet->h_proto;

	/* You'll need to linearize to get this one. */
	skb->h.raw = NULL;

	if (dump_flags)
		field_attach(skb, "dump_flags", dump_flags);

	if (interface) {
		struct net_device *dev;
		dev = interface_by_name(interface);
		if (!dev) {
			nfsim_log(LOG_UI, "No interface '%s'", interface);
			kfree_skb(skb);
			return false;
		}
		skb->dev = dev;
		nf_rcv(skb);
	} else {
		nf_rcv_local(skb);
	}
	return true;
}

static void gen_ip_help(int argc, char **argv)
{
#include "gen_ip-help:gen_ip"
/*** XML Help:
    <section id="c:gen_ip">
     <title><command>gen_ip</command></title>
     <para>Generate IP packets and send through the protocol layers</para>
     <cmdsynopsis>
      <command>gen_ip</command>
      <arg choice="opt">IF=<replaceable>interface</replaceable></arg>
      <arg choice="opt">FRAG=</arg>
      <arg choice="opt">DF</arg>
      <arg choice="opt">MF</arg>
      <arg choice="opt">CE</arg>
      <arg choice="opt">MAC=<replaceable>ethaddr</replaceable></arg>
      <arg choice="opt">TOS=<replaceable>diffserv field</replaceable></arg>
      <arg choice="opt">TTL=<replaceable>ttl</replaceable></arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">datalen</arg>
      <arg choice="req">protocol</arg>
      <arg choice="req">protocol-opts</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>gen_ip</command>
      <arg choice="opt"><replaceable>options</replaceable></arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">datalen</arg>
      TCP
      <arg choice="req">srcpt</arg>
      <arg choice="req">dstpt</arg>
      <arg choice="req">flags</arg>
      <arg choice="opt">SEQ=<replaceable>seqnum</replaceable></arg>
      <arg choice="opt">ACK=<replaceable>seqnum</replaceable></arg>
      <arg choice="opt">WIN=<replaceable>wsize</replaceable></arg>
      <arg choice="opt">OPT=<replaceable>options</replaceable></arg>
      <arg choice="opt">DATA <replaceable>args</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>gen_ip</command>
      <arg choice="opt"><replaceable>options</replaceable></arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">datalen</arg>
      UDP
      <arg choice="req">srcpt</arg>
      <arg choice="req">dstpt</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>gen_ip</command>
      <arg choice="opt"><replaceable>options</replaceable></arg>
      <arg choice="req">src</arg>
      <arg choice="req">dest</arg>
      <arg choice="req">datalen</arg>
      ICMP
      <arg choice="req">type</arg>
      <arg choice="req">code</arg>
      <arg choice="opt">id</arg>
      <arg choice="opt">sequence</arg>
     </cmdsynopsis>
     <para><command>gen_ip</command> will construct an IP packet and send it on
      the specified interface, or if no interface is specified, the packet will
      be sent from as if from the local host.</para>

     <para>If <arg>protocol</arg> is TCP, then the ports and TCP flags
     must be specified.  The format of the <arg>flags</arg> is
     'SYN/ACK' etc. The format of the <arg>OPT=</arg> option is a
     comma-separated list of numbers, and there must be a multiple of
     four of them, such as: <screen><![CDATA[1,2,3,4]]></screen>.  If
     DATA is specified, then the data of the packet will be populated
     with the rest of the arguments, with escapes for \n, \r, \t and
     \0. </para>

     <para>If <arg>protocol</arg> is UDP then the ports must be
     specified.</para>

     <para>If <arg>protocol</arg> is ICMP then the type and code must
     be specified: the id and sequence fields are optional.</para>
    </section>
*/
}

/* SRCIP DSTIP DATALEN PROTOCOL ... */
bool parse_packet(struct packet *packet, int argc, char *argv[],
		  char **dump_flags)
{
	int tos = 0, mf = 0, df = 0, ce = 0, ttl = 255, len, datalen;
	u_int16_t fragoff = 0, fraglen = 0;

	if (argc < 5)
		return false;

	if (argc > 5 && strncmp(argv[1], "FRAG=", 5) == 0) {
		const char *comma = strchr(argv[1]+5, ',');

		fragoff = atoi(argv[1]+5);
		if (fragoff > 65528 || (fragoff % 8) != 0) {
			nfsim_log(LOG_UI, "`%s' not >= 8, <= 65528, mod 8 = 0",
				argv[1]+5);
			return false;
		}

		if (!comma) {
			nfsim_log(LOG_UI, "Need FRAG=<offset>,<length>");
			return false;
		}

		fraglen = atoi(comma+1);
		if (fraglen < 1) {
			nfsim_log(LOG_UI, "`%s' bad fraglen",
				comma + 1);
			return false;
		}

		argc--;
		argv++;
	}

	if (argc > 5 && strcmp(argv[1], "DF") == 0) {
		df = 1;
		argc--;
		argv++;
	}

	if (argc > 5 && strcmp(argv[1], "MF") == 0) {
		mf = 1;
		argc--;
		argv++;
	}

	if (argc > 5 && strcmp(argv[1], "CE") == 0) {
		ce = 1;
		argc--;
		argv++;
	}

	if (argc > 5 && strncmp(argv[1], "MAC=", 4) == 0) {
		/* populate both h_dest and h_source */
		if (!parse_mac(argv[1]+4, packet->ehdr.h_dest)
		    || !parse_mac(argv[1]+4, packet->ehdr.h_source)) {
			nfsim_log(LOG_UI, "Invalid mac address.");
			return false;
		}
		argc--;
		argv++;
	} else {
		memset(packet->ehdr.h_dest, 0, sizeof(packet->ehdr.h_dest));
		memset(packet->ehdr.h_source, 0,sizeof(packet->ehdr.h_source));
	}

	if (argc > 5 && strncmp(argv[1], "TOS=", 4) == 0) {
		tos = atoi(argv[1]+4);

		if (tos < 0 || tos > 255) {
			nfsim_log(LOG_UI, "`%s' not >= 0, <= 255",
				argv[1]+4);
			return false;
		}
		*dump_flags = talloc_asprintf_append(*dump_flags, "tos");
		argc--;
		argv++;
	}

	if (argc > 5 && strncmp(argv[1], "DSCP=", 5) == 0) {
		if (*dump_flags && strstr(*dump_flags, "tos")) {
			nfsim_log(LOG_UI,
				  "You can't use both 'TOS=' and 'DSCP='");
			return false;
		}

		if ((tos = string_to_number(argv[1]+5, 0, IPT_DSCP_MAX))==-1) {
			nfsim_log(LOG_UI, "`%s' not >=0, <= %i",
				  argv[1]+5, IPT_DSCP_MAX);
			return false;
		}

		tos = ((tos << IPT_DSCP_SHIFT) & IPT_DSCP_MASK);
		*dump_flags = talloc_asprintf_append(*dump_flags, "dscp");
		argc--;
		argv++;
	}

	if (argc > 5 && strncmp(argv[1], "ECT=", 4) == 0) {
		int ect;
		if ((ect = string_to_number(argv[1]+4, 0, 3))==-1) {
			nfsim_log(LOG_UI, "`%s' not >=0, <= %i", argv[1]+5, 3);
			return false;
		}

		tos |= ect;
		*dump_flags = talloc_asprintf_append(*dump_flags, "ect");
		argc--;
		argv++;
	}

	if (argc > 5 && strncmp(argv[1], "TTL=", 4) == 0) {
		ttl = atoi(argv[1]+4);

		if (ttl < 0 || ttl > 255) {
			nfsim_log(LOG_UI, "`%s' not >= 0, <= 255",
				argv[1]+4);
			return false;
		}
		*dump_flags = talloc_asprintf_append(*dump_flags, "ttl");
		argc--;
		argv++;
	}

	memset(&packet->u, 0, sizeof(packet->u));
	datalen = parse_header(packet, argv[1], argv[2], argv[3], argv[4],
			       tos, ttl);
	if (datalen < 0)
		return false;

	switch (packet->iph.protocol) {
	case IPPROTO_ICMP:
		len = parse_icmp(packet, datalen, argv + 5);
		break;
	case IPPROTO_TCP:
		len = parse_tcp(packet, datalen, argv + 5, dump_flags);
		break;
	case IPPROTO_UDP:
		len = parse_udp(packet, datalen, argv + 5, dump_flags);
		break;
	default:
		if (argc != 5) {
			nfsim_log(LOG_UI, "Extra args for unknown protocol");
			return false;
		}
		len = datalen;
	}

	if (len < 0)
		return false;
	packet->iph.tot_len = htons(sizeof(packet->iph) + len);

	/* Fragment?  Move data down. */
	if (fraglen) {
		memmove(&packet->u, (char *)&packet->u + fragoff, fraglen);

		packet->iph.tot_len = htons(sizeof(packet->iph) + fraglen);
		packet->iph.frag_off = htons(fragoff / 8);
	}

	if (df)
		packet->iph.frag_off |= htons(IP_DF);
	if (mf)
		packet->iph.frag_off |= htons(IP_MF);
	if (ce)
		packet->iph.frag_off |= htons(IP_CE);

	packet->pad = 0;
	packet->iph.check = ~csum_partial(&packet->iph, sizeof(packet->iph),0);
	packet->ehdr.h_proto = htons(ETH_P_IP);
	return true;
}

static bool gen_ip(int argc, char *argv[])
{
	struct packet packet;
	char *dump_flags = NULL;
	char *interface = NULL;

	if (argc > 5 && strncmp(argv[1], "IF=", 3) == 0) {
		interface = argv[1] + 3;
		argc--;
		argv++;
	}

	if (!parse_packet(&packet, argc, argv, &dump_flags)) {
		gen_ip_help(0, NULL);
		return false;
	}

	return send_packet(&packet, interface, dump_flags);
}

static void init(void)
{
	tui_register_command("gen_ip", gen_ip, gen_ip_help);
}

init_call(init);

