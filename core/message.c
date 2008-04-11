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

#include "message.h"
#include "utils.h"

#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <linux/netfilter_ipv4/ip_tables.h>

static int msg_fd; /* socket for messages. */
static int io_fd; /* pipe to read child's stdout/stderr */
static int pid = -1; /* pid of child if we forked it ourselves */

static void send_userspace_message(struct nf_userspace_message *msg);

static void send_fd(int dest_fd, int fd)
{
	struct iovec iov[1];
	struct cmsghdr *cmsg;
	struct msghdr msg;
	char buf[1] = { '\0' };

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(buf);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = malloc(CMSG_SPACE(sizeof(fd)));
	msg.msg_controllen = CMSG_SPACE(sizeof(fd));
	msg.msg_flags = 0;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int*)CMSG_DATA(cmsg) = fd;

	if (sendmsg(dest_fd, &msg, 0) < 0)
		barf_perror("sendfd failed");
}

void message_init(void)
{
	sigset_t ss;

	sigemptyset(&ss);
	sigaddset(&ss, SIGPIPE);
	sigprocmask(SIG_BLOCK, &ss, NULL);
}

void message_cleanup(void)
{
	sigset_t ss;

	sigemptyset(&ss);
	sigaddset(&ss, SIGPIPE);
	sigprocmask(SIG_UNBLOCK, &ss, NULL);
}

void start_program(const char *name, int argc, char *argv[])
{
	int iofds[2];
	int msgfds[2];
	char *fdstr;

	if (pipe(iofds) != 0)
		barf_perror("%s pipe", name);

	if (socketpair(PF_UNIX, SOCK_STREAM, PF_UNSPEC, msgfds))
		barf_perror("socket");

	fflush(stdout);
	pid = fork();
	switch (pid) {
	case -1:
		barf_perror("iptables fork");
	case 0:
		dup2(iofds[1], STDOUT_FILENO);
		dup2(iofds[1], STDERR_FILENO);
		close(iofds[0]);
		close(msgfds[0]);
		if (setenv("LD_PRELOAD", "fakesockopt.so.1.0", 1))
			barf("putenv failed");
		fdstr = talloc_asprintf(NULL, "%d", msgfds[1]);
		setenv("NFSIM_FAKESOCK_FD", fdstr, 1);
		talloc_free(fdstr);
		execvp(name, argv);
		fprintf(stderr, "Could not exec %s!\n", name);
		exit(EXIT_FAILURE);
	}

	close(iofds[1]);
	close(msgfds[1]);
	io_fd = iofds[0];
	msg_fd = msgfds[0];
}

static const char *protofamily(int pf)
{
	switch (pf) {
	case PF_INET: return "PF_INET";
	default:
		barf("Unknown protocol family %i", pf);
	}
}

static const char *sockopt(int opt, bool get)
{
	if (get) {
		switch (opt) {
		case IPT_SO_GET_INFO: return "GET_INFO";
		case IPT_SO_GET_ENTRIES: return "GET_ENTRIES";
		/* Old headers don't define IPT_SO_GET_REVISION_MATCH etc. */
		case IPT_SO_GET_ENTRIES+1: return "GET_REVISION_MATCH";
		case IPT_SO_GET_ENTRIES+2: return "GET_REVISION_TARGET";
		}
	} else {
		switch (opt) {
		case IPT_SO_SET_REPLACE: return "SET_REPLACE";
		case IPT_SO_SET_ADD_COUNTERS: return "SET_ADD_COUNTERS";
		}
	}
	barf("Unknown %ssocket opt %i", get ? "get" : "set", opt);
}

static const char *err(int err)
{
	static char errstr[50];

	if (err >= 0)
		sprintf(errstr, "%i", err);
	else {
		switch (err) {
		case -EPERM: strcpy(errstr, "-EPERM"); break;
		case -ENOENT: strcpy(errstr, "-ENOENT"); break;
		case -ESRCH: strcpy(errstr, "-ESRCH"); break;
		case -EINTR: strcpy(errstr, "-EINTR"); break;
		case -EIO: strcpy(errstr, "-EIO"); break;
		case -ENXIO: strcpy(errstr, "-ENXIO"); break;
		case -E2BIG: strcpy(errstr, "-E2BIG"); break;
		case -ENOEXEC: strcpy(errstr, "-ENOEXEC"); break;
		case -EBADF: strcpy(errstr, "-EBADF"); break;
		case -ECHILD: strcpy(errstr, "-ECHILD"); break;
		case -EAGAIN: strcpy(errstr, "-EAGAIN"); break;
		case -ENOMEM: strcpy(errstr, "-ENOMEM"); break;
		case -EACCES: strcpy(errstr, "-EACCES"); break;
		case -EFAULT: strcpy(errstr, "-EFAULT"); break;
		case -ENOTBLK: strcpy(errstr, "-ENOTBLK"); break;
		case -EBUSY: strcpy(errstr, "-EBUSY"); break;
		case -EEXIST: strcpy(errstr, "-EEXIST"); break;
		case -EXDEV: strcpy(errstr, "-EXDEV"); break;
		case -ENODEV: strcpy(errstr, "-ENODEV"); break;
		case -ENOTDIR: strcpy(errstr, "-ENOTDIR"); break;
		case -EISDIR: strcpy(errstr, "-EISDIR"); break;
		case -EINVAL: strcpy(errstr, "-EINVAL"); break;
		case -ENFILE: strcpy(errstr, "-ENFILE"); break;
		case -EMFILE: strcpy(errstr, "-EMFILE"); break;
		case -ENOTTY: strcpy(errstr, "-ENOTTY"); break;
		case -ETXTBSY: strcpy(errstr, "-ETXTBSY"); break;
		case -EFBIG: strcpy(errstr, "-EFBIG"); break;
		case -ENOSPC: strcpy(errstr, "-ENOSPC"); break;
		case -ESPIPE: strcpy(errstr, "-ESPIPE"); break;
		case -EROFS: strcpy(errstr, "-EROFS"); break;
		case -EMLINK: strcpy(errstr, "-EMLINK"); break;
		case -EPIPE: strcpy(errstr, "-EPIPE"); break;
		case -EDOM: strcpy(errstr, "-EDOM"); break;
		case -ERANGE: strcpy(errstr, "-ERANGE"); break;
		case -EPROTONOSUPPORT: strcpy(errstr, "-EPROTONOSUPPORT"); break;
		case -ENOPROTOOPT: strcpy(errstr, "-ENOPROTOOPT"); break;
		case -ELOOP: strcpy(errstr, "-ELOOP"); break;
		default:
			barf("Unknown error %i!\n", -err);
		}
	}
	return errstr;
}

static bool handle_userspace_message(int *status)
{
	int len;
	struct nf_userspace_message msg;

	/* FIXME: msg.len?  Presumable always 0? --RR */
	len = read(msg_fd, &msg, sizeof(msg));
	if (len != sizeof(msg))
		return false;

	if (msg.type == UM_SYSCALL) {
		switch (msg.opcode) {

		case SYS_GETSOCKOPT:
			if (strace)
				nfsim_log(LOG_USERSPACE,
					  "strace: getsockopt(%s, %s,"
					  " %p, %u)",
					  protofamily(msg.args[0]),
					  sockopt(msg.args[1], true),
					  (char *)msg.args[2],
					  msg.args[3]);
			msg.retval = nf_getsockopt(NULL, msg.args[0],
					msg.args[1], (char *)msg.args[2],
					(int *)&msg.args[3]);
			if (strace)
				nfsim_log(LOG_USERSPACE,
					  "    getsockopt -> %s (len %i)",
					  err(msg.retval), msg.args[3]);
			write(msg_fd, &msg, sizeof(msg));
			break;
		case SYS_SETSOCKOPT:
			if (strace)
				nfsim_log(LOG_USERSPACE,
						  "strace: setsockopt(%s, %s,"
						  " %p, %u)",
					  protofamily(msg.args[0]),
						  sockopt(msg.args[1], false),
						  (char *)msg.args[2],
						  msg.args[3]);
			msg.retval = nf_setsockopt(NULL, msg.args[0],
					msg.args[1], (char *)msg.args[2],
					msg.args[3]);
			if (strace)
				nfsim_log(LOG_USERSPACE, "    setsockopt -> %s",
					  err(msg.retval));
			write(msg_fd, &msg, sizeof(msg));
			break;
		case SYS_EXIT:
			*status = msg.args[0];
			break;
		default:
			barf("Invalid syscall opcode %d\n", msg.opcode);
		}
	} else if (msg.type == UM_KERNELOP) {
		switch (msg.opcode) {

		case KOP_COPY_FROM_USER:
			if (complete_read(msg_fd, (char *)msg.args[0],
					msg.args[2]) !=	msg.args[2])
				barf_perror("read");
			break;
		case KOP_COPY_TO_USER:
			/* copying has been done */
			break;

		default:
			barf("Invalid kernelop opcode %d\n", msg.opcode);
		}
	} else
		barf("Unknown message type %d\n", msg.type);

	return true;
}

static void send_userspace_message(struct nf_userspace_message *msg)
{
	write(msg_fd, msg, sizeof(struct nf_userspace_message) + msg->len);

	/* expect a reply */
	handle_userspace_message(NULL);
}

int copy_to_user(void *to, const void *from, unsigned long n)
{
	struct nf_userspace_message *msg;

	if (should_i_fail(__func__)) {
		if (strace)
			nfsim_log(LOG_USERSPACE,
				  "        copy_to_user(%p,%i) = -EFAULT\n",
				  to, n);
		return n;
	}

	msg = _talloc_zero(NULL,
		sizeof(struct nf_userspace_message) + n, "copy_to_user");

	msg->type = UM_KERNELOP;
	msg->opcode = KOP_COPY_TO_USER;
	msg->len = n;
	msg->args[0] = (unsigned long)to;
	msg->args[1] = (unsigned long)from;
	msg->args[2] = (unsigned long)n;

	/* Keep valgrind happy.*/
	msg->args[3] = 0;
	msg->retval = 0;

	if (strace)
		nfsim_log(LOG_USERSPACE, "        copy_to_user(%p,%i)",
			  to, n);
	memcpy((char *)msg + sizeof(struct nf_userspace_message), from, n);

	send_userspace_message(msg);
	talloc_free(msg);

	return 0;
}

int copy_from_user(void *to, const void *from, unsigned long n)
{
	struct nf_userspace_message msg;

	if (should_i_fail(__func__)) {
		if (strace)
			nfsim_log(LOG_USERSPACE,
				  "        copy_from_user(%p,%i) = -EFAULT",
				  to, n);
		return n;
	}

	memset(&msg, 0, sizeof(struct nf_userspace_message));

	msg.type = UM_KERNELOP;
	msg.opcode = KOP_COPY_FROM_USER;
	msg.len = 0;
	msg.args[0] = (unsigned long)to;
	msg.args[1] = (unsigned long)from;
	msg.args[2] = (unsigned long)n;

	/* Keep valgrind happy.*/
	msg.args[3] = 0;
	msg.retval = 0;

	if (strace)
		nfsim_log(LOG_USERSPACE,
			  "        copy_from_user(%p,%i)",
			  to, n);
	send_userspace_message(&msg);

	return 0;
}

/* This child wants a fresh program to play with */
void fork_other_program(void)
{
	struct nf_userspace_message msg;
	int iofds[2], msgfds[2];

	/* Nothing to do if no program attached. */
	if (pid == -1)
		return;

	if (socketpair(PF_UNIX, SOCK_STREAM, PF_UNSPEC, msgfds))
		barf_perror("socket");

	if (pipe(iofds) != 0)
		barf_perror("pipe");

	memset(&msg, 0, sizeof(msg));
	msg.type = UM_KERNELOP;
	msg.opcode = KOP_FORK;
	if (strace)
		nfsim_log(LOG_USERSPACE, "        %i: fork() = %i",
			  getppid(), getpid());

	/* Send fork message (no response) */
	write(msg_fd, &msg, sizeof(struct nf_userspace_message));

	/* Child reads these. */
	send_fd(msg_fd, iofds[1]);
	send_fd(msg_fd, msgfds[1]);
	close(msgfds[1]);
	close(iofds[1]);

	/* Use the new socket pair. */
	msg_fd = msgfds[0];
	io_fd = iofds[0];
}

/* Loop accepting messages from fakesockopt.  If child talks, return. */
char *wait_for_output(int *status)
{
	fd_set fdset;
	int retry = 0;

	/* We need to keep going until both fds drained. */
	while (io_fd != -1 || msg_fd != -1) {
		int maxfd, sret;

		FD_ZERO(&fdset);
		if (msg_fd != -1)
			FD_SET(msg_fd, &fdset);
		if (io_fd != -1)
			FD_SET(io_fd, &fdset);

		maxfd = max(msg_fd, io_fd);
		sret = select(maxfd + 1, &fdset, NULL, NULL, NULL);
		if (sret < 0) {
			if ((errno == EINTR || errno == EAGAIN)
					&& ++retry < 3)
				continue;
			barf_perror("select");
		}

		if (io_fd != -1 && FD_ISSET(io_fd, &fdset)) {
			char *output = talloc_size(NULL, 1024);
			int ret = read(io_fd, output, 1023);
			if (ret > 0) {
				output[ret] = '\0';
				return output;
			}
			talloc_free(output);
			close(io_fd);
			io_fd = -1;
		}

		if (msg_fd != -1 && FD_ISSET(msg_fd, &fdset)) {
			if (!handle_userspace_message(status)) {
				close(msg_fd);
				msg_fd = -1;
			}
		}
	}

	/* If other end didn't tell us status, maybe direct child? */
	if (*status == -1)
		if (waitpid(pid, status, 0) == -1)
			*status = -1;
	pid = -1;
	return NULL;
}
