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
#include <utils.h>

static int ifindex_counter = 1;

static bool ifshow(struct net_device *dev)
{
	struct in_device *indev;
	struct in_ifaddr *ifaddr;


	indev = (struct in_device *)(dev->ip_ptr);

	nfsim_log(LOG_ALWAYS, "%s", dev->name);

	for (ifaddr = indev->ifa_list; ifaddr; ifaddr = ifaddr->ifa_next) {
		nfsim_log(LOG_ALWAYS,
			"\taddr:  %u.%u.%u.%u"
			"\tmask:  %u.%u.%u.%u"
			"\tbcast: %u.%u.%u.%u",
			NIPQUAD(ifaddr->ifa_local),
			NIPQUAD(ifaddr->ifa_mask),
			NIPQUAD(ifaddr->ifa_broadcast));
	}

	nfsim_log(LOG_ALWAYS,
	      "\tRX packets: %lu bytes: %lu\n"
	      "\tTX packets: %lu bytes: %lu\n",
	      dev->stats.rxpackets, dev->stats.rxbytes,
	      dev->stats.txpackets, dev->stats.txbytes);
	return true;
}

static bool set_device(struct net_device *dev, int argc, char **argv)
{
	struct in_ifaddr *ifaddr;

	assert(dev);
	ifaddr = ((struct in_device *)(dev->ip_ptr))->ifa_list;

	assert(ifaddr);

	if (argc) {
		if (!inet_atou32(*argv, &ifaddr->ifa_local)) {
			nfsim_log(LOG_ALWAYS, "invalid interface address '%s'",
				*argv);
			return false;
		}
		ifaddr->ifa_address = ifaddr->ifa_local;
		__call_inetaddr_notifier(NETDEV_UP, ifaddr);
	}

	if (!--argc)
		return true;
	argv++;

	if (argc) {

		if (!strcmp(*argv, "mask") && argc > 1) {
			if (!inet_atou32(*(argv+1), &ifaddr->ifa_mask)) {
				nfsim_log(LOG_ALWAYS,
					"invalid netmask '%s'", *(argv+1));
				return false;
			}
			argv++;

		} else {
			int mask = atoi(*argv);

			if (mask < 8 || mask > 31) {
				nfsim_log(LOG_ALWAYS,
				    "nonsensical netmask bits '%s'", *argv);
				return false;
			}

			ifaddr->ifa_mask = htonl(~(0xffffffff >> mask));
		}
	}

	if (!--argc)
		return true;
	argv++;

	if (argc) {
		if (!inet_atou32(*argv, &ifaddr->ifa_broadcast)) {
			nfsim_log(LOG_ALWAYS, "invalid broadcast address '%s'",
				*argv);
			return false;
		}
	}

	return true;
}

/* Used by the core to create default devices. */
struct net_device *create_device(const char *name, int argc, char **argv)
{
	struct net_device *dev;
	struct in_device *indev;

	dev = talloc(NULL, struct net_device);
	indev = talloc(dev, struct in_device);

	strncpy(dev->name, name, IFNAMSIZ);
	dev->ifindex = ifindex_counter++;
	dev->ip_ptr = indev;
	dev->hard_header_len = ETH_HLEN;
	memset(&dev->stats, 0, sizeof(dev->stats));

	indev->ifa_list = talloc(indev, struct in_ifaddr);
	indev->ifa_list->ifa_next = NULL;
	indev->ifa_list->ifa_dev = indev;
	indev->dev = dev;

	if (!set_device(dev, argc, argv)) {
		talloc_free(dev);
		return NULL;
	}

	add_route_for_device(indev);
	list_add_tail(&dev->entry, &interfaces);
	return dev;
}

static bool ifconfig(int argc, char **argv)
{
	struct net_device *dev;
	struct in_device *indev;

	if (argc == 1) {
		list_for_each_entry(dev, &interfaces, entry)
			ifshow(dev);
		return true;
	}

	/* first arg is the device name */
	dev = interface_by_name(argv[1]);

	if (!strncmp(argv[argc-1], "up", 2)) {
		if (dev) {
			nfsim_log(LOG_ALWAYS, "device '%s' already exists",
				dev->name);
			return false;
		}

		if (argc < 4) {
			nfsim_log(LOG_ALWAYS,
				"not enough arguments to bring up device");
			return false;
		}
		create_device(argv[1], argc-2, argv+2);
		return true;
	}

	/* for everything else, we need dev to exist */
	if (!dev) {
		u_log("No such device '%s'", argv[1]);
		return false;
	}

	if (argc == 2) {
		return ifshow(dev);
	}

	/* IP address either going down or changing. */
	indev = dev->ip_ptr;
	__call_inetaddr_notifier(NETDEV_DOWN, indev->ifa_list);

	if (argc == 3 && !strncmp(argv[2], "down", 4)) {
		list_del(&dev->entry);
		talloc_free(dev);
		return true;
	}
	return set_device(dev, argc-2, argv+2);
}

static void ifconfig_help(int argc, char **argv)
{
#include "ifconfig-help:ifconfig"
/*** XML Help:
    <section id="c:ifconfig">
     <title><command>ifconfig</command></title>
     <para>Administer the simulated network interfaces. Interfaces can be
      brought up or down, or reconfigured while up.</para>
     <para>At present, interfaces can be assigned only one interface (this
      limitiation is due solely to the ifconfig tool; the simulated kernel
      environment can handle multiple interface addresses)</para>
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="opt"><replaceable>interface</replaceable></arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="req"><replaceable>interface</replaceable></arg>
      <arg choice="req">
       <synopfragmentref linkend="ifoptions">if_options</synopfragmentref>
      </arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="req"><replaceable>interface</replaceable></arg>
      <arg choice="req">
       <synopfragmentref linkend="ifoptions">if_options</synopfragmentref>
      </arg>
      <arg choice="plain">up</arg>
     </cmdsynopsis>
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="req"><replaceable>interface</replaceable></arg>
      <arg choice="plain">down</arg>

      <synopfragment id="ifoptions">
       <group choice="req">
        <arg choice="req">
	 <replaceable>ip_addr</replaceable>
	 /
	 <replaceable>netmask_bits</replaceable>
	</arg>
        <arg choice="req">
	 <replaceable>ip_addr</replaceable> mask
         <replaceable>netmask</replaceable>
	</arg>
       </group>
       <arg choice="req"><replaceable>broadcast_addr</replaceable></arg>
      </synopfragment>
     </cmdsynopsis>

     <para>If <command>ifconfig</command> is invoked with no arguments, it will
      show the configuration of all devices. If a single argument is specified,
      it is assumed to be a device name, and the configuration for that device
      is displayed.
     </para>
     <para>To bring a new interface up, specify the interface name and a
      network specification:
      <screen>ifconfig eth2 10.0.0.1 8 10.255.255.255 up</screen>
      Alternatively, the netmask can be specified in dotted-decimal notation.
      This command is equivalent to the previous:
      <screen>ifconfig eth2 10.0.0.1 mask 255.0.0.0 10.255.255.255 up</screen>
     </para>

     <para>Interfaces can be brought down with the command
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="req"><replaceable>interface</replaceable></arg>
      <arg choice="plain">down</arg>
     </cmdsynopsis>
    </para>

    <para>To reconfigure an interface, use the syntax
     <cmdsynopsis>
      <command>ifconfig</command>
      <arg choice="req"><replaceable>device</replaceable></arg>
      <arg><replaceable>ip_addr</replaceable>
       <arg><replaceable>netmask_bits</replaceable>
        <arg><replaceable>broadcast_addr</replaceable></arg>
       </arg>
      </arg>
      </cmdsynopsis>
     </para>

    </section>
*/
}

static void init(void)
{
	tui_register_command("ifconfig", ifconfig, ifconfig_help);
}

init_call(init);

