/*

Copyright (c) 2003,2004 Jeremy Kerr, Rusty Russell & Harald Welte

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

#include "nfsockopt.h"
#include "utils.h"
#include "log.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/wait.h>

#include <dlfcn.h>

#include <assert.h>
#include <string.h>

#include <netinet/in.h>

/* pointers to the real functions */
static int (*__socket)(int, int, int);
static int (*__getsockopt)(int, int , int , void *, socklen_t *);
static int (*__setsockopt)(int, int , int , void *, socklen_t *);
static int (*__open)(const char *pathname, int flags, ...);
FILE * (*__fopen)(const char *path, const char *mode);
static int (*__close)(int);

static int sd;
static void *handle;
static char *proc_prefix = "/tmp/nfsim/proc";

#undef DEBUG

/* utils expects a log function.  Give it to them. */
bool nfsim_log(enum log_type type, const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "FATAL: ");
	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);

	fprintf(stderr, "\n");
	return false;
}

/**
 * dlysym a function to a '__function' pointer
 */
#define sym(h,fn) \
	do { \
	char *e; \
	__##fn = dlsym(h, #fn); \
	if ((e = dlerror()) != NULL) { \
		fprintf(stderr, "%s\n", e); \
		exit(EXIT_FAILURE); \
	} \
	} while (0)


/* Called on load */
void _init(void);
void _init(void)
{
	char *prefix;
	char *fdstr;

	if (!(handle = dlopen("libc.so.6", RTLD_LAZY))) {
		fprintf(stderr, "%s\n", dlerror());
		exit(EXIT_FAILURE);
	}

	sym(handle, getsockopt);
	sym(handle, setsockopt);
	sym(handle, socket);
	sym(handle, open);
	sym(handle, fopen);
	sym(handle, close);

	dlclose(handle);

	if (!(fdstr = getenv("NFSIM_FAKESOCK_FD"))) {
		fprintf(stderr, "The fakesockopt.so library can only be used  "
				"from within nfsim");
		exit(EXIT_FAILURE);
	}
	sd = atoi(fdstr);

	prefix = getenv("NFSIM_PROC_PREFIX");
	if (prefix)
		proc_prefix = prefix;

	return;
}

/* Called on unload */
void _fini(void);
void _fini(void)
{
	(*__close)(sd);
}

static int recv_fd(int from_fd)
{
	struct iovec iov[1];
	struct msghdr msg;
	char buf[1];
	char cmsgbuf[1000];
	struct cmsghdr *cmsg;
	int ret;

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(buf);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	msg.msg_flags = MSG_WAITALL;

	ret = recvmsg(from_fd, &msg, 0);
	if (ret < 0) {
		perror("recv_fd failed");
		exit(EXIT_FAILURE);
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	return *((int*)CMSG_DATA(cmsg));
}

static void do_fork(void)
{
	int pid, out_fd, msg_fd;

	out_fd = recv_fd(sd);
	msg_fd = recv_fd(sd);

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		/* Child: get own fd for output. */
		dup2(out_fd, STDOUT_FILENO);
		dup2(out_fd, STDERR_FILENO);
		close(sd);
		sd = msg_fd;
	} else {
		struct nf_userspace_message msg;
		int status;

		/* Wait for kid.  We write its exit status as a
		 * message through its pipe when it's done.*/
		waitpid(pid, &status, 0);

		fprintf(stderr, "Child exited %i\n", status);
		msg.type = UM_SYSCALL;
		msg.opcode = SYS_EXIT;
		msg.args[0] = status;
		msg.args[1] = 0;
		msg.args[2] = 0;
		msg.args[3] = 0;
		msg.len = 0;
		msg.retval = 0;

		write(msg_fd, &msg, sizeof(msg));
		close(msg_fd);
		close(out_fd);
	}
}

static void handle_kernelop(int fd, struct nf_userspace_message *msg)
{
	struct nf_userspace_message *reply;
	int m_offset = sizeof(struct nf_userspace_message);

	assert(msg->type = UM_KERNELOP);

	switch (msg->opcode) {

	case KOP_COPY_TO_USER:
		/* dst, src, len */
		memcpy((char *)(msg->args[0]),
				(char *)msg + m_offset, msg->args[2]);
		reply = msg;
		reply->len = 0;
		break;
	case KOP_COPY_FROM_USER:
		reply = malloc(m_offset + msg->args[2]);
		memcpy(reply, msg, m_offset);
		memcpy((char *)reply + m_offset,
				(char *)msg->args[1], msg->args[2]);
		reply->len = msg->args[2];

		break;
	case KOP_FORK:
		do_fork();
		return;

	default:
		fprintf(stderr, "Invalid kernelop opcode\n");
		return;

	}

	if (write(sd, reply, m_offset + reply->len) != m_offset + reply->len) {
		perror("write");
		exit(EXIT_FAILURE);
	}

	if (reply != msg)
		free(reply);
}

/* send a syscall and wait for a response, processing any kernelops
 * that arrive in the meantime */
static int fake_syscall(struct nf_userspace_message *msg)
{
	int ret, msize;
	struct nf_userspace_message *reply;

	msize = sizeof(struct nf_userspace_message);

	msg->type = UM_SYSCALL;
	msg->len = 0;
	msg->retval = 0;

	if (write(sd, msg, msize) != msize) {
		perror("write");
		exit(EXIT_FAILURE);
	}

	reply = malloc(sizeof(*reply));

	while(1) {
		if (read(sd, reply, msize) < msize) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		if (reply->len != 0)
			reply = realloc(reply, msize + reply->len);

		if (read(sd, (char *)reply + msize, reply->len) < msg->len) {
			perror("read");
			exit(EXIT_FAILURE);
		}


		switch (reply->type) {

		case UM_SYSCALL:
			ret = reply->retval;
			free(reply);

			if (ret < 0) {
				errno = -ret;
				ret = -1;
			}
			return ret;

		case UM_KERNELOP:
			handle_kernelop(sd, reply);
			break;
		default:
			fprintf(stderr, "Invalid message: %d\n", reply->type);
		}
	}

}

int getsockopt(int s, int level, int optname, void *optval,
		socklen_t *optlen)
{
	int ret;
	struct nf_userspace_message msg;

	msg.opcode  = SYS_GETSOCKOPT;
	msg.args[0] = PF_INET;
	/*msg.args[1] = level;*/
	msg.args[1] = optname;
	msg.args[2] = (unsigned long)optval;
	msg.args[3] = (unsigned long)*optlen;
	ret = fake_syscall(&msg);
	*optlen = msg.args[3];
	return ret;
}

int setsockopt(int s, int level, int optname, const void *optval,
		socklen_t optlen)
{
	struct nf_userspace_message msg;
	msg.opcode  = SYS_SETSOCKOPT;
	msg.args[0] = PF_INET;
	/*msg.args[1] = level;*/
	msg.args[1] = optname;
	msg.args[2] = (unsigned long)optval;
	msg.args[3] = (unsigned long)optlen;
	return fake_syscall(&msg);
}


/**
 * fake socket() call. returns the sd used to connect to the simulator, so
 * read() or write() will cause a world of pain. but iptables etc. shouldn't
 * be reading or writing to a raw socket anyhow...
 */
int socket(int domain, int type, int protocol)
{
#ifdef DEBUG
	printf("socket(%d, %d, %d)\n", domain, type, protocol);
#endif /* DEBUG */

	/* only catch AF_INET/SOCK_RAW/IPPROTO_RAW sockets */
	if (domain == AF_INET && type == SOCK_RAW && protocol == IPPROTO_RAW)
		return sd;
	//else if (domain == AF_NETLINK)
	//	return fake_netlink_socket(domain, type, protocol);
	else
		return __socket(domain, type, protocol);
}

static int file_is_proc(const char *pathname)
{
	if (pathname && strlen(pathname) >= 6
	    && !strncmp(pathname, "/proc/", 6))
		return 1;
	else
		return 0;
}

static char *mangle_alloc_proc_path(const char *pathname)
{
	char *fakepath = (char *) malloc(strlen(pathname)
			 + strlen(proc_prefix));
	if (!fakepath)
		return NULL;

	strcpy(fakepath, proc_prefix);
	strcat(fakepath, pathname+5); /* 5 because '/' */

	return fakepath;
}


/* open intercepts any accesses to /proc and directs them to somewhere else */
int open(const char *pathname, int flags, ...)
{
	int ret;
	va_list arglist;
	mode_t mode = 0;

#ifdef DEBUG
	printf("open(%s, %d)\n", pathname, flags);
#endif

	va_start(arglist, flags);

	/* mode is only valid if O_CREAT was specified */
	if (flags & O_CREAT)
		mode = va_arg(arglist, mode_t);

	/* if this is not a /proc open, pass it through */
	if (!file_is_proc(pathname))
		ret = __open(pathname, flags, mode);
	else {
		char *fakepath = mangle_alloc_proc_path(pathname);
		if (!fakepath) {
			ret = -ENOMEM;
			/* need this ugly goto for va_end */
			goto out;
		}

		/* mode will be ignored in case of no O_CRAT */
		ret = __open(fakepath, flags, mode);

		free(fakepath);
	}
out:
	va_end(arglist);
	return ret;
}

FILE *fopen(const char *path, const char *mode)
{
	FILE *ret;

#ifdef DEBUG
	printf("%s(%s, %s)\n", __FUNCTION__, path, mode);
#endif

	if (!file_is_proc(path))
		ret = __fopen(path, mode);
	else {
		char *fakepath = mangle_alloc_proc_path(path);
		if (!fakepath) {
			errno = -ENOMEM;
			return NULL;
		}

		ret = __fopen(fakepath, mode);
	}
	return ret;
}

int close(int fd)
{
	return (fd == sd) ? 0 : (*__close)(fd);
}
