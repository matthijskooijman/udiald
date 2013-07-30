/**
 *   udiald - UMTS connection manager
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
#include <stdarg.h>
#include <errno.h>
#include <termios.h>
#include "udiald.h"
#include "config.h"

static void fatal_error(struct udiald_state *state, const char *fmt, ...) {
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, lengthof(buf), fmt, ap);
	va_end(ap);

	syslog(LOG_ERR, "%s", buf);
	udiald_config_set(state, "udiald_dial_error_msg", buf);
	ucix_save(state->uci, state->uciname);
}

int udiald_dial_main(struct udiald_state *state) {
	udiald_select_modem(state);

	char *tty = ttyname(0);
	if (tty && (tty = strrchr(tty, '/')))
		tty++;

	tcflush(0, TCIFLUSH); // Skip crap

	char b[512];
	struct udiald_tty_read r;

	// Reset, unecho, ...
	syslog(LOG_NOTICE, "%s: Preparing to dial", tty);
	udiald_tty_put(1, "ATE0\r");
	if (udiald_tty_get(0, &r, NULL, 2500) != UDIALD_AT_OK) {
		fatal_error(state, "%s: Error disabling echo (%s)",
				   tty, (b[0]) ? b : strerror(errno));
		return UDIALD_EDIAL;
	}
	syslog(LOG_NOTICE, "%s: Echo disabled", tty);

	// Reset, unecho, ...
	udiald_tty_put(1, "ATH\r");
	if (udiald_tty_get(0, &r, NULL, 2500) != UDIALD_AT_OK) {
		fatal_error(state, "%s: Error resetting modem (%s)",
				   tty, (b[0]) ? b : strerror(errno));
		return UDIALD_EDIAL;
	}
	syslog(LOG_NOTICE, "%s: Modem reset", tty);

	// Set PDP and APN
	char *apn = udiald_config_get(state, "udiald_apn");

	if (!apn)
		apn = "";

	char *invalid = strpbrk(apn, "\"\r\n;");
	if (invalid) {
		if (invalid[0] == '\r')
			invalid = "\r";
		else if (invalid[0] == '\n')
			invalid = "\n";
		else
			invalid[1] = '\0';

		fatal_error(state,  "%s: Invalid character in APN: '%s'",
				    tty, invalid);
		return UDIALD_EDIAL;
	}

	snprintf(b, sizeof(b), "AT+CGDCONT=1,\"IP\",\"%s\"\r", apn);

	if (!*apn)
		syslog(LOG_WARNING, "%s: No apn configured, connection might not work", tty);

	udiald_tty_put(1, b);
	if (udiald_tty_get(0, &r, NULL, 2500) != UDIALD_AT_OK) {
		fatal_error(state,  "%s: Failed to set APN (%s)",
				    tty, r.lines ? udiald_tty_flatten_result(&r) : strerror(errno));
		return UDIALD_EDIAL;
	}
	syslog(LOG_NOTICE, "%s: Selected APN \"%s\". Now dialing...", tty, apn);
	free(apn);

	// Dial
	enum udiald_atres res = UDIALD_AT_NOCARRIER;
	for (int i = 0; i < 9; ++i) { // Wait 9 * 5s for network
		tcflush(0, TCIFLUSH);
		// Linux Driver 4.19.19.00 Tool User Guide.pdf inside
		// HUAWEI Data Cards Linux Driver suggests that ATD*99#
		// should generally work for WCDMA and GSM, but ATD#777
		// is needed for CDMA (EVDO). Alternatively,
		// AT+GCDATA="PPP",1 (where 1 is the PDP profile set up
		// wit CGDCONT) is also said to be the official connect
		// command (ATD is legacy but possibly supported by more
		// modems).
		syslog(LOG_INFO, "%s: Using dial command: %s", tty, state->modem.profile->cfg.dialcmd);
		udiald_tty_put(1, state->modem.profile->cfg.dialcmd);
		res = udiald_tty_get(0, &r, NULL, 10000);
		if (res != UDIALD_AT_NOCARRIER && res != UDIALD_AT_OK)
			break;
		syslog(LOG_NOTICE, "%s: No carrier. Waiting for network...", tty);
		sleep(5);
	}

	if (res != UDIALD_AT_CONNECT) {
		fatal_error(state,  "%s: Failed to connect (%s)", tty,
				   r.lines ? udiald_tty_flatten_result(&r) : strerror(errno));
		return UDIALD_EDIAL;
	}

	udiald_config_set(state, "udiald_state", "connected");
	ucix_save(state->uci, state->uciname);

	syslog(LOG_NOTICE, "%s: Connected. Handover to pppd.", tty);
	return UDIALD_OK;
}
