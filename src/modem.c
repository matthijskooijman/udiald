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
 * Check if the given profile matches the given modem (or, if a name is
 * given, has the given name).
 */
static int match_profile(struct umts_modem *modem, const struct umts_profile *p, const char *profile_name) {
	if (profile_name && !strcmp(p->name, profile_name)) {
		modem->profile = p;
		syslog(LOG_NOTICE, "%s: Selected requested configuration profile \"%s\" (%s)", modem->device_id, p->name, p->desc);
		return UMTS_OK;
	}
	if (!profile_name
	&& ((p->flags & UMTS_PROFILE_NOVENDOR) || p->vendor == modem->vendor)
	&& ((p->flags & UMTS_PROFILE_NODEVICE) || p->device == modem->device)
	&& (!p->driver || !strcmp(p->driver, modem->driver))) {
		modem->profile = p;

		if (p->vendor)
			syslog(LOG_INFO, "%s: Matched USB vendor id 0x%x", modem->device_id, p->vendor);
		if (p->device)
			syslog(LOG_INFO, "%s: Matched USB product id 0x%x", modem->device_id, p->device);
		if (p->driver)
			syslog(LOG_INFO, "%s: Matched driver name \"%s\"", modem->device_id, p->driver);
		syslog(LOG_NOTICE, "%s: Autoselected configuration profile \"%s\" (%s)", modem->device_id, p->name, p->desc);
		return UMTS_OK;
	}
	return UMTS_ENODEV;
}

/**
 * Find a profile matching the attributes passed. The found profile is
 * stored in modem->profile.
 *
 * Returns UMTS_OK when a profile was found or UMTS_ENODEV when there
 * was no applicable profile.
 */
static int umts_modem_find_profile(const struct umts_state *state, struct umts_modem *modem, const char *profile_name) {
	// Match profiles loaded from uci first
	struct umts_profile_list *l;
	list_for_each_entry(l, &state->custom_profiles, h) {
		if (match_profile(modem, &l->p, profile_name) == UMTS_OK)
			return UMTS_OK;
	}
	// Find the first profile that has all of its conditions
	// matching. The array is ordered so that specific devices are
	// matched first, then generic per-vendor profiles and then
	// generic per-driver profiles.
	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		if (match_profile(modem, &profiles[i], profile_name) == UMTS_OK)
			return UMTS_OK;
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
int umts_modem_find_devices(const struct umts_state *state, struct umts_modem *modem, void func(struct umts_modem *), struct umts_device_filter *filter) {
	if (func)
		syslog(LOG_INFO, "Detecting usable devices");
	else
		syslog(LOG_INFO, "Detecting first usable device");

	if (filter->flags & UMTS_FILTER_VENDOR)
		syslog(LOG_INFO, "Only considering devices with vendor id 0x%x", filter->vendor);
	if (filter->flags & UMTS_FILTER_DEVICE)
		syslog(LOG_INFO, "Only considering devices with product id 0x%x", filter->device);
	if (filter->device_id)
		syslog(LOG_INFO, "Only considering device with device id %s", filter->device_id);

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

		/* Check commandline device id. It's a bit inefficient
		 * to list all devices and apply this filter, instead of
		 * just constructing the right sysfs path from the
		 * device id, but this keeps the code a bit simpler for
		 * now. */
		if (filter->device_id && strcmp(device_id, filter->device_id)) {
			syslog(LOG_DEBUG, "%s: Skipping device (wrong device id)", device_id);
			continue;
		}

		/* Get the USB vidpid. */
		snprintf(buf, sizeof(buf), "%s/%s", path, "idVendor");
		if (umts_util_read_hex_word(buf, &modem->vendor)) continue;
		snprintf(buf, sizeof(buf), "%s/%s", path, "idProduct");
		if (umts_util_read_hex_word(buf, &modem->device)) continue;

		/* Check commandline vidpid filter */
		if (((filter->flags & UMTS_FILTER_VENDOR) && (filter->vendor != modem->vendor))
		|| ((filter->flags & UMTS_FILTER_DEVICE) && (filter->device != modem->device))) {
			syslog(LOG_DEBUG, "%s: Skipping device (0x%04x:0x%04x) due to commandline filter", device_id, modem->vendor, modem->device);
			continue;
		}

		syslog(LOG_DEBUG, "%s: Considering device (0x%04x:0x%04x)", device_id, modem->vendor, modem->device);

		/* Find out how many tty devices this USB device
		 * exports. */
		snprintf(buf, sizeof(buf), "%s/*/tty*", path);
		glob_t gl_tty;
		int e = umts_util_checked_glob(buf, 0, &gl_tty, "listing tty devices");
		if (e) continue; /* No ttys or glob error */
		modem->num_ttys = gl_tty.gl_pathc;
		syslog(LOG_DEBUG, "%s: Found %zu tty device%s", device_id, modem->num_ttys, modem->num_ttys != 1 ? "s" : "" );


		/* Chop off the ttyUSB part, so we keep the path to the
		 * subdevice, e.g., "/sys/bus/usb/devices/1-1.1/1-1.1:1.0".
		 * We dup the string to prevent modifying original,
		 * which is needed to extract the ttys below. */
		char *subdev = strdup(gl_tty.gl_pathv[0]);
		*(strrchr(subdev, '/')) = '\0';

		/* Read the driver name from the first subdev with a tty
		 * (the main device just has driver "usb", so that won't
		 * help us). */
		snprintf(buf, sizeof(buf), "%s/driver", subdev);
		free(subdev);

		umts_util_read_symlink_basename(buf, modem->driver, sizeof(modem->driver));
		syslog(LOG_DEBUG, "%s: Detected driver \"%s\"", device_id, modem->driver);

		snprintf(modem->device_id, sizeof(modem->device_id), "%s", device_id);

		/* Find an applicable profile */
		umts_modem_find_profile(state, modem, filter->profile_name);

		/* If a profile was found, find out the tty devices to
		 * use. */
		if (modem->profile) {
			if (modem->profile->cfg.ctlidx < modem->num_ttys
			&& modem->profile->cfg.datidx < modem->num_ttys) {
				snprintf(modem->ctl_tty, sizeof(modem->ctl_tty), "%s", strrchr(gl_tty.gl_pathv[modem->profile->cfg.ctlidx], '/') + 1);
				snprintf(modem->dat_tty, sizeof(modem->dat_tty), "%s", strrchr(gl_tty.gl_pathv[modem->profile->cfg.datidx], '/') + 1);
				syslog(LOG_INFO, "%s: Using control tty \"%s\" and data tty \"%s\"", modem->device_id, modem->ctl_tty, modem->dat_tty);
			} else {
				syslog(LOG_WARNING, "%s: Profile \"%s\" is invalid, control index (%d) or data index (%d) is more than number largest available tty index (%zu)", modem->device_id, modem->profile->name, modem->profile->cfg.ctlidx, modem->profile->cfg.datidx, modem->num_ttys - 1);
				modem->profile = NULL;
			}
		}

		if (modem->profile || !(filter->flags & UMTS_FILTER_PROFILE)) {
			syslog(LOG_INFO, "%s: Found usable USB device (0x%04x:0x%04x)", modem->device_id, modem->vendor, modem->device);
			found = true;

			/* Call the callback, if any. If there is no
			 * callback, just return the first match. */
			if (func) {
				func(modem);
			} else {
				globfree(&gl_tty);
				break;
			}
		}
		globfree(&gl_tty);
	}

	globfree(&gl);

	return (found ? UMTS_OK : UMTS_ENODEV);
}

/**
 * Helper function to pint a modem to stdout.
 */
static void umts_modem_print(struct umts_modem *modem) {
	printf("Device: %s\n", modem->device_id);
	printf("\tVendor: 0x%04x\n", modem->vendor);
	printf("\tProduct: 0x%04x\n", modem->device);
	printf("\tDriver: %s\n", modem->driver);
	printf("\tTTYCount: %zu\n", modem->num_ttys);
	if (modem->profile) {
		printf("\tProfile: %s\n", modem->profile->name);
		if (modem->profile->desc) printf("\tProfiledesc: %s\n", modem->profile->desc);
	}
}

/**
 * Detect (potentially) usable devices and list them on stdout.
 */
int umts_modem_list_devices(const struct umts_state *state, struct umts_device_filter *filter) {
	syslog(LOG_NOTICE, "Listing usable devices");
	/* Allocate some storage for umts_modem_find_devices to work */
	struct umts_modem modem;
	int e = umts_modem_find_devices(state, &modem, umts_modem_print, filter);
	if (e == UMTS_ENODEV) {
		syslog(LOG_NOTICE, "No devices found");
		return UMTS_OK;
	} else if (e != UMTS_OK) {
		syslog(LOG_ERR, "Error while detecting devices");
		return e;
	}

	return UMTS_OK;
}

/* Parse a single uci section of type umtsd_profile into a profile */
static int umts_modem_parse_profile(const struct uci_section *s, struct umts_profile *p) {
	p->name = strdup(s->e.name);
	p->flags = UMTS_PROFILE_FROMUCI | UMTS_PROFILE_NOVENDOR | UMTS_PROFILE_NODEVICE;
	struct uci_element *e;
	uci_foreach_element(&s->options, e) {
		struct uci_option *o = uci_to_option(e);
		if (o->type != UCI_TYPE_STRING) continue;

		if (!strcmp(o->e.name, "desc"))
			p->desc = strdup(o->v.string);
		else if (!strcmp(o->e.name, "control"))
			p->cfg.ctlidx = strtoul(o->v.string, NULL, 10);
		else if (!strcmp(o->e.name, "data"))
			p->cfg.datidx = strtoul(o->v.string, NULL, 10);
		else if (!strcmp(o->e.name, "vendor")) {
			p->vendor = strtoul(o->v.string, NULL, 16);
			p->flags &= ~UMTS_PROFILE_NOVENDOR;
		} else if (!strcmp(o->e.name, "product")) {
			p->device = strtoul(o->v.string, NULL, 16);
			p->flags &= ~UMTS_PROFILE_NODEVICE;
		} else if (!strncmp(o->e.name, "mode_", 5)) {
			/* Name starts with mode_ */
			for (int i=0; i < UMTS_NUM_MODES; ++i)
				if (!strcmp(o->e.name + 5, umts_modem_modestr(i)))
					/* And ends with this mode name */
					p->cfg.modecmd[i] = strdup(o->v.string);
		} else {
			syslog(LOG_WARNING, "Uci section %s contains unknown option: %s", s->e.name, o->e.name);
		}
	}

	return UMTS_OK;
}

/**
 * Load additional profiles from the uci configuration.
 */
int umts_modem_load_profiles(struct umts_state *state) {
	struct uci_ptr ptr = {0};
	ptr.package = state->uciname;
	uci_lookup_ptr(state->uci, &ptr, NULL, false);
	struct uci_element *se;
	uci_foreach_element(&ptr.p->sections, se) {
		struct uci_section *s = uci_to_section(se);
		if (!strcmp("umtsd_profile", s->type)) {
			struct umts_profile_list *l = calloc(1, sizeof (struct umts_profile_list));
			if (umts_modem_parse_profile(s, &l->p) != UMTS_OK) {
				free(l);
				continue;
			}

			syslog(LOG_INFO, "Loaded profile \"%s\" from uci", l->p.name);
			list_add(&l->h, &state->custom_profiles);
		}
	}
	return UMTS_OK;
}


static void display_profile(const struct umts_profile *p) {
		printf("Profile: %s\n", p->name);
		printf("\tFromUci: %s\n", p->flags & UMTS_PROFILE_FROMUCI ? "Yes" : "No");
		if (p->desc) printf("\tDesc: %s\n", p->desc);
		if (p->driver) printf("\tDriver: %s\n", p->driver);
		if (!(p->flags & UMTS_PROFILE_NOVENDOR)) printf("\tVendor: 0x%04x\n", p->vendor);
		if (!(p->flags & UMTS_PROFILE_NODEVICE)) printf("\tProduct: 0x%04x\n", p->device);
		printf("\tControl: %d\n", p->cfg.ctlidx);
		printf("\tData: %d\n", p->cfg.datidx);
		for (int mode = 0; mode < UMTS_NUM_MODES; ++mode)
			if (p->cfg.modecmd[mode]) printf("\tMode-%s: %s\n", umts_modem_modestr(mode), p->cfg.modecmd[mode]);
		printf("\n");
}
/**
 * Output a list of all known profiles on stdout.
 */
int umts_modem_list_profiles(const struct umts_state *state) {
	struct umts_profile_list *l;
	list_for_each_entry(l, &state->custom_profiles, h) {
		display_profile(&l->p);
	}

	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		const struct umts_profile *p = &profiles[i];
		display_profile(p);
	}
	return 0;
}
