/**
 *   umtsd - UMTS connection manager
 *   Copyright (C) 2010 Steven Barth <steven@midlink.org>
 *   Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <string.h>
#include <syslog.h>
#include "umtsd.h"
#include "config.h"

static const char *ttyresstr[] = {
	[UMTS_AT_OK] = "OK",
	[UMTS_AT_CONNECT] = "CONNECT",
	[UMTS_AT_ERROR] = "ERROR",
	[UMTS_AT_CMEERROR] = "+CME ERROR",
	[UMTS_AT_NODIALTONE] = "NO DIALTONE",
	[UMTS_AT_BUSY] = "BUSY",
	[UMTS_AT_NOCARRIER] = "NO CARRIER",
};

int umts_tty_open(const char *tty) {
	struct termios tio;
	int fd = open(tty, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;

	tcgetattr(fd, &tio);
	tio.c_cflag |= CREAD;
	tio.c_cflag |= CS8;
	tio.c_iflag |= IGNPAR;
	tio.c_lflag &= ~(ICANON);
	tio.c_lflag &= ~(ECHO);
	tio.c_lflag &= ~(ECHOE);
	tio.c_lflag &= ~(ISIG);
	tio.c_cc[VMIN]=1;
	tio.c_cc[VTIME]=0;
	tcsetattr(fd, TCSANOW, &tio);

	return fd;
}

int umts_tty_put(int fd, const char *cmd) {
	if (write(fd, cmd, strlen(cmd)) != strlen(cmd))
		return -1;
	return strlen(cmd);
}

// Retrieve answer from modem
enum umts_atres umts_tty_get(int fd, char *buf, size_t len, int timeout) {
	buf[0] = 0;
	struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLERR | POLLHUP};

	char *c = buf;
	size_t rem = len - 1;

	// Modems are evil, they might not send the complete answer when doing
	// a read, so we read until we get a known AT status code (see top)
	while (rem > 0) {
		if (poll(&pfd, 1, timeout) < 1) {
			errno = ETIMEDOUT;
			return -1;
		}

		ssize_t rxed = read(fd, c, rem);
		if (rxed < 1) return -1;

		rem -= rxed;
		*(c += rxed) = 0;

		// AT status codes end in \r(\n)
		if ((c[-1] != '\r' && c[-1] != '\n') || &c[-2] <= buf)
			continue;

		char *d = &c[-2];
		// Go to \r(\n) before status
		while (d > buf && d[-1] != '\r' && d[-1] != '\n')
			--d;

		if (*d == '^') { // Async signal (we don't want this)
			if (d - 1 >= buf && d[-1] == '\r') {
				d--;
			} else if (d - 2 >= buf && d[-2] == '\r') {
				d -= 2;
			}
			*d = 0;
			c = d;
			rem = len - 1 - (d - buf);
			continue;
		}

		// Compare with known AT status codes (array at the very top)
		for (size_t i = 0; i < sizeof(ttyresstr) / sizeof(*ttyresstr); ++i)
			if (!strncmp(ttyresstr[i], d, strlen(ttyresstr[i])))
				return i;
	}

	errno = ERANGE;
	return -1;
}

// Calculate actual control and data tty from basetty + index
char* umts_tty_calc(const char *basetty, uint8_t index, char buf[static 24]) {
	const char *c;
	for (c = basetty; *c && (*c < '0' || *c > '9'); ++c);
	const size_t slen = c - basetty;
	if (slen > 15) return NULL;
	memcpy(buf, "/dev/", 5);
	memcpy(buf + 5, basetty, slen);
	snprintf(buf + 5 + slen, 24 - 5 - slen, "%i", atoi(c) + index);
	return buf;
}

int umts_tty_cloexec(int fd) {
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	return fd;
}

pid_t umts_tty_pppd(struct umts_state *state) {
	char cpath[16 + sizeof(state->profile)] = "/tmp/umtsd-pppd-";
	strcat(cpath, state->profile);
	unlink(cpath);

	// Create config file
	FILE *fp;
	int cfd = open(cpath, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (cfd < 0 || !(fp = fdopen(cfd, "w"))) {
		close(cfd);
		return 0;
	}

	char buf[256];

	fputs(umts_tty_calc(state->modem.tty, state->modem.cfg->datidx, buf), fp);
	fputs("\n460800\ncrtscts\nlock\n"
		"noauth\nnoipdefault\nnovj\nnodetach\n", fp);

	// We need to pass ourselve as connect script so get our path from /proc
	memcpy(buf, "connect \"", 9);
	ssize_t l = readlink("/proc/self/exe", buf + 9, sizeof(buf) - 10);
	snprintf(buf + 9 + l, sizeof(buf) - 9 - l, " -dn%s\"\n", state->profile);
	fputs(buf, fp);

	// Set linkname and ipparam
	fprintf(fp, "linkname \"%s\"\nipparam \"%s\"\n", state->profile, state->profile);

	// UCI to pppd-cfg
	int val;
	if ((val = umts_config_get_int(state, "defaultroute", 1)) != 0) {
		fputs("defaultroute\n", fp);
	}
	if ((val = umts_config_get_int(state, "replacedefaultroute", 0)) != 0) {
		fputs("replacedefaultroute\n", fp);
	}
	if ((val = umts_config_get_int(state, "usepeerdns", 1)) != 0) {
		fputs("usepeerdns\n", fp);
	}
	if ((val = umts_config_get_int(state, "persist", 1)) != 0) {
		fputs("persist\n", fp);
	}
	if ((val = umts_config_get_int(state, "unit", -1)) > 0) {
		fprintf(fp, "unit %i\n", val);
	}
	if ((val = umts_config_get_int(state, "maxfail", 1)) >= 0) {
		fprintf(fp, "maxfail %i\n", val);
	}
	if ((val = umts_config_get_int(state, "holdoff", 0)) >= 0) {
		fprintf(fp, "holdoff %i\n", val);
	}
	if ((val = umts_config_get_int(state, "umts_mtu", -1)) > 0) {
		fprintf(fp, "mtu %i\nmru %i\n", val, val);
	}

	fprintf(fp, "lcp-echo-failure 12\n");

	char *s;
	s = umts_config_get(state, "umts_user");
	fprintf(fp, "user \"%s\"\n", (s && !strpbrk(s, "\"\r\n")) ? s : "");
	free(s);

	s = umts_config_get(state, "umts_pass");
	fprintf(fp, "password \"%s\"\n", (s && !strpbrk(s, "\"\r\n")) ? s : "");
	free(s);

	// Additional parameters
	struct list_head opts = LIST_HEAD_INIT(opts);
	umts_config_get_list(state, "umts_pppdopt", &opts);
	struct ucilist *p, *p2;
	list_for_each_entry_safe(p, p2, &opts, list) {
		fputs(p->val, fp);
		fputc('\n', fp);
		list_del(&p->list);
		free(p->val);
		free(p);
	}
	fclose(fp);

	char *const argv[] = {"/usr/sbin/pppd", "file", cpath, NULL};
	pid_t pid = vfork();
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(128);
	} else if (pid == -1) {
		return 0;
	} else {
		return pid;
	}
}
