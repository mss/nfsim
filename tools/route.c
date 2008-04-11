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
#include <ipv4/ipv4.h>
#include "utils.h"


/* hack */

static int get_route_dest(struct ipv4_route *route, int argc, char **argv)
{
	char *mask;
	int maskbits;
	/* 10.0.0.0/8
	     -or-
	   10.0.0.0 mask 255.0.0.0
	 */

	if (argc == 1) {

		if (!(mask = strchr(argv[0], '/'))) {
			nfsim_log(LOG_UI, "no netmask bits given");
			return -1;
		}
		*mask = 0;
		mask++;

		if (!inet_atou32(argv[0], &route->network)) {
			nfsim_log(LOG_UI, "can't parse network address %s",
					argv[0]);
			return -1;
		}

		maskbits = atoi(mask);

		if (maskbits < 0 || maskbits > 32) {
			nfsim_log(LOG_UI, "invalid mask bits: %s", mask);
			return -1;
		}

		route->netmask = (maskbits==32) ? htonl(0xffffffff)
					: htonl(~(0xffffffff >> maskbits));
		route->network &= route->netmask;

		return 0;
	}

	if (argc == 3 && !strcmp(argv[1], "mask")) {
		uint32_t maskaddr;
		char maxbits;

		if (!inet_atou32(argv[0], &route->network)) {
			nfsim_log(LOG_UI, "can't parse network address %s",
					argv[0]);
			return -1;
		}

		if (!inet_atou32(argv[2], &maskaddr)) {
			nfsim_log(LOG_UI, "can't parse netmask address %s",
					argv[2]);
			return -1;
		}

		maxbits = 32;
		maskbits = 0;
		while (((ntohl(maskaddr) << (maskbits)) & 0x80000000)
				&& maxbits--) {
			maskbits++;
		}

		route->netmask = (maskbits==32) ? htonl(0xffffffff)
				: htonl(~(0xffffffff >> maskbits));
		route->network &= route->netmask;

		return 0;
	}



	nfsim_log(LOG_UI, "cant parse route destination : %d args", argc);
	return 1;
}

static struct ipv4_route *find_matching_route(struct ipv4_route *r1)
{
	struct ipv4_route *r2;

	list_for_each_entry(r2, &routes, entry) {
		if (r1->network == r2->network &&
		    r1->netmask == r2->netmask)
		    return r2;
	}
	return NULL;
}

static bool route(int argc, char **argv)
{
	char network[16];
	char netmask[16];

	if (argc == 1 || (argc == 2 && !strcmp(argv[1], "print"))) {
		nfsim_log(LOG_UI, "%-15s %-15s %-10s",
				"network", "netmask", "iface");
		struct ipv4_route *route;
		list_for_each_entry(route, &routes, entry) {
			strncpy(network,inet_u32toa(route->network),16);
			strncpy(netmask,inet_u32toa(route->netmask),16);
			network[15]=0;
			netmask[15]=0;
			nfsim_log(LOG_UI, "%-15s %-15s %-10s",
				network,
				netmask,
				route->interface->name);
		}
		return true;
	}

	if (!strcmp(argv[1], "add")) {
		struct ipv4_route *route;
		struct net_device *dev;

		/*
		   route add 10.0.0.0/24 eth0
		     -or-
		   route add 10.0.0.0 mask 255.0.0.0 eth0
		 */

		if (argc != 4 && argc != 6) {
			nfsim_log(LOG_UI, "not enough arguments");
			return false;
		}

		route = talloc(NULL, struct ipv4_route);
		if (get_route_dest(route, argc-3, argv+2)) {
			talloc_free(route);
			return false;
		}

		if (find_matching_route(route)) {
			nfsim_log(LOG_UI, "route already exists!");
			talloc_free(route);
			return false;
		}

		if (!(dev = interface_by_name(argv[argc-1]))) {
			nfsim_log(LOG_UI, "no such interface %s", argv[argc-1]);
			talloc_free(route);
			return false;
		}

		route->interface = dev;
		list_add(&route->entry, &routes);

		return true;
	}

	if (!strcmp(argv[1], "del")) {
		struct ipv4_route *route;
		struct ipv4_route *r2;
		char found=0;

		route = talloc(NULL, struct ipv4_route);
		if (get_route_dest(route, argc-2, argv+2)) {
			talloc_free(route);
			return false;
		}

		list_for_each_entry(r2, &routes, entry) {

			if (route->network == r2->network &&
			    route->netmask == r2->netmask) {
				found=1;
				talloc_free(r2);
				nfsim_log(LOG_UI, "route removed");
				break;
			}
		}
		if (!found) {
			nfsim_log(LOG_UI, "route not found");
		}

		talloc_free(route);

		return true;
	}


	nfsim_log(LOG_UI, "Unknown argument: %s", argv[1]);
	return false;

}

static void route_help(int argc, char **argv)
{
#include "route-help:route"
/*** XML Help:
    <section id="c:route">
     <title><command>route</command></title>
     <para>Manipulate the IPv4 routing table.</para>

     <cmdsynopsis>
      <command>route</command>
      <arg choice="opt">print</arg>
      <sbr/>
      <command>route</command>
      <arg choice="plain">add</arg>
      <arg choice="req">
       <synopfragmentref linkend="rtnetwork">network</synopfragmentref>
      </arg>
      <arg choice="req"><replaceable>interface</replaceable></arg>
      <sbr/>
      <command>route</command>
      <arg choice="plain">del</arg>
      <arg choice="req">
       <synopfragmentref linkend="rtnetwork">network</synopfragmentref>
      </arg>

      <synopfragment id="rtnetwork">
       <group choice="req">
        <arg choice="req">
	 <replaceable>ip_addr</replaceable> /
	 <replaceable>netmask_bits</replaceable>
	</arg>
        <arg choice="req">
	 <replaceable>ip_addr</replaceable> mask
         <replaceable>netmask</replaceable>
	</arg>
       </group>
      </synopfragment>
     </cmdsynopsis>
     <para><command>route</command> with no arguments, or a single argument
      "print" will display the current routing table.</para>
     <para><command>route del <replaceable>network</replaceable></command> will
      delete the route to the sepecified network.</para>
     <para><command>route add <replaceable>network interface</replaceable>
      </command>will add a route to the sepecified network, via the specified
      interface.</para>
    </section>
*/
}

static void init(void)
{
	tui_register_command("route", route, route_help);
}

init_call(init);

