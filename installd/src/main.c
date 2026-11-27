/*
 * pc-installd: the LineageOS for PCs installer's root service. The PcInstaller app talks to it over
 * a Unix socket with the line protocol in README.md.
 *
 *   pc-installd                             serve on init's socket (/dev/socket/pc_installd)
 *   pc-installd --socket PATH [--root DIR]  serve on PATH; DIR replaces /sys, /dev/block, ... (host trials)
 *   pc-installd --cmdline FOLDER EXTRA      print an installed system's kernel command line
 *   pc-installd --grub-entry FOLDER ID TITLE [previous]  print its GRUB menu entry
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include "grubcfg.h"
#include "proto.h"
#include "server.h"
#include "util.h"

/* ---- serving -------------------------------------------------------------------------- */

static void send_reply(void *ctx, const char *line)
{
	int fd = *(int *)ctx;
	send(fd, line, strlen(line), MSG_NOSIGNAL);
}

/* Only root and the system app of any user may drive the installer (and, on a host, its own user). */
static int is_trusted_peer(int fd)
{
	struct ucred cred;
	socklen_t len = sizeof cred;
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len))
		return 0;
	return is_trusted_uid(cred.uid) || cred.uid == getuid();
}

#define MAX_PASSED_FDS 4

static void replace_fd(int *pending, int fd)
{
	if (*pending >= 0)
		close(*pending);
	*pending = fd;
}

/* A package's descriptor comes with its command line (SCM_RIGHTS on the same sendmsg). */
static void take_fds(struct msghdr *mh, int *pending)
{
	for (struct cmsghdr *c = CMSG_FIRSTHDR(mh); c; c = CMSG_NXTHDR(mh, c)) {
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
			continue;
		size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
		for (size_t i = 0; i < count; i++) {
			int fd;
			memcpy(&fd, CMSG_DATA(c) + i * sizeof fd, sizeof fd);
			replace_fd(pending, fd);
		}
	}
}

static ssize_t receive(int fd, char *buf, size_t len, int *pending)
{
	char control[CMSG_SPACE(sizeof(int) * MAX_PASSED_FDS)];
	struct iovec iov = { buf, len };
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof control };
	ssize_t n = recvmsg(fd, &mh, MSG_CMSG_CLOEXEC);
	if (n >= 0)
		take_fds(&mh, pending);
	return n;
}

/* The descriptor that came with a line belongs to that command and is closed after it. */
static void serve_client(struct server *s, int fd)
{
	char buf[LINE_MAX_LEN];
	size_t len = 0;
	ssize_t n;
	int pending = -1;
	while ((n = receive(fd, buf + len, sizeof buf - 1 - len, &pending)) > 0) {
		len += (size_t)n;
		buf[len] = 0;
		char *nl;
		while ((nl = strchr(buf, '\n'))) {
			*nl = 0;
			handle_line_with_fd(s, buf, pending, send_reply, &fd);
			replace_fd(&pending, -1);
			len -= (size_t)(nl + 1 - buf);
			memmove(buf, nl + 1, len + 1);
		}
		len = len == sizeof buf - 1 ? 0 : len; /* drop an overlong line */
	}
	replace_fd(&pending, -1);
}

static int serve(int listener, struct server *s)
{
	for (;;) {
		int fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
		if (fd < 0 && errno == EINTR)
			continue;
		if (fd < 0)
			return 1;
		if (is_trusted_peer(fd))
			serve_client(s, fd);
		close(fd);
	}
}

/* init creates and binds /dev/socket/pc_installd (pc-installd.rc); listening is the service's job. */
static int open_init_socket(void)
{
	const char *env = getenv("ANDROID_SOCKET_pc_installd");
	int fd = env ? atoi(env) : -1;
	return fd >= 0 && !listen(fd, 4) ? fd : -1;
}

static int open_path_socket(const char *path)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	unlink(path);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 4))
		return -1;
	return fd;
}

/* ---- host trials: --root DIR ---------------------------------------------------------- */

static struct sys_env build_root_env(const char *root)
{
	static char paths[7][512];
	static const char *const RELS[] = { "sys", "dev/block", "efivars", "work", "mountinfo", "cmdline", "build.prop" };
	for (int i = 0; i < 7; i++)
		snprintf(paths[i], sizeof paths[i], "%s/%s", root, RELS[i]);
	return (struct sys_env){ paths[0], paths[1], paths[2], paths[3], paths[4], paths[5], "mke2fs", paths[6] };
}

static const char *find_option(int argc, char **argv, const char *name)
{
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], name))
			return argv[i + 1];
	return NULL;
}

static int run_server(int argc, char **argv)
{
	static struct server s;
	static struct sys_env root_env;
	const char *path = find_option(argc, argv, "--socket");
	const char *root = find_option(argc, argv, "--root");
	const struct sys_env *env = linux_sys_env();
	if (root) {
		root_env = build_root_env(root);
		env = &root_env;
	}
	int listener = path ? open_path_socket(path) : open_init_socket();
	if (listener < 0) {
		fprintf(stderr, "pc-installd: no socket (%s)\n", strerror(errno));
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	server_init(&s, env, linux_sys_ops());
	return serve(listener, &s);
}

/* ---- printing modes ------------------------------------------------------------------- */

static int print_cmdline(int argc, char **argv)
{
	(void)argc;
	char out[1024];
	build_kernel_cmdline(out, sizeof out, argv[2], argv[3]);
	puts(out);
	return 0;
}

static int print_grub_entry(int argc, char **argv)
{
	char entry[8192];
	struct entry_params p = { argv[2], argv[3], argv[4], argc > 5 && !strcmp(argv[5], "previous") ? PREVIOUS_SYSTEM : 0 };
	if (build_install_entry(entry, sizeof entry, &p))
		return 1;
	fputs(entry, stdout);
	return 0;
}

static const struct {
	const char *flag;
	int min_args;
	int (*run)(int, char **);
} MODES[] = {
	{ "--cmdline", 4, print_cmdline },
	{ "--grub-entry", 5, print_grub_entry },
};

int main(int argc, char **argv)
{
	for (size_t i = 0; i < sizeof MODES / sizeof *MODES; i++)
		if (argc >= MODES[i].min_args && !strcmp(argv[1], MODES[i].flag))
			return MODES[i].run(argc, argv);
	return run_server(argc, argv);
}
