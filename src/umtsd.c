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

enum umts_app {
		UMTS_APP_CONNECT, UMTS_APP_SCAN,
		UMTS_APP_UNLOCK, UMTS_APP_DIAL,
		UMTS_APP_PINPUK
};

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

/**
 * Parse the commandline and return the selected app.
 */
static enum umts_app umts_parse_cmdline(struct umts_state *state, int argc, char * const argv[]) {
	enum umts_app app = UMTS_APP_CONNECT;

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
				break;

			case 'e':
				state->flags |= UMTS_FLAG_NOERRSTAT;
				break;

			case 'n':
				strncpy(state->profile, optarg, sizeof(state->profile) - 1);
				break;

			case 'v':
				verbose++;
				break;

			case 't':
				state->flags |= UMTS_FLAG_TESTSTATE;
				break;

			default:
				exit(umts_usage(argv[0]));
		}
	}

	return app;
}

static void umts_setup_syslog(struct umts_state *state, enum umts_app app) {
	char *appname = "umtsd";
	if (app == UMTS_APP_DIAL)
		appname = "umtsd-dialer";

	openlog(appname, LOG_PID | LOG_PERROR, LOG_USER);

	if (!verbose)
		setlogmask(LOG_UPTO(LOG_NOTICE));
}

static void umts_setup_uci(struct umts_state *state) {
	// Prepare and initialize state
	if (!(state->uci = ucix_init(state->uciname, 1))) {
		exit(UMTS_EINTERNAL);
	}
}

/**
 * Select the modem to use, depending on config or autodetection.
 */
static void umts_select_modem(struct umts_state *state) {
	// Find modem
	char *basetty = umts_config_get(state, "umts_basetty");
	if (basetty) {
		strncpy(state->modem.tty, basetty, sizeof(state->modem.tty) - 1);
		free(basetty);
	}
	/* Identify and/or autodetect the modem */
	if (umts_modem_find(&state->modem)) {
		syslog(LOG_CRIT, "Unknown modem");
		umts_exitcode(UMTS_EDEVICE);
	}
	char b[512] = {0};
	snprintf(b, sizeof(b), "%04x:%04x", state->modem.vendor, state->modem.device);
	syslog(LOG_NOTICE, "%s: Found %s modem %s", state->modem.tty,
			state->modem.driver, b);
	umts_config_set(state, "modem_id", b);
	umts_config_set(state, "modem_driver", state->modem.driver);

	b[0] = '\0';
	// Writing modestrings
	const struct umts_config *cfg = state->modem.cfg;
	for (size_t i = 0; i < sizeof(cfg->modecmd)/sizeof(*cfg->modecmd); ++i)
		if (cfg->modecmd[i]) {
			umts_config_append(state, "modem_mode", umts_modem_modestr(i));
			strncat(b, umts_modem_modestr(i), sizeof(b) - strlen(b) - 2);
			strcat(b, " ");
		}
	syslog(LOG_NOTICE, "%s: Supported modes: %s", state->modem.tty, b);
}

/**
 * Open the control connection, storing the fd in state->ctlfd.
 */
static void umts_open_control(struct umts_state *state) {
	// Open control connection
	char ttypath[24];
	umts_tty_calc(state->modem.tty, state->modem.cfg->ctlidx, ttypath);
	if ((state->ctlfd = umts_tty_cloexec(umts_tty_open(ttypath))) == -1) {
		syslog(LOG_CRIT, "%s: Unable to open terminal", state->modem.tty);
		umts_exitcode(UMTS_EMODEM);
	}
}

/**
 * Reset the modem through the control connection.
 */
static void umts_modem_reset(struct umts_state *state) {
	char b[512] = {0};
	// Hangup modem, disable echoing
	tcflush(state->ctlfd, TCIFLUSH);
	umts_tty_put(state->ctlfd, "ATE0\r");
	umts_tty_get(state->ctlfd, b, sizeof(b), 2500);
	tcflush(state->ctlfd, TCIFLUSH);
}

/**
 * Query the modem for identification.
 */
static void umts_identify(struct umts_state *state) {
	char b[512] = {0};
	// Identify modem
	if (umts_tty_put(state->ctlfd, "AT+CGMI;+CGMM\r") < 1
	|| umts_tty_get(state->ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_CRIT, "%s: Unable to identify modem (%s)", state->modem.tty, b);
		umts_exitcode(UMTS_EMODEM);
	}
	char *saveptr;
	char *mi = strtok_r(b, "\r\n", &saveptr);
	char *mm = strtok_r(NULL, "\r\n", &saveptr);
	if (mi && mm) {
		mi = strdup(mi);
		mm = strdup(mm);
		snprintf(b, sizeof(b), "%s %s", mi, mm);
		syslog(LOG_NOTICE, "%s: Identified as %s", state->modem.tty, b);
		umts_config_set(state, "modem_name", b);
		free(mi);
		free(mm);
	}
}

/**
 * Query the modem for its SIM status.
 */
static void umts_check_sim(struct umts_state *state) {
	char b[512] = {0};
	char *saveptr;
	char *c = NULL;
	// Getting SIM state
	tcflush(state->ctlfd, TCIFLUSH);
	if (umts_tty_put(state->ctlfd, "AT+CPIN?\r") < 1
	|| umts_tty_get(state->ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK
	|| !(c = strtok_r(b, "\r\n", &saveptr))) {
		syslog(LOG_CRIT, "%s: Unable to get SIM status (%s)", state->modem.tty, b);
		umts_config_set(state, "simstate", "error");
		umts_exitcode(UMTS_ESIM);
	}

	// Evaluate SIM state
	if (!strcmp(c, "+CPIN: READY")) {
		syslog(LOG_NOTICE, "%s: SIM card is ready", state->modem.tty);
		umts_config_set(state, "simstate", "ready");
		state->simstate = 0;
	} else if (!strcmp(c, "+CPIN: SIM PIN")) {
		umts_config_set(state, "simstate", "wantpin");
		state->simstate = 1;
	} else if (!strcmp(c, "+CPIN: SIM PUK")) {
		syslog(LOG_WARNING, "%s: SIM requires PUK!", state->modem.tty);
		umts_config_set(state, "simstate", "wantpuk");
		state->simstate = 2;
	} else {
		syslog(LOG_CRIT, "%s: Unknown SIM status (%s)", state->modem.tty, c);
		umts_config_set(state, "simstate", "error");
		state->simstate = -1;
		umts_exitcode(UMTS_ESIM);
	}
}

/**
 * Use the PUK code to reset the PIN.
 *
 * Can only be used when the device has locked itself down (due to
 * subsequent invalid PIN entries, for example).
 *
 * @param puk     The PUK to enter
 * @param pin     The new PIN code to set
 */
static void umts_enter_puk(struct umts_state *state, const char *puk, const char *pin) {
	// Reset PIN with PUK
	if (state->simstate != 2)
		umts_exitcode(UMTS_ESIM);

	// Prepare command
	char b[512] = {0};
	if (strpbrk(pin, "\"\r\n;") || strpbrk(puk, "\"\r\n;"))
		umts_exitcode(UMTS_EINVAL);
	snprintf(b, sizeof(b), "AT+CPIN=\"%s\",\"%s\"\r", puk, pin);

	// Send command
	tcflush(state->ctlfd, TCIFLUSH);
	if (umts_tty_put(state->ctlfd, b) >= 0
	&& umts_tty_get(state->ctlfd, b, sizeof(b), 2500) == UMTS_AT_OK) {
		syslog(LOG_NOTICE, "%s: PIN reset successful", state->modem.tty);
		umts_config_set(state, "simstate", "ready");
		umts_exitcode(UMTS_OK);
	} else {
		syslog(LOG_CRIT, "%s: Failed to reset PIN (%s)", state->modem.tty, b);
		umts_exitcode(UMTS_EUNLOCK);
	}
}

/**
 * Unlock the device using the PIN.
 *
 * The pincode to use is taken from the configuration.
 */
static void umts_enter_pin(struct umts_state *state) {
	//Try unlocking with PIN
	char *pin = umts_config_get(state, "umts_pin");
	char b[512] = {0};
	if (!pin) {
		syslog(LOG_CRIT, "%s: PIN missing", state->modem.tty);
		umts_exitcode(UMTS_EUNLOCK);
	}
	if (strpbrk(pin, "\"\r\n;"))
		umts_exitcode(UMTS_EINVAL);
	snprintf(b, sizeof(b), "AT+CPIN=\"%s\"\r", pin);
	free(pin);

	// Send command
	tcflush(state->ctlfd, TCIFLUSH);
	if (umts_tty_put(state->ctlfd, b) < 0
	|| umts_tty_get(state->ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_CRIT, "%s: PIN rejected (%s)", state->modem.tty, b);
		umts_exitcode(UMTS_EUNLOCK);
	}
	syslog(LOG_NOTICE, "%s: PIN accepted", state->modem.tty);
	umts_config_set(state, "simstate", "ready");

	// Wait a few seconds for the dongle to find a carrier.
	// Some dongles apparently do not send a NO CARRIER reply to the
	// dialing, but instead hang up directly after sending a CONNECT
	// reply (Alcatel X060S / 1bbb:0000 showed this problem).
	sleep_seconds(5);
}

/**
 * Query the device for supported capabilities.
 */
static void umts_check_caps(struct umts_state *state) {
	char b[512] = {0};
	state->is_gsm = 0;
	if (umts_tty_put(state->ctlfd, "AT+GCAP\r") >= 0
	&& umts_tty_get(state->ctlfd, b, sizeof(b), 2500) == UMTS_AT_OK) {
		if (strstr(b, "CGSM")) {
			state->is_gsm = 1;
			umts_config_set(state, "modem_gsm", "1");
			syslog(LOG_NOTICE, "%s: Detected a GSM modem", state->modem.tty);
		}
	}
}

/**
 * Set the device mode (GPRS/UMTS).
 *
 * The mode to set is taken from the configuration.
 */
static void umts_set_mode(struct umts_state *state) {
	char b[512] = {0};
	char *m = umts_config_get(state, "umts_mode");
	enum umts_mode mode = umts_modem_modeval((m) ? m : "auto");
	if (mode == -1 || !state->modem.cfg->modecmd[mode]) {
		syslog(LOG_CRIT, "%s: Unsupported mode %s", state->modem.tty, m);
		free(m);
		umts_exitcode(UMTS_EINVAL);
	}
	tcflush(state->ctlfd, TCIFLUSH);
	if (state->modem.cfg->modecmd[mode][0]
	&& (umts_tty_put(state->ctlfd, state->modem.cfg->modecmd[mode]) < 0
	|| umts_tty_get(state->ctlfd, b, sizeof(b), 5000) != UMTS_AT_OK)) {
		syslog(LOG_CRIT, "%s: Failed to set mode %s (%s)",
			state->modem.tty, (m) ? m : "auto", b);
		free(m);
		umts_exitcode(UMTS_EMODEM);
	}
	syslog(LOG_NOTICE, "%s: Mode set to %s", state->modem.tty, (m) ? m : "auto");
	free(m);
}

static void umts_connect_status_mainloop(struct umts_state *state) {
	int status = -1;
	int logsteps = 4;	// Report RSSI / BER to syslog every LOGSTEPS intervals
	char provider[64] = {0};
	char b[512] = {0};

	// Main loop, wait for termination, measure signal strength
	while (!signaled) {
		// First run
		if (!++status) {
			umts_config_set(state, "connected", "1");
			ucix_save(state->uci, state->uciname);
		} else {
			sleep_seconds(15);
			if (signaled) break;
		}

		// Query provider and RSSI / BER
		tcflush(state->ctlfd, TCIFLUSH);
/*		umts_tty_put(state->ctlfd, "AT+CREG?\r");
		umts_tty_get(state->ctlfd, b, sizeof(b), 2500);
		printf("%s:%s[%d]%s\n", __FILE__, __func__, __LINE__, b);
*/
		umts_tty_put(state->ctlfd, "AT+COPS?;+CSQ\r");
		if (umts_tty_get(state->ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK)
			continue;

		char *saveptr;
		char *cops = strtok_r(b, "\r\n", &saveptr);
		char *csq = strtok_r(NULL, "\r\n", &saveptr);

		if (cops && (cops = strchr(cops, '"')) // +COPS: 0,0,"FONIC",2
		&& (cops = strtok_r(cops, "\"", &saveptr))
		&& strncmp(cops, provider, sizeof(provider) - 1)) {
			syslog(LOG_NOTICE, "%s: Provider is %s",
				state->modem.tty, cops);
			umts_config_revert(state, "provider");
			umts_config_set(state, "provider", cops);
			strncpy(provider, cops, sizeof(provider) - 1);
		}

		if (csq && (csq = strtok_r(csq, " ,", &saveptr))
		&& (csq = strtok_r(NULL, " ,", &saveptr))) {	// +CSQ: 14,99
			// RSSI
			umts_config_revert(state, "rssi");
			umts_config_set(state, "rssi", csq);
			if ((status % logsteps) == 0)
				syslog(LOG_NOTICE, "%s: RSSI is %s",
					state->modem.tty, csq);
		}
		ucix_save(state->uci, state->uciname);
	}
	syslog(LOG_NOTICE, "Received signal %d, disconnecting", signaled);
}

static void umts_connect_finish(struct umts_state *state) {
	umts_config_revert(state, "pid");
	umts_config_revert(state, "connected");
	umts_config_revert(state, "provider");
	umts_config_revert(state, "rssi");

	// Terminate active connection by hanging up and resetting
	umts_tty_put(state->ctlfd, "ATH;&F\r");
	int status;
	if (waitpid(state->pppd, &status, WNOHANG) != state->pppd) {
		kill(state->pppd, SIGTERM);
		waitpid(state->pppd, &status, 0);
		syslog(LOG_NOTICE, "%s: Terminated by signal %i",
				state->modem.tty, signaled);
		umts_exitcode(UMTS_ESIGNALED);
	}

	if (WIFSIGNALED(status) || WEXITSTATUS(status) == 5) {
		// pppd was termined externally, we won't treat this as an error
		syslog(LOG_NOTICE, "%s: pppd terminated by signal", state->modem.tty);
		umts_exitcode(UMTS_ESIGNALED);
	}

	switch (WEXITSTATUS(status)) {	// Exit codes from pppd (man pppd)
		case 7:
		case 16:
			syslog(LOG_CRIT, "%s: pppd modem error", state->modem.tty);
			umts_exitcode(UMTS_EMODEM);

		case 8:
			syslog(LOG_CRIT, "%s: pppd dialing error", state->modem.tty);
			umts_exitcode(UMTS_EDIAL);

		case 0:
		case 15:
			syslog(LOG_CRIT, "%s: terminated by network", state->modem.tty);
			umts_exitcode(UMTS_ENETWORK);

		case 19:
			syslog(LOG_CRIT, "%s: invalid PPP credentials", state->modem.tty);
			umts_exitcode(UMTS_EAUTH);

		default:
			syslog(LOG_CRIT, "%s: PPP error (%i)",
					state->modem.tty, WEXITSTATUS(status));
			umts_exitcode(UMTS_EPPP);
	}
}

int main(int argc, char *const argv[]) {
	enum umts_app app;
	app = umts_parse_cmdline(&state, argc, argv);

	umts_setup_syslog(&state, app);

	umts_setup_uci(&state);

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

	umts_select_modem(&state);

	umts_open_control(&state);

	umts_modem_reset(&state);

	umts_identify(&state);

	umts_check_sim(&state);


	if (app == UMTS_APP_SCAN) {
		umts_exitcode(UMTS_OK); // We are done here.
	} else if (app == UMTS_APP_PINPUK) {
		// Need two arguments
		if (optind + 2 != argc) {
			syslog(LOG_CRIT, "%s: Need exactly two arguments for -p", state.modem.tty);
			umts_exitcode(UMTS_EINVAL);
		}

		umts_enter_puk(&state, argv[optind], argv[optind+1]);
	}

	if (state.simstate == 2) {
		umts_exitcode(UMTS_EUNLOCK);
	} else if (state.simstate == 1) {
		umts_enter_pin(&state);
	}

	if (app == UMTS_APP_UNLOCK)
		umts_exitcode(UMTS_OK); // We are done here.

	umts_check_caps(&state);
/*
	char b[512] = {0};
	// verbose provider info
	if (umts_tty_put(state.ctlfd, "AT+CREG=2\r") < 1
	|| umts_tty_get(state.ctlfd, b, sizeof(b), 2500) != UMTS_AT_OK) {
		syslog(LOG_CRIT, "%s: failed to set verbose provider info (%s)", state.modem.tty, b);
	}
*/

	// Setting network mode if GSM
	if (state.is_gsm) {
		umts_set_mode(&state);
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

	umts_connect_status_mainloop(&state);

	/* Clean up state and set exit code. Never returns. */
	umts_connect_finish(&state);

	// This cannot happen
	return UMTS_EINTERNAL;
}

