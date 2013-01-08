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

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <poll.h>
#include <time.h>

#include "umtsd.h"
#include "config.h"

static volatile int signaled = 0;
static struct umts_state state = {.uciname = "network", .profile = "wan"};
int verbose = 0;


static int umts_usage(const char *app) {
	fprintf(stderr,
			"umtsd - UMTS connection manager\n"
			"(c) 2010 Steven Barth, John Crispin\n\n"
			"Usage: %s [options] [params...]\n\n"
			"Command Options and Parameters:\n"
			"	-c			Connect using modem (default)\n"
			"	-s			Scan modem and reset state file\n"
			"	-u			Same as scan but also try to unlock SIM\n"
			" 	-p <PUK> <PIN>		Reset PIN of locked SIM using PUK\n"
			"	-d			Dial (used internally)\n\n"
			"Global Options:\n"
			"	-e			Don't write error state\n"
			"	-n <name>		Use given profile instead of \"wan\"\n"
			"	-v			Increase verbosity\n\n"
			"Connect Options:\n"
			"	-t			Test state file for previous SIM-unlocking\n"
			"				errors before attempting to connect\n\n"
			"Return Codes:\n"
			"	0			OK\n"
			"	1			Syntax error\n"
			"	2			Internal error\n"
			"   	3			Terminated by signal\n"
			"	4			Unknown modem\n"
			"	5			Modem error\n"
			"	6			SIM error\n"
			"	7			SIM unlocking error (PIN failed etc.)\n"
			"	8			Dialing error\n"
			"	9			PPP auth error\n"
			"   	10			Generic PPP error\n"
			"   	11			Network error\n",
			app);
	return UMTS_EINVAL;
}

static void umts_catch_signal(int signal) {
	if (!signaled) signaled = signal;
}

// Signal safe cleanup function
static void umts_cleanup_safe(int signal) {
	if (state.ctlfd > 0) {
		close(state.ctlfd);
		state.ctlfd = -1;
	}
	if (signal) {
		state.flags |= UMTS_FLAG_SIGNALED;
	}
}

static void umts_cleanup() {
	if (state.uci) {
		ucix_cleanup(state.uci);
		state.uci = NULL;
	}
	umts_cleanup_safe(0);
}

static void umts_exitcode(int code) {
	if (code && state.flags & UMTS_FLAG_SIGNALED)
		code = UMTS_ESIGNALED;
	if (code && code != UMTS_ESIGNALED && !(state.flags & UMTS_FLAG_NOERRSTAT))
		umts_config_set_int(&state, "umts_error", code);
	ucix_save(state.uci, state.uciname);
	exit(code);
}

static void sleep_seconds(int seconds) {
	const struct timespec ts = {.tv_sec = seconds};
	nanosleep(&ts, NULL);
}

int main(int argc, char *const argv[]) {
	enum umts_app {
		UMTS_APP_CONNECT, UMTS_APP_SCAN,
		UMTS_APP_UNLOCK, UMTS_APP_DIAL,
		UMTS_APP_PINPUK
	} app = UMTS_APP_CONNECT;
	char *appname = "umtsd"; /* for syslog */

	int s;
	while ((s = getopt(argc, argv, "csupden:vt")) != -1) {
		switch(s) {
			case 'c':
				app = UMTS_APP_CONNECT;
				break;

			case 's':
				app = UMTS_APP_SCAN;
				break;

			case 'u':
				app = UMTS_APP_UNLOCK;
				break;

			case 'p':
				app = UMTS_APP_PINPUK;
				break;

			case 'd':
				app = UMTS_APP_DIAL;
				appname = "umtsd-dialer";
				break;

			case 'e':
				state.flags |= UMTS_FLAG_NOERRSTAT;
				break;

			case 'n':
				strncpy(state.profile, optarg, sizeof(state.profile) - 1);
				break;

			case 'v':
				verbose++;
				break;

			case 't':
				state.flags |= UMTS_FLAG_TESTSTATE;
				break;

			default:
				return umts_usage(argv[0]);
		}
	}

	openlog(appname, LOG_PID | LOG_PERROR, LOG_USER);


	if (!verbose)
		setlogmask(LOG_UPTO(LOG_NOTICE));

	// Prepare and initialize state
	if (!(state.uci = ucix_init(state.uciname, 1))) {
		return UMTS_EINTERNAL;
	}
	atexit(umts_cleanup);

	//Setup signals
	struct sigaction sa = {
		.sa_handler = SIG_IGN,
	};
	sigaction(SIGPIPE, &sa, NULL);
	sa.sa_handler = umts_cleanup_safe;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	// Dial only needs an active UCI context
	if (app == UMTS_APP_DIAL)
		return umts_dial_main(&state);

	if (app == UMTS_APP_CONNECT && state.flags & UMTS_FLAG_TESTSTATE) {
		if (umts_config_get_int(&state, "umts_error", UMTS_OK) == UMTS_EUNLOCK) {
			syslog(LOG_CRIT, "Aborting due to previous SIM unlocking failure. "
			"Please check PIN and rescan device before reconnecting.");
			exit(UMTS_EUNLOCK);
		}
	}

	// Reset state
	umts_config_revert(&state, "modem_name");
	umts_config_revert(&state, "modem_driver");
	umts_config_revert(&state, "modem_id");
	umts_config_revert(&state, "modem_mode");
	umts_config_revert(&state, "modem_gsm");
	umts_config_revert(&state, "simstate");
	if (!(state.flags & UMTS_FLAG_NOERRSTAT))
		umts_config_revert(&state, "umts_error");

	// Find modem
	char *basetty = umts_config_get(&state, "umts_basetty");
	if (basetty) {
		strncpy(state.modem.tty, basetty, sizeof(state.modem.tty) - 1);
		free(basetty);
	}
	if (umts_modem_find(&state.modem)) {
		syslog(LOG_CRIT, "Unknown modem");
		umts_exitcode(UMTS_EDEVICE);
	}
	char b[512] = {0}, *saveptr;
	snprintf(b, sizeof(b), "%04x:%04x", state.modem.vendor, state.modem.device);
	syslog(LOG_NOTICE, "%s: Found %s modem %s", state.modem.tty,
			state.modem.driver, b);
	umts_config_set(&state, "modem_id", b);
	umts_config_set(&state, "modem_driver", state.modem.driver);

	// Writing modestrings
	b[0] = 0;
	const struct umts_config *cfg = state.modem.cfg;
	for (size_t i = 0; i < sizeof(cfg->modecmd)/sizeof(*cfg->modecmd); ++i)
		if (cfg->modecmd[i]) {
			umts_config_append(&state, "modem_mode", umts_modem_modestr(i));
			strncat(b, umts_modem_modestr(i), sizeof(b) - strlen(b) - 2);
			strcat(b, " ");
		}
	syslog(LOG_NOTICE, "%s: Supported modes: %s", state.modem.tty, b);


	// Open control connection
	char ttypath[24];
	umts_tty_calc(state.modem.tty, state.modem.cfg->ctlidx, ttypath);
	if ((state.ctlfd = umts_tty_cloexec(umts_tty_open(ttypath))) == -1) {
		syslog(LOG_CRIT, "%s: Unable to open terminal", state.modem.tty);
		umts_exitcode(UMTS_EMODEM);
	}

	// Hangup modem, disable echoing
	tcflush(state.ctlfd, TCIFLUSH);
	umts_tty_put(state.ctlfd, "ATE0\r");
	umts_tty_get(state.ctlfd, b, sizeof(b), 2500);
	tcflush(state.ctlfd, TCIFLUSH);

	// Identify modem
	if (umts_tty_put(state.ctlfd, "AT+CGMI;+CGMM\r") < 1
	|| umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_CRIT, "%s: Unable to identify modem (%s)", state.modem.tty, b);
		umts_exitcode(UMTS_EMODEM);
	}
	char *mi = strtok_r(b, "\r\n", &saveptr);
	char *mm = strtok_r(NULL, "\r\n", &saveptr);
	if (mi && mm) {
		mi = strdup(mi);
		mm = strdup(mm);
		snprintf(b, sizeof(b), "%s %s", mi, mm);
		syslog(LOG_NOTICE, "%s: Identified as %s", state.modem.tty, b);
		umts_config_set(&state, "modem_name", b);
		free(mi);
		free(mm);
	}

	char *c = NULL;
	// Getting SIM state
	tcflush(state.ctlfd, TCIFLUSH);
	if (umts_tty_put(state.ctlfd, "AT+CPIN?\r") < 1
	|| umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK
	|| !(c = strtok_r(b, "\r\n", &saveptr))) {
		syslog(LOG_CRIT, "%s: Unable to get SIM status (%s)", state.modem.tty, b);
		umts_config_set(&state, "simstate", "error");
		umts_exitcode(UMTS_ESIM);
	}

	// Evaluate SIM state
	int simstate;
	if (!strcmp(c, "+CPIN: READY")) {
		syslog(LOG_NOTICE, "%s: SIM card is ready", state.modem.tty);
		umts_config_set(&state, "simstate", "ready");
		simstate = 0;
	} else if (!strcmp(c, "+CPIN: SIM PIN")) {
		umts_config_set(&state, "simstate", "wantpin");
		simstate = 1;
	} else if (!strcmp(c, "+CPIN: SIM PUK")) {
		syslog(LOG_WARNING, "%s: SIM requires PUK!", state.modem.tty);
		umts_config_set(&state, "simstate", "wantpuk");
		simstate = 2;
	} else {
		syslog(LOG_CRIT, "%s: Unknown SIM status (%s)", state.modem.tty, c);
		umts_config_set(&state, "simstate", "error");
		simstate = -1;
		umts_exitcode(UMTS_ESIM);
	}

	if (app == UMTS_APP_SCAN) {
		umts_exitcode(UMTS_OK); // We are done here.
	} else if (app == UMTS_APP_PINPUK) {
		// Reset PIN with PUK
		if (simstate != 2)
			umts_exitcode(UMTS_ESIM);
		// Need two arguments
		if (optind + 2 != argc) {
			syslog(LOG_CRIT, "%s: Need exactly two arguments for -p", state.modem.tty);
			umts_exitcode(UMTS_EINVAL);
		}

		// Prepare command
		const char *puk = argv[optind];
		const char *pin = argv[optind+1];
		if (strpbrk(pin, "\"\r\n;") || strpbrk(puk, "\"\r\n;"))
			umts_exitcode(UMTS_EINVAL);
		snprintf(b, sizeof(b), "AT+CPIN=\"%s\",\"%s\"\r", puk, pin);

		// Send command
		tcflush(state.ctlfd, TCIFLUSH);
		if (umts_tty_put(state.ctlfd, b) >= 0
		&& umts_tty_get(state.ctlfd, b, sizeof(b), 2500) == UMTS_AT_OK) {
			syslog(LOG_NOTICE, "%s: PIN reset successful", state.modem.tty);
			umts_config_set(&state, "simstate", "ready");
			umts_exitcode(UMTS_OK);
		} else {
			syslog(LOG_CRIT, "%s: Failed to reset PIN (%s)", state.modem.tty, b);
			umts_exitcode(UMTS_EUNLOCK);
		}
	}

	if (simstate == 2) {
		umts_exitcode(UMTS_EUNLOCK);
	} else if (simstate == 1) {
		//Try unlocking with PIN
		char *pin = umts_config_get(&state, "umts_pin");
		if (!pin) {
			syslog(LOG_CRIT, "%s: PIN missing", state.modem.tty);
			umts_exitcode(UMTS_EUNLOCK);
		}
		if (strpbrk(pin, "\"\r\n;"))
			umts_exitcode(UMTS_EINVAL);
		snprintf(b, sizeof(b), "AT+CPIN=\"%s\"\r", pin);
		free(pin);

		// Send command
		tcflush(state.ctlfd, TCIFLUSH);
		if (umts_tty_put(state.ctlfd, b) < 0
		|| umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
			syslog(LOG_CRIT, "%s: PIN rejected (%s)", state.modem.tty, b);
			umts_exitcode(UMTS_EUNLOCK);
		}
		syslog(LOG_NOTICE, "%s: PIN accepted", state.modem.tty);
		umts_config_set(&state, "simstate", "ready");

		// Wait a few seconds for the dongle to find a carrier.
		// Some dongles apparently do not send a NO CARRIER reply to the
		// dialing, but instead hang up directly after sending a CONNECT
		// reply (Alcatel X060S / 1bbb:0000 showed this problem).
		sleep_seconds(5);
	}

	if (app == UMTS_APP_UNLOCK)
		umts_exitcode(UMTS_OK); // We are done here.


	int is_gsm = 0;
	if (umts_tty_put(state.ctlfd, "AT+GCAP\r") >= 0
	&& umts_tty_get(state.ctlfd, b, sizeof(b), 2500) == UMTS_AT_OK) {
		if (strstr(b, "CGSM")) {
			is_gsm = 1;
			umts_config_set(&state, "modem_gsm", "1");
			syslog(LOG_NOTICE, "%s: Detected a GSM modem", state.modem.tty);
		}
	}

	// verbose provider info
	if (umts_tty_put(state.ctlfd, "AT+CREG=2\r") < 1
	|| umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_CRIT, "%s: failed to set verbose provider info (%s)", state.modem.tty, b);
	}

	// Setting network mode if GSM
	if (is_gsm) {
		char *m = umts_config_get(&state, "umts_mode");
		enum umts_mode mode = umts_modem_modeval((m) ? m : "auto");
		if (mode == -1 || !state.modem.cfg->modecmd[mode]) {
			syslog(LOG_CRIT, "%s: Unsupported mode %s", state.modem.tty, m);
			free(m);
			umts_exitcode(UMTS_EINVAL);
		}
		tcflush(state.ctlfd, TCIFLUSH);
		if (state.modem.cfg->modecmd[mode][0]
		&& (umts_tty_put(state.ctlfd, state.modem.cfg->modecmd[mode]) < 0
		|| umts_tty_get(state.ctlfd, b, sizeof(b), 5000) != UMTS_AT_OK)) {
			syslog(LOG_CRIT, "%s: Failed to set mode %s (%s)",
				state.modem.tty, (m) ? m : "auto", b);
			free(m);
			umts_exitcode(UMTS_EMODEM);
		}
		syslog(LOG_NOTICE, "%s: Mode set to %s", state.modem.tty, (m) ? m : "auto");
		free(m);
	} else {
		syslog(LOG_NOTICE, "%s: Skipped setting mode on non-GSM modem", state.modem.tty);
	}

	// Save state
	umts_config_set_int(&state, "pid", getpid());
	ucix_save(state.uci, state.uciname);

	// Block and unbind signals so they won't interfere
	sa.sa_handler = umts_catch_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	// Start pppd to dial
	if (!(state.pppd = umts_tty_pppd(&state)))
		umts_exitcode(UMTS_EINTERNAL);

	int status = -1;
	int logsteps = 4;	// Report RSSI / BER to syslog every LOGSTEPS intervals
	char provider[64] = {0};

	// Main loop, wait for termination, measure signal strength
	while (!signaled) {
		// First run
		if (!++status) {
			umts_config_set(&state, "connected", "1");
			ucix_save(state.uci, state.uciname);
		} else {
			sleep_seconds(15);
			if (signaled) break;
		}

		// Query provider and RSSI / BER
		tcflush(state.ctlfd, TCIFLUSH);
/*		umts_tty_put(state.ctlfd, "AT+CREG?\r");
		umts_tty_get(state.ctlfd, b, sizeof(b), 2500);
		printf("%s:%s[%d]%s\n", __FILE__, __func__, __LINE__, b);
*/
		umts_tty_put(state.ctlfd, "AT+COPS?;+CSQ\r");
		if (umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK)
			continue;

		char *cops = strtok_r(b, "\r\n", &saveptr);
		char *csq = strtok_r(NULL, "\r\n", &saveptr);

		if (cops && (cops = strchr(cops, '"')) // +COPS: 0,0,"FONIC",2
		&& (cops = strtok_r(cops, "\"", &saveptr))
		&& strncmp(cops, provider, sizeof(provider) - 1)) {
			syslog(LOG_NOTICE, "%s: Provider is %s",
				state.modem.tty, cops);
			umts_config_revert(&state, "provider");
			umts_config_set(&state, "provider", cops);
			strncpy(provider, cops, sizeof(provider) - 1);
		}

		if (csq && (csq = strtok_r(csq, " ,", &saveptr))
		&& (csq = strtok_r(NULL, " ,", &saveptr))) {	// +CSQ: 14,99
			// RSSI
			umts_config_revert(&state, "rssi");
			umts_config_set(&state, "rssi", csq);
			if ((status % logsteps) == 0)
				syslog(LOG_NOTICE, "%s: RSSI is %s",
					state.modem.tty, csq);
		}
		ucix_save(state.uci, state.uciname);
	}
	syslog(LOG_NOTICE, "Received signal %d, disconnecting", signaled);

	umts_config_revert(&state, "pid");
	umts_config_revert(&state, "connected");
	umts_config_revert(&state, "provider");
	umts_config_revert(&state, "rssi");

	// Terminate active connection by hanging up and resetting
	umts_tty_put(state.ctlfd, "ATH;&F\r");
	if (waitpid(state.pppd, &status, WNOHANG) != state.pppd) {
		kill(state.pppd, SIGTERM);
		waitpid(state.pppd, &status, 0);
		syslog(LOG_NOTICE, "%s: Terminated by signal %i",
				state.modem.tty, signaled);
		umts_exitcode(UMTS_ESIGNALED);
	}

	if (WIFSIGNALED(status) || WEXITSTATUS(status) == 5) {
		// pppd was termined externally, we won't treat this as an error
		syslog(LOG_NOTICE, "%s: pppd terminated by signal", state.modem.tty);
		umts_exitcode(UMTS_ESIGNALED);
	}

	switch (WEXITSTATUS(status)) {	// Exit codes from pppd (man pppd)
		case 7:
		case 16:
			syslog(LOG_CRIT, "%s: pppd modem error", state.modem.tty);
			umts_exitcode(UMTS_EMODEM);

		case 8:
			syslog(LOG_CRIT, "%s: pppd dialing error", state.modem.tty);
			umts_exitcode(UMTS_EDIAL);

		case 0:
		case 15:
			syslog(LOG_CRIT, "%s: terminated by network", state.modem.tty);
			umts_exitcode(UMTS_ENETWORK);

		case 19:
			syslog(LOG_CRIT, "%s: invalid PPP credentials", state.modem.tty);
			umts_exitcode(UMTS_EAUTH);

		default:
			syslog(LOG_CRIT, "%s: PPP error (%i)",
					state.modem.tty, WEXITSTATUS(status));
			umts_exitcode(UMTS_EPPP);
	}

	// This cannot happen
	return UMTS_EINTERNAL;
}

