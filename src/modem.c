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
#include <limits.h>
#include <string.h>
#include <syslog.h>

#include "deviceconfig.h"

#define UMTS_SYS_USB_DEVICES "/sys/bus/usb/devices/*"

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

/**
 * Find a profile matching the attributes passed. The configuration for
 * this profile is stored in modem->cfg.
 *
 * Returns UMTS_OK when a profile was found or UMTS_ENODEV when there
 * was no applicable profile.
 */
static int umts_modem_match_profile(struct umts_modem *modem) {
	// Find the first profile that has all of its conditions
	// matching. The array is ordered so that specific devices are
	// matched first, then generic per-vendor profiles and then
	// generic per-driver profiles.
	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		const struct umts_profile *p = &profiles[i];
		if (((p->flags & UMTS_PROFILE_NOVENDOR) || p->vendor == modem->vendor)
		&& ((p->flags & UMTS_PROFILE_NODEVICE) || p->device == modem->device)
		&& (!p->driver || !strcmp(p->driver, modem->driver))) {
			modem->cfg = &p->cfg;

			if (p->vendor)
				syslog(LOG_INFO, "%s: Matched USB vendor id 0x%x", modem->tty, p->vendor);
			if (p->device)
				syslog(LOG_INFO, "%s: Matched USB product id 0x%x", modem->tty, p->device);
			if (p->driver)
				syslog(LOG_INFO, "%s: Matched driver name \"%s\"", modem->tty, p->driver);
			syslog(LOG_NOTICE, "%s: Autoselected configuration profile \"%s\"", modem->tty, p->name);
			return UMTS_OK;
		}
	}

	return UMTS_ENODEV;
}

/**
 * Scan the list of USB devices for any device that looks like a usable
 * device.
 *
 * When func is NULL, detection stops at the first usable device, which
 * is returned in *modem.
 *
 * When func is not null, it is called for every device detected. The
 * contents of *modem are undefined when the function returns.
 *
 * When no modems were found, this function returns UMTS_ENODEV.
 * If at least one modem was detected, it returns UMTS_OK.
 */
int umts_modem_find_devices(struct umts_modem *modem, void func(struct umts_modem *), struct umts_device_filter *filter) {
	if (func)
		syslog(LOG_INFO, "Detecting usable devices");
	else
		syslog(LOG_INFO, "Detecting first usable device");

	if (filter->flags & UMTS_FILTER_VENDOR)
		syslog(LOG_INFO, "Only considering devices with vendor id 0x%x", filter->vendor);
	if (filter->flags & UMTS_FILTER_DEVICE)
		syslog(LOG_INFO, "Only considering devices with product id 0x%x", filter->device);

	bool found = false;
	glob_t gl;
	char buf[PATH_MAX + 1];
	int e = umts_util_checked_glob(UMTS_SYS_USB_DEVICES, GLOB_NOSORT, &gl, "listing USB devices");
	if (e) return e;

	for (size_t i = 0; i < gl.gl_pathc; ++i) {
		char *path = gl.gl_pathv[i];

		char *device_id = strrchr(path, '/') + 1;

		/* Skip devices with a : in their id, which are
		 * really subdevices / endpoints */
		if (strchr(device_id, ':'))
			continue;

		/* Get the USB vidpid. */
		snprintf(buf, sizeof(buf), "%s/%s", path, "idVendor");
		if (umts_util_read_hex_word(buf, &modem->vendor)) continue;
		snprintf(buf, sizeof(buf), "%s/%s", path, "idProduct");
		if (umts_util_read_hex_word(buf, &modem->device)) continue;

		/* Check commandline vidpid filter */
		if (((filter->flags & UMTS_FILTER_VENDOR) && (filter->vendor != modem->vendor))
		|| ((filter->flags & UMTS_FILTER_DEVICE) && (filter->device != modem->device))) {
			syslog(LOG_DEBUG, "Skipping USB device %s (0x%04x:0x%04x) due to commandline filter", device_id, modem->vendor, modem->device);
			continue;
		}

		syslog(LOG_DEBUG, "Considering USB device %s (0x%04x:0x%04x)", device_id, modem->vendor, modem->device);

		/* Find out how many tty devices this USB device
		 * exports. */
		snprintf(buf, sizeof(buf), "%s/*/tty*", path);
		glob_t gl_tty;
		int e = umts_util_checked_glob(buf, 0, &gl_tty, "listing tty devices");
		if (e) continue; /* No ttys or glob error */
		syslog(LOG_DEBUG, "Found %zu tty device%s", gl_tty.gl_pathc, gl_tty.gl_pathc != 1 ? "s" : "" );

		/* Split the matched filename into the subdevice name
		 * and the tty name. e.g., into
		 * "/sys/bus/usb/devices/1-1.1/1-1.1:1.0" and "ttyUSB0".
		 */
		const char *subdev = gl_tty.gl_pathv[0];
		char *ttyname = strrchr(subdev, '/');
		*ttyname = '\0';
		ttyname++;

		snprintf(modem->tty, sizeof(modem->tty), "%s", ttyname);

		/* Read the driver name from the first subdev with a tty
		 * (the main device just has driver "usb", so that won't
		 * help us). */
		snprintf(buf, sizeof(buf), "%s/driver", subdev);

		umts_util_read_symlink_basename(buf, modem->driver, sizeof(modem->driver));
		syslog(LOG_DEBUG, "%s uses driver \"%s\"", ttyname, modem->driver);

		if (umts_modem_match_profile(modem) == UMTS_OK) {
			syslog(LOG_INFO, "Found usable USB device (0x%04x:0x%04x)", modem->vendor, modem->device);
			found = true;

			/* Call the callback, if any. If there is no
			 * callback, just return the first match. */
			if (func)
				func(modem);
			else
				break;
		}
	}

	return (found ? UMTS_OK : UMTS_ENODEV);
}

/**
 * Helper function to pint a modem to stdout.
 */
static void umts_modem_print(struct umts_modem *modem) {
	printf("Device\n");
	printf("\tVendor: 0x%04x\n", modem->vendor);
	printf("\tProduct: 0x%04x\n", modem->device);
	printf("\tDriver: %s\n", modem->driver);
}

/**
 * Detect (potentially) usable devices and list them on stdout.
 */
int umts_modem_list_devices(struct umts_device_filter *filter) {
	syslog(LOG_NOTICE, "Listing usable devices");
	/* Allocate some storage for umts_modem_find_devices to work */
	struct umts_modem modem;
	int e = umts_modem_find_devices(&modem, umts_modem_print, filter);
	if (e == UMTS_ENODEV) {
		syslog(LOG_NOTICE, "No devices found");
		return UMTS_OK;
	} else if (e != UMTS_OK) {
		syslog(LOG_ERR, "Error while detecting devices");
		return e;
	}

	return UMTS_OK;
}

/**
 * Output a list of all known profiles on stdout.
 */
int umts_modem_list_profiles() {
	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		const struct umts_profile *p = &profiles[i];
		printf("Profile: %s\n", p->name);
		if (p->driver) printf("\tDriver: %s\n", p->driver);
		if (p->vendor) printf("\tVendor: 0x%x\n", p->vendor);
		if (p->device) printf("\tProduct: 0x%x\n", p->device);
		printf("\tControl: %d\n", p->cfg.ctlidx);
		printf("\tData: %d\n", p->cfg.datidx);
		for (int mode = 0; mode < UMTS_NUM_MODES; ++mode)
			if (p->cfg.modecmd[mode]) printf("\tMode-%s: %s\n", umts_modem_modestr(mode), p->cfg.modecmd[mode]);
		printf("\n");
	}
	return 0;
}
