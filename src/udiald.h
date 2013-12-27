/*
 * umtsd.h
 *
 *  Created on: 08.09.2010
 *      Author: steven
 */

#ifndef UDIALD_H_
#define UDIALD_H_

#include <libubox/list.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <glob.h>
#include <json/json.h>
#include "ucix.h"

#define UDIALD_FLAG_TESTSTATE	0x01
#define UDIALD_FLAG_NOERRSTAT	0x02
#define UDIALD_FLAG_SIGNALED	0x04

#define lengthof(x) (sizeof(x) / sizeof(*x))

enum udiald_errcode {
	UDIALD_OK,
	UDIALD_EINVAL,
	UDIALD_EINTERNAL,
	UDIALD_ESIGNALED,
	UDIALD_ENODEV,
	UDIALD_EMODEM,
	UDIALD_ESIM,
	UDIALD_EUNLOCK,
	UDIALD_EDIAL,
	UDIALD_EAUTH,
	UDIALD_EPPP,
	UDIALD_ENETWORK,
};

enum udiald_mode {
	UDIALD_MODE_AUTO,
	UDIALD_FORCE_UMTS,
	UDIALD_FORCE_GPRS,
	UDIALD_PREFER_UMTS,
	UDIALD_PREFER_GPRS,
	UDIALD_NUM_MODES /* This must always be the last entry. */
};

enum udiald_atres {
	UDIALD_FAIL = -1,
	UDIALD_AT_OK,
	UDIALD_AT_CONNECT,
	UDIALD_AT_ERROR,
	UDIALD_AT_CMEERROR,
	UDIALD_AT_NODIALTONE,
	UDIALD_AT_BUSY,
	UDIALD_AT_NOCARRIER,
	UDIALD_AT_NOT_SUPPORTED,
};

struct udiald_config {
	uint8_t ctlidx;		/* Index of control TTY from first TTY */
	uint8_t datidx;		/* Index of data TTY from first TTY */
	char *modecmd[UDIALD_NUM_MODES];	/* Commands to enter modes */
	char *dialcmd; /* Dial command */
};

enum udiald_profile_flags {
	UDIALD_PROFILE_NOVENDOR = 1, /* The vendor field in this profile should be ignored */
	UDIALD_PROFILE_NODEVICE = 2, /* The device field in this profile should be ignored */
	UDIALD_PROFILE_FROMUCI = 4, /* This profile comes from uci */
};

/* Configuration profile, which combines a configuration with info about
 * which device it supports.
 */
struct udiald_profile {
	enum udiald_profile_flags flags; /* Flags influencing profile selection */
	char *name; /* A name to identify this profile. */
	char *desc; /* A description of the device(s) supported by the profile */
	uint16_t vendor; /* The USB vendor id. */
	uint16_t device; /* The USB product id. */
	char *driver; /* The usb driver, or NULL for a device profile or generic vendor profile. */
	struct udiald_config cfg;
};

/**
 * A struct to put a udiald_profile in a ubox list.
 */
struct udiald_profile_list {
	struct udiald_profile p;
	struct list_head h;
};

enum udiald_filter_flags {
	UDIALD_FILTER_VENDOR = 1, /* The vendor field in this filter is valid */
	UDIALD_FILTER_DEVICE = 2, /* The device field in this filter is valid */
	UDIALD_FILTER_PROFILE = 4, /* Only return devices with a valid profile */
};

/**
 * A set of limitations for device auto-detection.
 */
struct udiald_device_filter {
	enum udiald_filter_flags flags; /* Flags to determine validity of vendor and device fields */
	uint16_t vendor; /* The USB vendor id. */
	uint16_t device; /* The USB product id. */
	char *device_id; /* The actual device id to use e.g., "1-1.5.3.7" */
	char *profile_name; /* Use the profile with this name (NULL for auto) */

};

struct udiald_modem {
	uint16_t vendor;
	uint16_t device;
	char driver[32];
	char device_id[32];
	char ctl_tty[16];
	char dat_tty[16];
	size_t num_ttys;
	const struct udiald_profile *profile;
};

struct udiald_command {
	char *command;
	int timeout;
	char *response;
};

enum udiald_app {
		UDIALD_APP_CONNECT, UDIALD_APP_SCAN,
		UDIALD_APP_UNLOCK, UDIALD_APP_DIAL,
		UDIALD_APP_PINPUK, UDIALD_APP_LIST_PROFILES,
		UDIALD_APP_LIST_DEVICES, UDIALD_APP_PROBE,
};

enum udiald_display_format {
	/* Full details in JSON format */
	UDIALD_FORMAT_JSON,
	/* Only identifiers */
	UDIALD_FORMAT_ID,
};

/* Current umts state */
struct udiald_state {
	int ctlfd;
	int flags;
	int sim_state;
	int is_gsm;
	struct udiald_device_filter filter;
	struct udiald_modem modem;
	struct uci_context *uci;
	char uciname[32]; /*< The name of the uci config file to use */
	char networkname[32]; /*< The name of the uci section to use */
	char *pin; /*< PIN passed on the commandline, if any */
	pid_t pppd;
	struct list_head custom_profiles; /* Custom profiles loaded from uci */
	enum udiald_app app;
	enum udiald_display_format format;
};

/* Result struct for udiald_tty_get */
struct udiald_tty_read {
	// Number of lines read
	size_t lines;
	// Lines read
	char *raw_lines[10];
	// First line starting with the given result_prefix
	char *result_line;

	// Don't use, call udiald_tty_flatten_result instead
	char flat_buf[512];
	// Don't use, raw_lines above points into this buffer
	char raw_buf[512];
};

extern int verbose;

const char* udiald_modem_modestr(enum udiald_mode mode);
enum udiald_mode udiald_modem_modeval(const char *mode);
int udiald_modem_find_devices(const struct udiald_state *state, struct udiald_modem *modem, void func(struct udiald_modem *, void *), void *data, struct udiald_device_filter *filter);
int udiald_modem_list_profiles(const struct udiald_state *state);
int udiald_modem_list_devices(const struct udiald_state *state, struct udiald_device_filter *filter);
int udiald_modem_load_profiles(struct udiald_state *state);

int udiald_tty_open(const char *tty);
char* udiald_tty_calc(const char *basetty, uint8_t index, char buf[static 24]);
int udiald_tty_cloexec(int fd);
int udiald_tty_put(int fd, const char *cmd);
const char *udiald_tty_flatten_result(struct udiald_tty_read *r);
enum udiald_atres udiald_tty_get(int fd, struct udiald_tty_read *r, const char *result_prefix, int timeout);
pid_t udiald_tty_pppd(struct udiald_state *state);

int udiald_connect_main(struct udiald_state *state);
int udiald_dial_main(struct udiald_state *state);
void udiald_select_modem(struct udiald_state *state);

int udiald_util_checked_glob(const char *pattern, int flags, glob_t *pglob, const char *activity);
int udiald_util_parse_hex_word(const char *hex, uint16_t *res);
int udiald_util_read_hex_word(const char *path, uint16_t *res);
void udiald_util_read_symlink_basename(const char *path, char *res, size_t size);
struct json_object *udiald_util_sprintf_json_string(const char *fmt, ...);

#endif /* UDIALD_H_ */
