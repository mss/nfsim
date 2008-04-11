
#include <protocol.h>

#ifndef __HAVE_IPV6_H
#define __HAVE_IPV6_H 1

#define ICMPV6_DEST_UNREACH		1
#define ICMPV6_PKT_TOOBIG		2
#define ICMPV6_TIME_EXCEED		3
#define ICMPV6_PARAMPROB		4

#define ICMPV6_INFOMSG_MASK		0x80

#define ICMPV6_ECHO_REQUEST		128
#define ICMPV6_ECHO_REPLY		129
#define ICMPV6_MGM_QUERY		130
#define ICMPV6_MGM_REPORT       	131
#define ICMPV6_MGM_REDUCTION    	132

#define ICMPV6_NI_QUERY			139
#define ICMPV6_NI_REPLY			140

#define ICMPV6_MLD2_REPORT		143

#define ICMPV6_DHAAD_REQUEST		144
#define ICMPV6_DHAAD_REPLY		145
#define ICMPV6_MOBILE_PREFIX_SOL	146
#define ICMPV6_MOBILE_PREFIX_ADV	147

/*
 *	Codes for Destination Unreachable
 */
#define ICMPV6_NOROUTE			0
#define ICMPV6_ADM_PROHIBITED		1
#define ICMPV6_NOT_NEIGHBOUR		2
#define ICMPV6_ADDR_UNREACH		3
#define ICMPV6_PORT_UNREACH		4

/*
 *	Codes for Time Exceeded
 */
#define ICMPV6_EXC_HOPLIMIT		0
#define ICMPV6_EXC_FRAGTIME		1

/*
 *	Codes for Parameter Problem
 */
#define ICMPV6_HDR_FIELD		0
#define ICMPV6_UNK_NEXTHDR		1
#define ICMPV6_UNK_OPTION		2

void icmpv6_send(struct sk_buff *skb, int type, int code, __u32 info,
		 struct net_device *dev);

int ip6_forward(struct sk_buff *skb);
int ip6_output(struct sk_buff *skb);

struct dst_entry *ip6_route_output(struct sock *sk, struct flowi *fl);
void ip6_route_input(struct sk_buff *skb);

unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
                                   struct in6_addr *daddr,
                                   __u16 len,
                                   unsigned short proto,
                                   unsigned int csum);

#endif /* __HAVE_IPV6_H */
