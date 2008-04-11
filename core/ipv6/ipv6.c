
#include "ipv6.h"
#include "utils.h"
#include <core.h>
#include <field.h>

#include <linux/netfilter_ipv6.h>

int route(struct sk_buff *skb);

LIST_HEAD(routes6);

static struct notifier_block *inet6addr_chain;

static void init(void)
{
	/* name our hooks */
	nf_hooknames[PF_INET6][0] = "NF_IP6_PRE_ROUTING";
	nf_hooknames[PF_INET6][1] = "NF_IP6_LOCAL_IN";
	nf_hooknames[PF_INET6][2] = "NF_IP6_FORWARD";
	nf_hooknames[PF_INET6][3] = "NF_IP6_LOCAL_OUT";
	nf_hooknames[PF_INET6][4] = "NF_IP6_POST_ROUTING";
}

init_call(init);

static int ip6_output2(struct sk_buff *skb)
{
	struct net_device *dev = skb->dst->dev;

	skb->dev  = dev;
	skb->protocol = htons(ETH_P_IPV6);

	return NF_HOOK(PF_INET6, NF_IP6_POST_ROUTING, skb, NULL, skb->dev,
			nf_send);
}

int ip6_output(struct sk_buff *skb)
{
	/* FIXME: fragment? */
	return ip6_output2(skb);
}

int ip6_forward(struct sk_buff *skb)
{
	struct ipv6hdr *hdr = skb->nh.ipv6h;

	if (!(--hdr->hop_limit)) {
		log_route(skb, "ip6_forward: HOPLIMIT expired");
		icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT, 0,
			    skb->dev);
		kfree_skb(skb);
		return 1;
	}
	nfsim_update_skb(skb, &skb->nh.ipv6h->hop_limit,
			 sizeof(skb->nh.ipv6h->hop_limit));

	return NF_HOOK(PF_INET6, NF_IP6_FORWARD, skb, skb->dev, skb->dst->dev,
			dst_output);
}

void ip6_route_input(struct sk_buff *skb)
{

}

struct dst_entry *ip6_route_output(struct sock *sk, struct flowi *fl)
{
	/* FIXME */
	return NULL;
}

void icmpv6_send(struct sk_buff *skb, int type, int code, __u32 info,
		 struct net_device *dev)
{
	/* FIXME */
}

unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
				   struct in6_addr *daddr,
				   __u16 len,
			           unsigned short proto,
			           unsigned int csum) 
{
	/* FIXME */
	return 0;
}

