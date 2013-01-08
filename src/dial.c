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

#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include "umtsd.h"
#include "config.h"

int umts_dial_main(struct umts_state *state) {
	char *tty = ttyname(0);
	if (tty && (tty = strrchr(tty, '/')))
		tty++;

	tcflush(0, TCIFLUSH); // Skip crap

	char b[512];

	// Reset, unecho, ...
	umts_tty_put(1, "ATE0\r");
	if (umts_tty_get(0, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_ERR, "%s: Error disabling echo (%s)",
				tty, (b[0]) ? b : strerror(errno));
		return UMTS_EDIAL;
	}

	// Reset, unecho, ...
	umts_tty_put(1, "ATH\r");
	if (umts_tty_get(0, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_ERR, "%s: Error resetting modem (%s)",
				tty, (b[0]) ? b : strerror(errno));
		return UMTS_EDIAL;
	}

	// Set PDP and APN
	char *apn = umts_config_get(state, "umts_apn");
	snprintf(b, sizeof(b), "AT+CGDCONT=1,\"IP\",\"%s\"\r",
		(apn && !strpbrk(apn, "\"\r\n;")) ? apn : "");

	umts_tty_put(1, b);
	if (umts_tty_get(0, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_ERR, "%s: Failed to set APN (%s)",
				tty, (b[0]) ? b : strerror(errno));
		return UMTS_EDIAL;
	}
	syslog(LOG_NOTICE, "%s: Selected APN %s. Now dialing...", tty, apn);
	free(apn);

	// Dial
	enum umts_atres res = UMTS_AT_NOCARRIER;
	for (int i = 0; i < 9; ++i) { // Wait 9 * 5s for network
		tcflush(0, TCIFLUSH);
		umts_tty_put(1, "ATD*99#\r");
		if ((res = umts_tty_get(0, b, sizeof(b), 10000)) != UMTS_AT_NOCARRIER)
			break;
		syslog(LOG_NOTICE, "%s: No carrier. Waiting for network...", tty);
		sleep(5);
	}

	if (res != UMTS_AT_CONNECT) {
		syslog(LOG_ERR, "%s: Failed to connect (%s)", tty,
				(b[0]) ? b : strerror(errno));
		return UMTS_EDIAL;
	}

	syslog(LOG_NOTICE, "%s: Connected. Handover to pppd.", tty);
	return UMTS_OK;
}
