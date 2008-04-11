#ifndef _NFSIM_GEN_IP_H
#define _NFSIM_GEN_IP_H

/* Defines ip structures.  */
#include <ipv4/ipv4.h>

struct packet {
	uint16_t pad;
	struct ethhdr ehdr;
	struct iphdr iph;
	union {
		struct tcphdr tcph;
		struct udphdr udph;
		struct icmphdr icmph;
		unsigned char raw_data[65536];
	} u;
};

/* Fill in packet, given arguments. */
bool parse_packet(struct packet *packet, int argc, char *argv[],
		  char **dump_flags);

/* Send filled in packet. */
bool send_packet(const struct packet *packet, const char *interface,
		 char *dump_flags);

/* Convert string to number in this range: -1 for fail. */
unsigned int string_to_number(const char *s, unsigned int min, unsigned int max);

/* Convert string of form w.x.y.z to an address.  NULL on fail. */
struct in_addr *dotted_to_addr(const char *dotted);

/* How many bytes will this take up?  Used by tcpsession */
int gen_ip_data_length(unsigned int datanum, char *data[]);
#endif /* _NFSIM_GEN_IP_H */
