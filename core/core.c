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

#include "config.h"
#include "core.h"
#include "message.h"
#include "tui.h"
#include "utils.h"
#include "usage.h"
#include "field.h"
#include "expect.h"

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

static char *nf_retval_str[NF_MAX_VERDICT+1] = {
	"NF_DROP", "NF_ACCEPT", "NF_STOLEN", "NF_QUEUE", "NF_REPEAT"
};

char nf_retval_error[100];
const char *nfsim_testname;

const char *nf_retval(int retval)
{
	if (retval >= 0 && retval < ARRAY_SIZE(nf_retval_str))
		return nf_retval_str[retval];
	else {
		sprintf(nf_retval_error, "ERROR (%d)", retval);
		return nf_retval_error;
	}
}

int nf_retval_by_name(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nf_retval_str); i++)
		if (streq(name, nf_retval_str[i]))
			return i;

	return -1;
}

extern struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];
extern struct list_head nf_sockopts;

const char *nf_hooknames[NPROTO][NF_MAX_HOOKS];

/* userspace queue */
static int queueid;
LIST_HEAD(nfsim_queue);

LIST_HEAD(interfaces);

struct net_device *loopback_dev_p;
void *nfsim_tallocs;
static unsigned int nfsim_tallocs_initial_blocks;

static void core_init(void)
{
	int i, h;
	char *argv[3];

	/* ifconfig lo 127.0.0.1 8 127.255.255.255 up */
	argv[0] = "127.0.0.1";
	argv[1] = "8";
	argv[2] = "127.255.255.255";
	loopback_dev_p = create_device("lo", 3, argv);

	/* ifconfig eth0 192.168.0.1 24 192.168.0.255 up */
	argv[0] = "192.168.0.1";
	argv[1] = "24";
	argv[2] = "192.168.0.255";
	create_device("eth0", 3, argv);

	/* ifconfig eth1 192.168.1.1 24 192.168.1.255 up */
	argv[0] = "192.168.1.1";
	argv[1] = "24";
	argv[2] = "192.168.1.255";
	create_device("eth1", 3, argv);

	for (i = 0; i < NPROTO; i++) {
		for (h = 0; h < NF_MAX_HOOKS; h++)
			INIT_LIST_HEAD(&nf_hooks[i][h]);
	}

	nfsim_log(LOG_UI, "core_init() completed");
}

init_call(core_init);

/* FIXME: Use queue number. */
static int enqueue_packet_to_queuenum(struct sk_buff *skb,
				      struct nf_info *info,
				      unsigned int queuenum,
				      void *data)
{
	struct nfsim_queueitem *pq;

	pq = talloc(skb, struct nfsim_queueitem);
	pq->skb = skb;
	pq->info = info;
	pq->id = queueid++;

	list_add_tail(&pq->list, &nfsim_queue);
	log_packet(skb, "queue:added%s", describe_packet(skb));

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
static struct nf_queue_handler enqueue_packet_h = {
	.outfn = enqueue_packet_to_queuenum,
	.data = NULL,
	.name = "core.c:enqueue_packet",
};
#else
static int enqueue_packet(struct sk_buff *skb, struct nf_info *info,void *data)
{
	return enqueue_packet_to_queuenum(skb, info, 0, data);
}
#endif

/* We want logging for every hook */
unsigned int call_elem_hook(struct nf_hook_ops *ops,
			    unsigned int hooknum,
			    struct sk_buff **skb,
			    const struct net_device *in,
			    const struct net_device *out,
			    int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	char *hookname = talloc_asprintf(NULL, "%s:%i", __func__, hooknum);

	nfsim_check_packet(*skb);

	if (should_i_fail(hookname)) {
		talloc_free(hookname);
		return NF_DROP;
	}
	talloc_free(hookname);

	ret = ops->hook(hooknum, skb, in, out, okfn);
	if (ret == NF_STOLEN)
		nfsim_log(LOG_HOOK, "hook:%s %s %s",
			  nf_hooknames[PF_INET][hooknum],
			  ops->owner ? ops->owner->name : "nfsim",
			  nf_retval(ret));
	else
		nfsim_log(LOG_HOOK, "hook:%s %s %s%s",
			  nf_hooknames[PF_INET][hooknum],
			  ops->owner ? ops->owner->name : "nfsim",
			  nf_retval(ret), describe_packet(*skb));
	return ret;
}


static void run_inits(void)
{
	/* Linker magic creates these to delineate section. */
	extern nfsim_initcall_t __start_nfsim_init_call[],
		__stop_nfsim_init_call[];
	nfsim_initcall_t *p;

	for (p = __start_nfsim_init_call; p < __stop_nfsim_init_call; p++)
		(*p)();
}

static void print_license(void)
{
	printf("nfsim %s, Copyright (C) 2004 Jeremy Kerr, Rusty Russell\n"
	       "Nfsim comes with ABSOLUTELY NO WARRANTY; see COPYING.\n"
	       "This is free software, and you are welcome to redistribute\n"
	       "it under certain conditions; see COPYING for details.\n",
	       VERSION);
}

/*** XML Argument:
    <section id="a:echo">
     <title><option>--echo</option>, <option>-x</option></title>
     <subtitle>Echo commands as they are executed</subtitle>
     <para>nfsim will echo each command before it is executed. Useful when
      commands are read from a file</para>
    </section>
*/
static void cmdline_echo(struct option *opt)
{
	tui_echo_commands = 1;
}
cmdline_opt("echo", 0, 'x', cmdline_echo);

/*** XML Argument:
    <section id="a:quiet">
     <title><option>--quiet</option>, <option>-q</option></title>
     <subtitle>Run quietly</subtitle>
     <para>Causes nfsim to reduce its output to the minimum possible - no prompt
      is displayed, and most warning messages are suppressed
     </para>
    </section>
*/
static void cmdline_quiet(struct option *opt)
{
	tui_quiet = 1;
}
cmdline_opt("quiet", 0, 'q', cmdline_quiet);

/*** XML Argument:
    <section id="a:exit">
     <title><option>--exit</option>, <option>-e</option></title>
     <subtitle>Exit on error</subtitle>
     <para>If <option>--exit</option> is specified, nfsim will exit (with a
     non-zero error code) on the first script error it encounters (eg an
     expect command does not match). This is useful when nfsim is invoked as a
     non-interactive script</para>
    </section>
*/
static void cmdline_abort_on_fail(struct option *opt)
{
	tui_abort_on_fail = 1;
}
cmdline_opt("exit", 0, 'e', cmdline_abort_on_fail);

/*** XML Argument:
    <section id="a:no-modules">
     <title><option>--no-modules</option></title>
     <subtitle>Don't load all available modules on startup</subtitle>
     <para>Usually all netfilter modules are loaded on startup, this option
     will suppress this behaviour. To load modules after invoking nfsim with
     this argument, use the <command>insmod</command> command</para>
    </section>
*/
static bool load_modules = true;
static void cmdline_no_modules(struct option *opt)
{
	load_modules = false;
}
cmdline_opt("no-modules", 0, 'N', cmdline_no_modules);


/*** XML Argument:
    <section id="a:version">
     <title><option>--version</option>, <option>-V</option></title>
     <subtitle>Print the version of the simulator and kernel</subtitle>
    </section>
*/
static void cmdline_version(struct option *opt)
{
	printf("nfsim version %s\nkernel version %s\n",
	       VERSION, UTS_RELEASE);
	print_license();
	exit(EXIT_SUCCESS);
}
cmdline_opt("version", 0, 'V', cmdline_version);

/*** XML Argument:
    <section id="a:help">
     <title><option>--help</option></title>
     <subtitle>Print usage information</subtitle>
     <para>Causes nfsim to print its command line arguments and then exit</para>
    </section>
*/
static void cmdline_help(struct option *opt)
{
	print_license();
	print_usage();
	exit(EXIT_SUCCESS);
}
cmdline_opt("help", 0, 'h', cmdline_help);

extern struct cmdline_option __start_cmdline[], __stop_cmdline[];

static struct cmdline_option *get_cmdline_option(int opt)
{
	struct cmdline_option *copt;

	/* if opt is < '0', we have been passed a long option, which is
	 * indexed directly */
	if (opt < '0')
		return __start_cmdline + opt;

	/* otherwise search for the short option in the .val member */
	for (copt = __start_cmdline; copt < __stop_cmdline; copt++)
		if (copt->opt.val == opt)
			return copt;

	return NULL;
}

static struct option *get_cmdline_options(void)
{
	struct cmdline_option *copts;
	struct option *opts;
	unsigned int x, n_opts;

	n_opts = ((void *)__stop_cmdline - (void *)__start_cmdline) /
		sizeof(struct cmdline_option);

	opts = talloc_zero_array(NULL, struct option, n_opts + 1);
	copts = __start_cmdline;

	for (x = 0; x < n_opts; x++) {
		unsigned int y;

		if (copts[x].opt.has_arg > 2)
			barf("Bad argument `%s'", copts[x].opt.name);

		for (y = 0; y < x; y++)
			if ((copts[x].opt.val && copts[x].opt.val
						== opts[y].val)
					|| streq(copts[x].opt.name,
						opts[y].name))
				barf("Conflicting arguments %s = %s\n",
					copts[x].opt.name, opts[y].name);

		opts[x] = copts[x].opt;
		opts[x].val = x;
	}

	return opts;
}

static char *get_cmdline_optstr(void)
{
	struct cmdline_option *copts;
	unsigned int x, n_opts;
	char *optstr, tmpstr[3], *colonstr = "::";

	n_opts = ((void *)__stop_cmdline - (void *)__start_cmdline) /
		sizeof(struct cmdline_option);

	optstr = talloc_size(NULL, 3 * n_opts * sizeof(*optstr) + 1);
	*optstr = '\0';

	copts = __start_cmdline;

	for (x = 0; x < n_opts; x++) {
		if (!copts[x].opt.val)
			continue;
		snprintf(tmpstr, 4, "%c%s", copts[x].opt.val,
			colonstr + 2 - copts[x].opt.has_arg);
		strcat(optstr, tmpstr);
	}
	return optstr;
}

void check_allocations(void)
{
	proc_cleanup();

	if (talloc_total_blocks(nfsim_tallocs)!=nfsim_tallocs_initial_blocks){
		talloc_report_full(nfsim_tallocs, stderr);
		barf("Resource leak");
	}
}

static void initialize_builtin_modules(void)
{
	/* Linker magic creates these to delineate section. */
	extern initcall_t __start_module_init[], __stop_module_init[];
	initcall_t *p;
	int ret;

	/* Runs in link order. */
	for (p = __start_module_init; p < __stop_module_init; p++) {
		ret = (*p)();
		if (ret)
			nfsim_log(LOG_UI, "Initcall %p failed: %d\n", p, ret);
	}
}

void remove_builtin_modules(void)
{
	/* Linker magic creates these to delineate section. */
	extern exitcall_t __start_module_exit[], __stop_module_exit[];
	exitcall_t *p;

	/* Stop them in reverse order. */
	for (p = __stop_module_exit-1; p >= __start_module_exit; p--)
		(*p)();
}

int main(int argc, char **argv)
{
	int c, input_fd = STDIN_FILENO;
	char *p, *optstr;
	struct option *options;

	nfsim_tallocs = talloc_named_const(NULL, 1, "NFSIM");

	options = get_cmdline_options();
	optstr = get_cmdline_optstr();

	while ((c = getopt_long(argc, argv, optstr, options, NULL)) != EOF) {
		struct cmdline_option *copt = get_cmdline_option(c);
		if (!copt)
			barf("Unknown argument");

		copt->parse(&copt->opt);
	}

	if (optind < argc) {
		input_fd = open(argv[optind], O_RDONLY);
		if (input_fd < 0)
			barf_perror("Opening %s", argv[optind]);
		nfsim_testname = argv[optind];
	} else if (get_failtest())
		barf("Not clever enough to use --failtest interactively");

	/* Hack to make users' lives easier: set LD_LIBRARY_PATH for
	 * fakesockopt.so, based on where we are. */
	p = strrchr(argv[0], '/');
	if (!p || strncmp(argv[0], BINDIR, strlen(BINDIR)) == 0) {
		char *oldpath, *path;

		oldpath = getenv("LD_LIBRARY_PATH");
		path = talloc_asprintf(NULL, "%s%s%s",
				       oldpath ?:"", oldpath ? ":" : "",
				       LIBDIR);
		setenv("LD_LIBRARY_PATH", path, 1);
		talloc_free(path);
		module_path = talloc_asprintf(NULL, "%s/nfsim", LIBDIR);
		extension_path = talloc_asprintf(NULL, ".:%s/extensions:%s",
						 LIBDIR, getenv("PATH"));
	} else {
		char *oldpath, *path;

		/* Assume it's still in working directory. */
		oldpath = getenv("LD_LIBRARY_PATH");
		path = talloc_asprintf(NULL, "%s%s%.*s",
				       oldpath ?:"", oldpath ? ":" : "",
				       (int)(p - argv[0]), argv[0]);
		setenv("LD_LIBRARY_PATH", path, 1);
		talloc_free(path);
		module_path = talloc_asprintf(NULL, "%.*s/netfilter/ipv4",
					      (int)(p - argv[0]), argv[0]);
		extension_path = talloc_asprintf(NULL,
						 ".:%.*s/tools/extensions:%s",
						 (int)(p - argv[0]), argv[0],
						 getenv("PATH"));
	}
	if (getenv("NFSIM_MODPATH"))
		module_path = getenv("NFSIM_MODPATH");
	if (getenv("NFSIM_EXTPATH"))
		extension_path = getenv("NFSIM_EXTPATH");

	/* Don't do failures during initialization. */
	suppress_failtest++;
	kernelenv_init();

	run_inits();
	if (!tui_quiet)
		print_license();

	netfilter_init();

	nfsim_tallocs_initial_blocks = talloc_total_blocks(nfsim_tallocs);

	initialize_builtin_modules();
	if (load_modules && !load_all_modules())
		barf("Module loading failed\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
	nf_register_queue_handler(PF_INET, &enqueue_packet_h);
#else
	nf_register_queue_handler(PF_INET, enqueue_packet, NULL);
#endif

	nfsim_log(LOG_UI, "initialisation done");

	message_init();
	suppress_failtest--;

	tui_run(input_fd);

	/* Everyone loves a good error haiku! */
	if (expects_remaining())
		barf("Expectations still / "
		     "unfulfilled remaining. / "
		     "Testing blossoms fail.");

	message_cleanup();
	if (unload_all_modules()) {
		remove_builtin_modules();
		check_allocations();
	}

	return 0;
}

static char pbuf[1024] ;

const char *describe_packet(struct sk_buff *skb)
{
	char *ptr = pbuf;

	*ptr = '\0';

	if (!log_describe_packets())
		return pbuf;

#if 0
	ptr += sprintf(ptr, " {");

	switch (htons(skb->protocol)) {

	case ETH_P_IP:
		ptr += sprintf(ptr, "IPv4 %s", ipv4_describe_packet(skb));
		break;
	default:
		ptr += sprintf(ptr, "-UNKNOWN PROTOCOL- %d", skb->protocol);
	}


/* out: */
	strcat(ptr, "}");
#endif
	if (skb->nfmark)
		ptr += sprintf(ptr, " MARK %lu", skb->nfmark);
	ptr += sprintf(ptr, " {IPv4 %s}", ipv4_describe_packet(skb));
	return pbuf;
}

struct net_device *interface_by_name(const char *name)
{
	struct net_device *dev;

	list_for_each_entry(dev, &interfaces, entry)
		if (!strncmp(dev->name, name, IFNAMSIZ))
			return dev;


	return NULL;
}

int nf_send_local(struct sk_buff *skb)
{
	log_packet(skb, "send:LOCAL%s", describe_packet(skb));
	kfree_skb(skb);
	return 0;
}

int nf_send(struct sk_buff *skb)
{
	skb->dev->stats.txpackets++;
	skb->dev->stats.txbytes += skb->len;

	log_packet(skb, "send:%s%s", skb->dev->name,
		describe_packet(skb));
	kfree_skb(skb);
	return 0;
}

int nf_rcv(struct sk_buff *skb)
{
	/* change for protocol... */
	return ip_rcv(skb);
}

int nf_rcv_local(struct sk_buff *skb)
{
	/* change for protocol... */
	return ip_rcv_local(skb);
}

/* Don't do should_i_fail() here: it doesn't actually ever fail. */
int __nf_register_hook_wrapper(struct nf_hook_ops *reg, unsigned int n,
			       struct module *owner, const char *location)
{
	unsigned int i;
	int ret;

	for (i = 0; i < n; i++) {
		if (reg->owner && !streq(reg[i].owner->name, owner->name))
			barf("nf_register_hook at %u: owner %s != %s\n",
			     reg[i].owner->name, reg[i].owner);
		reg[i].owner = owner;

		ret = __nf_register_hook(&reg[i]);
		if (ret == 0)
			field_attach_static(&reg[i], location, NULL);
		else
			goto err;
	}
	return ret;
err:
	if (i > 0)
		__nf_unregister_hook_wrapper(reg, i);
	return ret;
}

void __nf_unregister_hook_wrapper(struct nf_hook_ops *reg, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		field_detach_all(&reg[i]);
		__nf_unregister_hook(&reg[i]);
	}
}

int __nf_register_sockopt_wrapper(struct nf_sockopt_ops *reg,
	struct module *owner, const char *location)
{
	int ret;
	if (reg->owner && !streq(reg->owner->name, owner->name))
		barf("nf_register_sockopt at %u: owner %s != %s\n",
		     reg->owner->name, reg->owner);
	reg->owner = owner;

	if (should_i_fail(location))
		return -EINTR;

	ret = __nf_register_sockopt(reg);
	if (ret == 0)
		field_attach_static(reg, location, NULL);
	return ret;
}

void __nf_unregister_sockopt_wrapper(struct nf_sockopt_ops *reg)
{
	field_detach_all(reg);
	__nf_unregister_sockopt(reg);
}
