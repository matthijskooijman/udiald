/*
 * umtsd.h
 *
 *  Created on: 08.09.2010
 *      Author: steven
 */

#ifndef UMTSD_H_
#define UMTSD_H_

#include <libubox/list.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <glob.h>
#include "ucix.h"

#define UMTS_FLAG_TESTSTATE	0x01
#define UMTS_FLAG_NOERRSTAT	0x02
#define UMTS_FLAG_SIGNALED	0x04

enum umts_errcode {
	UMTS_OK,
	UMTS_EINVAL,
	UMTS_EINTERNAL,
	UMTS_ESIGNALED,
	UMTS_ENODEV,
	UMTS_EMODEM,
	UMTS_ESIM,
	UMTS_EUNLOCK,
	UMTS_EDIAL,
	UMTS_EAUTH,
	UMTS_EPPP,
	UMTS_ENETWORK,
};

enum umts_mode {
	UMTS_MODE_AUTO,
	UMTS_FORCE_UMTS,
	UMTS_FORCE_GPRS,
	UMTS_PREFER_UMTS,
	UMTS_PREFER_GPRS,
	UMTS_NUM_MODES /* This must always be the last entry. */
};

enum umts_atres {
	UMTS_FAIL = -1,
	UMTS_AT_OK,
	UMTS_AT_CONNECT,
	UMTS_AT_ERROR,
	UMTS_AT_CMEERROR,
	UMTS_AT_NODIALTONE,
	UMTS_AT_BUSY,
	UMTS_AT_NOCARRIER,
};

struct umts_config {
	uint8_t ctlidx;		/* Index of control TTY from first TTY */
	uint8_t datidx;		/* Index of data TTY from first TTY */
	const char *modecmd[UMTS_NUM_MODES];	/* Commands to enter modes */
};

enum umts_profile_flags {
	UMTS_PROFILE_NOVENDOR = 1, /* The vendor field in this profile should be ignored */
	UMTS_PROFILE_NODEVICE = 2, /* The device field in this profile should be ignored */
};

/* Configuration profile, which combines a configuration with info about
 * which device it supports.
 */
struct umts_profile {
	enum umts_profile_flags flags; /* Flags influencing profile selection */
	char *name; /* A descriptive name for the profile */
	uint16_t vendor; /* The USB vendor id. */
	uint16_t device; /* The USB product id. */
	char *driver; /* The usb driver, or NULL for a device profile or generic vendor profile. */
	const struct umts_config cfg;
};

enum umts_filter_flags {
	UMTS_FILTER_VENDOR = 1, /* The vendor field in this filter is valid */
	UMTS_FILTER_DEVICE = 2, /* The device field in this filter is valid */
};

/**
 * A set of limitations for device auto-detection.
 */
struct umts_device_filter {
	enum umts_filter_flags flags; /* Flags to determine validity of vendor and device fields */
	uint16_t vendor; /* The USB vendor id. */
	uint16_t device; /* The USB product id. */
	char *device_id; /* The actual device id to use e.g., "1-1.5.3.7" */
};

struct umts_modem {
	uint16_t vendor;
	uint16_t device;
	char driver[32];
	char tty[16];
	const struct umts_config *cfg;
};

struct umts_command {
	char *command;
	int timeout;
	char *response;
};

/* Current umts state */
struct umts_state {
	int ctlfd;
	int flags;
	int simstate;
	int is_gsm;
	struct umts_device_filter filter;
	struct umts_modem modem;
	struct uci_context *uci;
	char uciname[32];
	char profile[32];
	pid_t pppd;
};

extern int verbose;

const char* umts_modem_modestr(enum umts_mode mode);
enum umts_mode umts_modem_modeval(const char *mode);
int umts_modem_find_devices(struct umts_modem *modem, void func(struct umts_modem *), struct umts_device_filter *filter);
int umts_modem_list_profiles();
int umts_modem_list_devices(struct umts_device_filter *filter);

int umts_tty_open(const char *tty);
char* umts_tty_calc(const char *basetty, uint8_t index, char buf[static 24]);
int umts_tty_cloexec(int fd);
int umts_tty_put(int fd, const char *cmd);
enum umts_atres umts_tty_get(int fd, char *buf, size_t len, int timeout);
pid_t umts_tty_pppd(struct umts_state *state);

int umts_connect_main(struct umts_state *state);
int umts_dial_main(struct umts_state *state);

int umts_util_checked_glob(const char *pattern, int flags, glob_t *pglob, const char *activity);
int umts_util_parse_hex_word(const char *hex, uint16_t *res);
int umts_util_read_hex_word(const char *path, uint16_t *res);
void umts_util_read_symlink_basename(const char *path, char *res, size_t size);

#endif /* UMTSD_H_ */
