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

#include "umtsd.h"
#include <glob.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>

#include "deviceconfig.h"

#define UMTS_TTY_SYSDIR "/sys/class/tty/"

static const char *modestr[] = {
	[UMTS_MODE_AUTO] = "auto",
	[UMTS_FORCE_UMTS] = "force-umts",
	[UMTS_FORCE_GPRS] = "force-gprs",
	[UMTS_PREFER_UMTS] = "prefer-umts",
	[UMTS_PREFER_GPRS] = "prefer-gprs"
};

// mode no -> mode string
const char* umts_modem_modestr(enum umts_mode mode) {
	return modestr[mode];
}

// mode string -> mode no
enum umts_mode umts_modem_modeval(const char *mode) {
	for (size_t i = 0; i < sizeof(modestr)/sizeof(*modestr); ++i)
		if (!strcmp(mode, modestr[i]))
			return i;
	return -1;
}

static int umts_modem_identify(struct umts_modem *modem) {
	char buffer[128] = UMTS_TTY_SYSDIR;
	char path[PATH_MAX];

	// Detect real device sysfs-path
	strcat(buffer, modem->tty);
	if (!realpath(buffer, path)) return -ENODEV;
	char *crap = strstr(path, "/tty");
	if (!crap || (sizeof(path) - (crap - path)) < 16) return -ENODEV;
	crap[0] = 0;

	// Open and read modalias line for tty device
	strcat(path, "/modalias");
	int fd = open(path, O_RDONLY);
	if (fd == -1) return -ENODEV;
	if (read(fd, buffer, sizeof(buffer) - 1)) {
		// Fix compiler warning
	}
	close(fd);

	// Get vendor / product id from modalias
	char *delim = strchr(buffer, ':');
	if (!delim) return -ENODEV;
	char *c = strchr(delim, 'v');
	if (!c) return -ENODEV;
	modem->vendor = strtol(c + 1, NULL, 16);
	c = strchr(c, 'p');
	if (!c || strlen(c) < 5) return -ENODEV;
	c[5] = 0;
	modem->device = strtol(c + 1, NULL, 16);

	// Get driver name from driver symlink
	strcpy(strstr(path, "/modalias"), "/driver");
	ssize_t llen = readlink(path, buffer, sizeof(buffer) - 1);
	if (llen < 0) return -ENODEV;
	buffer[llen] = 0;
	c = strrchr(buffer, '/');
	if (!c || strlen(c + 1) >= sizeof(modem->driver)) return -ENODEV;
	memcpy(modem->driver, c + 1, strlen(c + 1) + 1);

	// Find the first profile that has all of its conditions
	// matching. The array is ordered so that specific devices are
	// matched first, then generic per-vendor profiles and then
	// generic per-driver profiles.
	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i)
		if ((profiles[i].vendor == modem->vendor || !profiles[i].vendor)
		&& (profiles[i].device == modem->device || !profiles[i].device)
		&& (!profiles[i].driver || !strcmp(profiles[i].driver, modem->driver))) {
			modem->cfg = &profiles[i].cfg;
			return 0;
		}


	return -ENODEV;
}

int umts_modem_find(struct umts_modem *modem) {
	if (!modem->tty[0]) {	// Auto-detect modem base tty
		glob_t gl;
		const char search1[] = UMTS_TTY_SYSDIR"ttyACM*";
		const char search2[] = UMTS_TTY_SYSDIR"ttyHSO*";
		const char search3[] = UMTS_TTY_SYSDIR"ttyUSB*";
		modem->cfg = NULL;

		// glob all relevant tty-devices
		int s1 = glob(search1, 0, NULL, &gl);
		if (s1 && s1 != GLOB_NOMATCH) return -ENODEV;
		int s2 = glob(search2, (!s1) ? GLOB_APPEND : 0, NULL, &gl);
		if (s1 && s2 && s2 != GLOB_NOMATCH) return -ENODEV;
		int s3 = glob(search3, (!s1 || !s2) ? GLOB_APPEND : 0, NULL, &gl);
		if (s1 && s2 && s3) return -ENODEV;


		for (size_t i = 0; i < gl.gl_pathc; ++i) {
			char *c = strrchr(gl.gl_pathv[i], '/') + 1;
			if (strlen(c) < sizeof(modem->tty)) {
				memcpy(modem->tty, c, strlen(c) + 1);
				if (!umts_modem_identify(modem))
					break;
			}
		}

		globfree(&gl);
		if (modem->cfg) {
			syslog(LOG_NOTICE, "%s: Using control tty %d", modem->tty, modem->cfg->ctlidx);
			syslog(LOG_NOTICE, "%s: Using data tty %d", modem->tty, modem->cfg->datidx);
		}

		return (modem->cfg) ? 0 : -ENODEV;
	} else {
		return umts_modem_identify(modem);
	}
}


