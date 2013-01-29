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

#include "udiald.h"
#include "config.h"

static volatile int signaled = 0;
static struct udiald_state state = {.uciname = "network", .networkname = "wan"};
int verbose = 0;

static int udiald_usage(const char *app) {
	fprintf(stderr,
			"udiald - UMTS connection manager\n"
			"(c) 2010 Steven Barth, John Crispin\n\n"
			"Usage: %s [options] [params...]\n\n"
			"Command Options and Parameters:\n"
			"	-c			Connect using modem (default)\n"
			"	-s			Scan modem and reset state file\n"
			"	-u			Same as scan but also try to unlock SIM\n"
			" 	-U <PUK> <PIN>		Reset PIN of locked SIM using PUK\n"
			"	-d			Dial (used internally)\n"
			"	-L                      List available configuration profiles\n"
			"	-l                      Detect and list usable devices\n"
			"\nGlobal Options:\n"
			"	-e			Don't write error state\n"
			"	-n <name>		Use given network name instead of \"wan\"\n"
			"	-v			Increase verbosity\n\n"
			"	-V <vendorid>		Only consider devices with the given vendor id (in hexadecimal)\n"
			"	-P <productid>		Only consider devices with the given product id (in hexadecimal)\n"
			"	-D <deviceid>		Only consider the device with the given id (as listed in sysfs,\n"
			"				e.g. 1.2-1)\n"
			"	-p <profilename>        Use the profile with the given name instead of autodetecting a\n"
			"				profile to use. Run with -L to get a list of valid profiles.\n"
			"Connect Options:\n"
			"	-t			Test state file for previous SIM-unlocking\n"
			"				errors before attempting to connect\n\n"
			"Return Codes:\n"
			"	0			OK\n"
			"	1			Syntax error\n"
			"	2			Internal error\n"
			"   	3			Terminated by signal\n"
			"	4			No usable modem found\n"
			"	5			Modem error\n"
			"	6			SIM error\n"
			"	7			SIM unlocking error (PIN failed etc.)\n"
			"	8			Dialing error\n"
			"	9			PPP auth error\n"
			"   	10			Generic PPP error\n"
			"   	11			Network error\n",
			app);
	return UDIALD_EINVAL;
}

static void udiald_catch_signal(int signal) {
	if (!signaled) signaled = signal;
}

// Signal safe cleanup function
static void udiald_cleanup_safe(int signal) {
	if (state.ctlfd > 0) {
		close(state.ctlfd);
		state.ctlfd = -1;
	}
	if (signal) {
		state.flags |= UDIALD_FLAG_SIGNALED;
	}
}

static void udiald_cleanup() {
	if (state.uci) {
		ucix_cleanup(state.uci);
		state.uci = NULL;
	}
	udiald_cleanup_safe(0);
}

static void udiald_exitcode(int code) {
	if (code && state.flags & UDIALD_FLAG_SIGNALED)
		code = UDIALD_ESIGNALED;
	if (code && code != UDIALD_ESIGNALED && !(state.flags & UDIALD_FLAG_NOERRSTAT))
		udiald_config_set_int(&state, "udiald_error", code);
	if (state.app == UDIALD_APP_CONNECT)
		udiald_config_revert(&state, "udiald_state");
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
static enum udiald_app udiald_parse_cmdline(struct udiald_state *state, int argc, char * const argv[]) {
	enum udiald_app app = UDIALD_APP_CONNECT;

	int s;
	while ((s = getopt(argc, argv, "csuUden:vtlLV:P:D:p:")) != -1) {
		switch(s) {
			case 'c':
				app = UDIALD_APP_CONNECT;
				break;

			case 's':
				app = UDIALD_APP_SCAN;
				break;

			case 'u':
				app = UDIALD_APP_UNLOCK;
				break;

			case 'U':
				app = UDIALD_APP_PINPUK;
				break;

			case 'd':
				app = UDIALD_APP_DIAL;
				break;

			case 'l':
				app = UDIALD_APP_LIST_DEVICES;
				break;

			case 'L':
				app = UDIALD_APP_LIST_PROFILES;
				break;

			case 'e':
				state->flags |= UDIALD_FLAG_NOERRSTAT;
				break;

			case 'n':
				strncpy(state->networkname, optarg, sizeof(state->networkname) - 1);
				break;

			case 'v':
				verbose++;
				break;

			case 't':
				state->flags |= UDIALD_FLAG_TESTSTATE;
				break;
			case 'V':
				if (udiald_util_parse_hex_word(optarg, &state->filter.vendor) != UDIALD_OK) {
					fprintf(stderr, "Failed to parse vendor id: \"%s\"\n", optarg);
					exit(UDIALD_EINVAL);
				}
				state->filter.flags |= UDIALD_FILTER_VENDOR;
				break;
			case 'P':
				if (udiald_util_parse_hex_word(optarg, &state->filter.device) != UDIALD_OK) {
					fprintf(stderr, "Failed to parse product id: \"%s\"\n", optarg);
					exit(UDIALD_EINVAL);
				}
				state->filter.flags |= UDIALD_FILTER_DEVICE;
				break;
			case 'D':
				state->filter.device_id = optarg;
				break;
			case 'p':
				state->filter.profile_name = strdup(optarg);
				break;
			default:
				exit(udiald_usage(argv[0]));
		}
	}

	return app;
}

static void udiald_setup_syslog(struct udiald_state *state) {
	char *appname = "udiald";
	if (state->app == UDIALD_APP_DIAL)
		appname = "udiald-dialer";

	openlog(appname, LOG_PID | LOG_PERROR, LOG_USER);

	if (!verbose)
		setlogmask(LOG_UPTO(LOG_NOTICE));
}

static void udiald_setup_uci(struct udiald_state *state) {
	// Prepare and initialize state
	if (!(state->uci = ucix_init(state->uciname, 1))) {
		exit(UDIALD_EINTERNAL);
	}
	/* Reset errno, when running udiald unprivileged, setting up uci
	 * might cause an ignored error, which could cloud debug
	 * attempts */
	errno = 0;
}

/**
 * Select the modem to use, depending on config or autodetection.
 */
static void udiald_select_modem(struct udiald_state *state) {
	/* Only return a modem for which we have a valid configuration profile */
	state->filter.flags |= UDIALD_FILTER_PROFILE;

	/* Autodetect the first available modem (if any) */
	int e = udiald_modem_find_devices(state, &state->modem, NULL, NULL, &state->filter);
	if (e != UDIALD_OK) {
		syslog(LOG_CRIT, "No usable modem found");
		udiald_exitcode(e);
	}
	char b[512] = {0};
	snprintf(b, sizeof(b), "%04x:%04x", state->modem.vendor, state->modem.device);
	syslog(LOG_NOTICE, "%s: Found %s modem %s", state->modem.device_id,
			state->modem.driver, b);
	udiald_config_set(state, "modem_id", b);
	udiald_config_set(state, "modem_driver", state->modem.driver);

	b[0] = '\0';
	// Writing modestrings
	const struct udiald_config *cfg = &state->modem.profile->cfg;
	for (size_t i = 0; i < UDIALD_NUM_MODES; ++i)
		if (cfg->modecmd[i]) {
			udiald_config_append(state, "modem_mode", udiald_modem_modestr(i));
			strncat(b, udiald_modem_modestr(i), sizeof(b) - strlen(b) - 2);
			strcat(b, " ");
		}
	syslog(LOG_NOTICE, "%s: Supported modes: %s", state->modem.device_id, b);
}

/**
 * Open the control connection, storing the fd in state->ctlfd.
 */
static void udiald_open_control(struct udiald_state *state) {
	// Open control connection
	char ttypath[24];
	snprintf(ttypath, sizeof(ttypath), "/dev/%s", state->modem.ctl_tty);
	if ((state->ctlfd = udiald_tty_cloexec(udiald_tty_open(ttypath))) == -1) {
		syslog(LOG_CRIT, "%s: Unable to open terminal", state->modem.device_id);
		udiald_exitcode(UDIALD_EMODEM);
	}
}

/**
 * Reset the modem through the control connection.
 */
static void udiald_modem_reset(struct udiald_state *state) {
	char b[512] = {0};
	// Hangup modem, disable echoing
	tcflush(state->ctlfd, TCIFLUSH);
	udiald_tty_put(state->ctlfd, "ATE0\r");
	udiald_tty_get(state->ctlfd, b, sizeof(b), 2500);
	tcflush(state->ctlfd, TCIFLUSH);
}

/**
 * Query the modem for identification.
 */
static void udiald_identify(struct udiald_state *state) {
	char b[512] = {0};
	// Identify modem
	if (udiald_tty_put(state->ctlfd, "AT+CGMI;+CGMM\r") < 1
	|| udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) != UDIALD_AT_OK) {
		syslog(LOG_CRIT, "%s: Unable to identify modem (%s)", state->modem.device_id, b);
		udiald_exitcode(UDIALD_EMODEM);
	}
	char *saveptr;
	char *mi = strtok_r(b, "\r\n", &saveptr);
	char *mm = strtok_r(NULL, "\r\n", &saveptr);
	if (mi && mm) {
		mi = strdup(mi);
		mm = strdup(mm);
		snprintf(b, sizeof(b), "%s %s", mi, mm);
		syslog(LOG_NOTICE, "%s: Identified as %s", state->modem.device_id, b);
		udiald_config_set(state, "modem_name", b);
		free(mi);
		free(mm);
	}
}

/**
 * Query the modem for its SIM status.
 */
static void udiald_check_sim(struct udiald_state *state) {
	char b[512] = {0};
	char *saveptr;
	char *c = NULL;
	// Getting SIM state
	tcflush(state->ctlfd, TCIFLUSH);
	if (udiald_tty_put(state->ctlfd, "AT+CPIN?\r") < 1
	|| udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) != UDIALD_AT_OK
	|| !(c = strtok_r(b, "\r\n", &saveptr))) {
		syslog(LOG_CRIT, "%s: Unable to get SIM status (%s)", state->modem.device_id, b);
		udiald_config_set(state, "sim_state", "error");
		udiald_exitcode(UDIALD_ESIM);
	}

	// Evaluate SIM state
	if (!strcmp(c, "+CPIN: READY")) {
		syslog(LOG_NOTICE, "%s: SIM card is ready", state->modem.device_id);
		udiald_config_set(state, "sim_state", "ready");
		state->sim_state = 0;
	} else if (!strcmp(c, "+CPIN: SIM PIN")) {
		udiald_config_set(state, "sim_state", "wantpin");
		state->sim_state = 1;
	} else if (!strcmp(c, "+CPIN: SIM PUK")) {
		syslog(LOG_WARNING, "%s: SIM requires PUK!", state->modem.device_id);
		udiald_config_set(state, "sim_state", "wantpuk");
		state->sim_state = 2;
	} else {
		syslog(LOG_CRIT, "%s: Unknown SIM status (%s)", state->modem.device_id, c);
		udiald_config_set(state, "sim_state", "error");
		state->sim_state = -1;
		udiald_exitcode(UDIALD_ESIM);
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
static void udiald_enter_puk(struct udiald_state *state, const char *puk, const char *pin) {
	// Reset PIN with PUK
	if (state->sim_state != 2)
		udiald_exitcode(UDIALD_ESIM);

	// Prepare command
	char b[512] = {0};
	if (strpbrk(pin, "\"\r\n;") || strpbrk(puk, "\"\r\n;"))
		udiald_exitcode(UDIALD_EINVAL);
	snprintf(b, sizeof(b), "AT+CPIN=\"%s\",\"%s\"\r", puk, pin);

	// Send command
	tcflush(state->ctlfd, TCIFLUSH);
	if (udiald_tty_put(state->ctlfd, b) >= 0
	&& udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) == UDIALD_AT_OK) {
		syslog(LOG_NOTICE, "%s: PIN reset successful", state->modem.device_id);
		udiald_config_set(state, "sim_state", "ready");
		udiald_exitcode(UDIALD_OK);
	} else {
		syslog(LOG_CRIT, "%s: Failed to reset PIN (%s)", state->modem.device_id, b);
		udiald_exitcode(UDIALD_EUNLOCK);
	}
}

/**
 * Unlock the device using the PIN.
 *
 * The pincode to use is taken from the configuration.
 */
static void udiald_enter_pin(struct udiald_state *state) {
	//Try unlocking with PIN
	char *pin = udiald_config_get(state, "udiald_pin");
	char b[512] = {0};
	if (!pin) {
		syslog(LOG_CRIT, "%s: PIN missing", state->modem.device_id);
		udiald_exitcode(UDIALD_EUNLOCK);
	}
	if (strpbrk(pin, "\"\r\n;"))
		udiald_exitcode(UDIALD_EINVAL);
	snprintf(b, sizeof(b), "AT+CPIN=\"%s\"\r", pin);
	free(pin);

	// Send command
	tcflush(state->ctlfd, TCIFLUSH);
	if (udiald_tty_put(state->ctlfd, b) < 0
	|| udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) != UDIALD_AT_OK) {
		syslog(LOG_CRIT, "%s: PIN rejected (%s)", state->modem.device_id, b);
		udiald_exitcode(UDIALD_EUNLOCK);
	}
	syslog(LOG_NOTICE, "%s: PIN accepted", state->modem.device_id);
	udiald_config_set(state, "sim_state", "ready");

	// Wait a few seconds for the dongle to find a carrier.
	// Some dongles apparently do not send a NO CARRIER reply to the
	// dialing, but instead hang up directly after sending a CONNECT
	// reply (Alcatel X060S / 1bbb:0000 showed this problem).
	sleep_seconds(5);
}

/**
 * Query the device for supported capabilities.
 */
static void udiald_check_caps(struct udiald_state *state) {
	char b[512] = {0};
	state->is_gsm = 0;
	if (udiald_tty_put(state->ctlfd, "AT+GCAP\r") >= 0
	&& udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) == UDIALD_AT_OK) {
		if (strstr(b, "CGSM")) {
			state->is_gsm = 1;
			udiald_config_set(state, "modem_gsm", "1");
			syslog(LOG_NOTICE, "%s: Detected a GSM modem", state->modem.device_id);
		}
	}
}

/**
 * Set the device mode (GPRS/UMTS).
 *
 * The mode to set is taken from the configuration.
 */
static void udiald_set_mode(struct udiald_state *state) {
	char b[512] = {0};
	char *m = udiald_config_get(state, "udiald_mode");
	enum udiald_mode mode = udiald_modem_modeval((m) ? m : "auto");
	if (mode == -1 || !state->modem.profile->cfg.modecmd[mode]) {
		syslog(LOG_CRIT, "%s: Unsupported mode %s", state->modem.device_id, m);
		free(m);
		udiald_exitcode(UDIALD_EINVAL);
	}
	tcflush(state->ctlfd, TCIFLUSH);
	if (state->modem.profile->cfg.modecmd[mode][0]
	&& (udiald_tty_put(state->ctlfd, state->modem.profile->cfg.modecmd[mode]) < 0
	|| udiald_tty_get(state->ctlfd, b, sizeof(b), 5000) != UDIALD_AT_OK)) {
		syslog(LOG_CRIT, "%s: Failed to set mode %s (%s)",
			state->modem.device_id, (m) ? m : "auto", b);
		free(m);
		udiald_exitcode(UDIALD_EMODEM);
	}
	syslog(LOG_NOTICE, "%s: Mode set to %s", state->modem.device_id, (m) ? m : "auto");
	free(m);
}

static void udiald_connect_status_mainloop(struct udiald_state *state) {
	int status = -1;
	int logsteps = 4;	// Report RSSI / BER to syslog every LOGSTEPS intervals
	char provider[64] = {0};
	char b[512] = {0};

	// Main loop, wait for termination, measure signal strength
	while (!signaled) {
		// First run
		if (!++status) {
			udiald_config_set(state, "connected", "1");
			ucix_save(state->uci, state->uciname);
		} else {
			sleep_seconds(15);
			if (signaled) break;
		}

		// Query provider and RSSI / BER
		tcflush(state->ctlfd, TCIFLUSH);
/*		udiald_tty_put(state->ctlfd, "AT+CREG?\r");
		udiald_tty_get(state->ctlfd, b, sizeof(b), 2500);
		printf("%s:%s[%d]%s\n", __FILE__, __func__, __LINE__, b);
*/
		udiald_tty_put(state->ctlfd, "AT+COPS?;+CSQ\r");
		if (udiald_tty_get(state->ctlfd, b, sizeof(b), 2500) != UDIALD_AT_OK)
			continue;

		char *saveptr;
		char *cops = strtok_r(b, "\r\n", &saveptr);
		char *csq = strtok_r(NULL, "\r\n", &saveptr);

		if (cops && (cops = strchr(cops, '"')) // +COPS: 0,0,"FONIC",2
		&& (cops = strtok_r(cops, "\"", &saveptr))
		&& strncmp(cops, provider, sizeof(provider) - 1)) {
			syslog(LOG_NOTICE, "%s: Provider is %s",
				state->modem.device_id, cops);
			udiald_config_revert(state, "provider");
			udiald_config_set(state, "provider", cops);
			strncpy(provider, cops, sizeof(provider) - 1);
		}

		if (csq && (csq = strtok_r(csq, " ,", &saveptr))
		&& (csq = strtok_r(NULL, " ,", &saveptr))) {	// +CSQ: 14,99
			// RSSI
			udiald_config_revert(state, "rssi");
			udiald_config_set(state, "rssi", csq);
			if ((status % logsteps) == 0)
				syslog(LOG_NOTICE, "%s: RSSI is %s",
					state->modem.device_id, csq);
		}
		ucix_save(state->uci, state->uciname);
	}
	syslog(LOG_NOTICE, "Received signal %d, disconnecting", signaled);
}

static void udiald_connect_finish(struct udiald_state *state) {
	udiald_config_revert(state, "pid");
	udiald_config_revert(state, "connected");
	udiald_config_revert(state, "provider");
	udiald_config_revert(state, "rssi");

	// Terminate active connection by hanging up and resetting
	udiald_tty_put(state->ctlfd, "ATH;&F\r");
	int status;
	if (waitpid(state->pppd, &status, WNOHANG) != state->pppd) {
		kill(state->pppd, SIGTERM);
		waitpid(state->pppd, &status, 0);
		syslog(LOG_NOTICE, "%s: Terminated by signal %i",
				state->modem.device_id, signaled);
		udiald_exitcode(UDIALD_ESIGNALED);
	}

	if (WIFSIGNALED(status) || WEXITSTATUS(status) == 5) {
		// pppd was termined externally, we won't treat this as an error
		syslog(LOG_NOTICE, "%s: pppd terminated by signal", state->modem.device_id);
		udiald_exitcode(UDIALD_ESIGNALED);
	}

	switch (WEXITSTATUS(status)) {	// Exit codes from pppd (man pppd)
		case 7:
		case 16:
			syslog(LOG_CRIT, "%s: pppd modem error", state->modem.device_id);
			udiald_exitcode(UDIALD_EMODEM);

		case 8:
			syslog(LOG_CRIT, "%s: pppd dialing error", state->modem.device_id);
			udiald_exitcode(UDIALD_EDIAL);

		case 0:
		case 15:
			syslog(LOG_CRIT, "%s: terminated by network", state->modem.device_id);
			udiald_exitcode(UDIALD_ENETWORK);

		case 19:
			syslog(LOG_CRIT, "%s: invalid PPP credentials", state->modem.device_id);
			udiald_exitcode(UDIALD_EAUTH);

		default:
			syslog(LOG_CRIT, "%s: PPP error (%i)",
					state->modem.device_id, WEXITSTATUS(status));
			udiald_exitcode(UDIALD_EPPP);
	}
}

int main(int argc, char *const argv[]) {
	INIT_LIST_HEAD(&state.custom_profiles);

	state.app = udiald_parse_cmdline(&state, argc, argv);

	udiald_setup_syslog(&state);

	udiald_setup_uci(&state);

	/* Load additional profiles from uci */
	udiald_modem_load_profiles(&state);

	atexit(udiald_cleanup);

	//Setup signals
	struct sigaction sa = {
		.sa_handler = SIG_IGN,
	};
	sigaction(SIGPIPE, &sa, NULL);
	sa.sa_handler = udiald_cleanup_safe;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	// Dial only needs an active UCI context
	if (state.app == UDIALD_APP_DIAL)
		return udiald_dial_main(&state);

	if (state.app == UDIALD_APP_LIST_PROFILES)
		return udiald_modem_list_profiles(&state);

	if (state.app == UDIALD_APP_LIST_DEVICES)
		return udiald_modem_list_devices(&state, &state.filter);

	if (state.app == UDIALD_APP_CONNECT && state.flags & UDIALD_FLAG_TESTSTATE) {
		if (udiald_config_get_int(&state, "udiald_error", UDIALD_OK) == UDIALD_EUNLOCK) {
			syslog(LOG_CRIT, "Aborting due to previous SIM unlocking failure. "
			"Please check PIN and rescan device before reconnecting.");
			exit(UDIALD_EUNLOCK);
		}
	}

	// Reset state
	udiald_config_revert(&state, "modem_name");
	udiald_config_revert(&state, "modem_driver");
	udiald_config_revert(&state, "modem_id");
	udiald_config_revert(&state, "modem_mode");
	udiald_config_revert(&state, "modem_gsm");
	udiald_config_revert(&state, "sim_state");
	if (!(state.flags & UDIALD_FLAG_NOERRSTAT))
		udiald_config_revert(&state, "udiald_error");

	if (state.app == UDIALD_APP_CONNECT) {
		udiald_config_set(&state, "udiald_state", "init");
		ucix_save(state.uci, state.uciname);
	}

	udiald_select_modem(&state);

	udiald_open_control(&state);

	udiald_modem_reset(&state);

	udiald_identify(&state);

	udiald_check_sim(&state);


	if (state.app == UDIALD_APP_SCAN) {
		udiald_exitcode(UDIALD_OK); // We are done here.
	} else if (state.app == UDIALD_APP_PINPUK) {
		// Need two arguments
		if (optind + 2 != argc) {
			syslog(LOG_CRIT, "%s: Need exactly two arguments for -p", state.modem.device_id);
			udiald_exitcode(UDIALD_EINVAL);
		}

		udiald_enter_puk(&state, argv[optind], argv[optind+1]);
	}

	if (state.sim_state == 2) {
		udiald_exitcode(UDIALD_EUNLOCK);
	} else if (state.sim_state == 1) {
		udiald_enter_pin(&state);
	}

	if (state.app == UDIALD_APP_UNLOCK)
		udiald_exitcode(UDIALD_OK); // We are done here.

	udiald_check_caps(&state);
/*
	char b[512] = {0};
	// verbose provider info
	if (udiald_tty_put(state.ctlfd, "AT+CREG=2\r") < 1
	|| udiald_tty_get(state.ctlfd, b, sizeof(b), 2500) != UDIALD_AT_OK) {
		syslog(LOG_CRIT, "%s: failed to set verbose provider info (%s)", state.modem.device_id, b);
	}
*/

	// Setting network mode if GSM
	if (state.is_gsm) {
		udiald_set_mode(&state);
	} else {
		syslog(LOG_NOTICE, "%s: Skipped setting mode on non-GSM modem", state.modem.device_id);
	}

	// Save state
	udiald_config_set_int(&state, "pid", getpid());
	ucix_save(state.uci, state.uciname);

	// Block and unbind signals so they won't interfere
	sa.sa_handler = udiald_catch_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);

	if (state.app == UDIALD_APP_CONNECT) {
		udiald_config_set(&state, "udiald_state", "dial");
		ucix_save(state.uci, state.uciname);
	}

	// Start pppd to dial
	if (!(state.pppd = udiald_tty_pppd(&state)))
		udiald_exitcode(UDIALD_EINTERNAL);

	udiald_connect_status_mainloop(&state);

	/* Clean up state and set exit code. Never returns. */
	udiald_connect_finish(&state);

	// This cannot happen
	return UDIALD_EINTERNAL;
}

