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

#include "udiald.h"
#include <limits.h>
#include <string.h>
#include <syslog.h>

#include "deviceconfig.h"

#define UDIALD_SYS_USB_DEVICES "/sys/bus/usb/devices/*"

static const char *modestr[] = {
	[UDIALD_MODE_AUTO] = "auto",
	[UDIALD_FORCE_UMTS] = "force-umts",
	[UDIALD_FORCE_GPRS] = "force-gprs",
	[UDIALD_PREFER_UMTS] = "prefer-umts",
	[UDIALD_PREFER_GPRS] = "prefer-gprs"
};

// mode no -> mode string
const char* udiald_modem_modestr(enum udiald_mode mode) {
	return modestr[mode];
}

// mode string -> mode no
enum udiald_mode udiald_modem_modeval(const char *mode) {
	for (size_t i = 0; i < sizeof(modestr)/sizeof(*modestr); ++i)
		if (!strcmp(mode, modestr[i]))
			return i;
	return -1;
}


/**
 * Check if the given profile matches the given modem (or, if a name is
 * given, has the given name).
 */
static int match_profile(struct udiald_modem *modem, const struct udiald_profile *p, const char *profile_name) {
	if (profile_name && !strcmp(p->name, profile_name)) {
		modem->profile = p;
		syslog(LOG_NOTICE, "%s: Selected requested configuration profile \"%s\" (%s)", modem->device_id, p->name, p->desc);
		return UDIALD_OK;
	}
	if (!profile_name
	&& ((p->flags & UDIALD_PROFILE_NOVENDOR) || p->vendor == modem->vendor)
	&& ((p->flags & UDIALD_PROFILE_NODEVICE) || p->device == modem->device)
	&& (!p->driver || !strcmp(p->driver, modem->driver))) {
		modem->profile = p;

		if (p->vendor)
			syslog(LOG_INFO, "%s: Matched USB vendor id 0x%x", modem->device_id, p->vendor);
		if (p->device)
			syslog(LOG_INFO, "%s: Matched USB product id 0x%x", modem->device_id, p->device);
		if (p->driver)
			syslog(LOG_INFO, "%s: Matched driver name \"%s\"", modem->device_id, p->driver);
		syslog(LOG_NOTICE, "%s: Autoselected configuration profile \"%s\" (%s)", modem->device_id, p->name, p->desc);
		return UDIALD_OK;
	}
	return UDIALD_ENODEV;
}

/**
 * Find a profile matching the attributes passed. The found profile is
 * stored in modem->profile.
 *
 * Returns UDIALD_OK when a profile was found or UDIALD_ENODEV when there
 * was no applicable profile.
 */
static int udiald_modem_find_profile(const struct udiald_state *state, struct udiald_modem *modem, const char *profile_name) {
	// Match profiles loaded from uci first
	struct udiald_profile_list *l;
	list_for_each_entry(l, &state->custom_profiles, h) {
		if (match_profile(modem, &l->p, profile_name) == UDIALD_OK)
			return UDIALD_OK;
	}
	// Find the first profile that has all of its conditions
	// matching. The array is ordered so that specific devices are
	// matched first, then generic per-vendor profiles and then
	// generic per-driver profiles.
	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		if (match_profile(modem, &profiles[i], profile_name) == UDIALD_OK)
			return UDIALD_OK;
	}

	return UDIALD_ENODEV;
}

/**
 * Scan the list of USB devices for any device that looks like a usable
 * device.
 *
 * When func is NULL, detection stops at the first usable device, which
 * is returned in *modem.
 *
 * When func is not null, it is called for every device detected
 * (passing the detectet device and data argument). The contents of
 * *modem are undefined when this function returns.
 *
 * When no modems were found, this function returns UDIALD_ENODEV.
 * If at least one modem was detected, it returns UDIALD_OK.
 */
int udiald_modem_find_devices(const struct udiald_state *state, struct udiald_modem *modem, void func(struct udiald_modem *, void *), void *data, struct udiald_device_filter *filter) {
	if (func)
		syslog(LOG_INFO, "Detecting usable devices");
	else
		syslog(LOG_INFO, "Detecting first usable device");

	if (filter->flags & UDIALD_FILTER_VENDOR)
		syslog(LOG_INFO, "Only considering devices with vendor id 0x%x", filter->vendor);
	if (filter->flags & UDIALD_FILTER_DEVICE)
		syslog(LOG_INFO, "Only considering devices with product id 0x%x", filter->device);
	if (filter->device_id)
		syslog(LOG_INFO, "Only considering device with device id %s", filter->device_id);

	bool found = false;
	glob_t gl;
	char buf[PATH_MAX + 1];
	int e = udiald_util_checked_glob(UDIALD_SYS_USB_DEVICES, GLOB_NOSORT, &gl, "listing USB devices");
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
		if (udiald_util_read_hex_word(buf, &modem->vendor)) continue;
		snprintf(buf, sizeof(buf), "%s/%s", path, "idProduct");
		if (udiald_util_read_hex_word(buf, &modem->device)) continue;

		/* Check commandline vidpid filter */
		if (((filter->flags & UDIALD_FILTER_VENDOR) && (filter->vendor != modem->vendor))
		|| ((filter->flags & UDIALD_FILTER_DEVICE) && (filter->device != modem->device))) {
			syslog(LOG_DEBUG, "%s: Skipping device (0x%04x:0x%04x) due to commandline filter", device_id, modem->vendor, modem->device);
			continue;
		}

		syslog(LOG_DEBUG, "%s: Considering device (0x%04x:0x%04x)", device_id, modem->vendor, modem->device);

		/* Find out how many tty devices this USB device
		 * exports. */
		snprintf(buf, sizeof(buf), "%s/*/tty*", path);
		glob_t gl_tty;
		int e = udiald_util_checked_glob(buf, 0, &gl_tty, "listing tty devices");
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

		udiald_util_read_symlink_basename(buf, modem->driver, sizeof(modem->driver));
		syslog(LOG_DEBUG, "%s: Detected driver \"%s\"", device_id, modem->driver);

		snprintf(modem->device_id, sizeof(modem->device_id), "%s", device_id);

		/* Find an applicable profile */
		udiald_modem_find_profile(state, modem, filter->profile_name);

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

		if (modem->profile || !(filter->flags & UDIALD_FILTER_PROFILE)) {
			syslog(LOG_INFO, "%s: Found usable USB device (0x%04x:0x%04x)", modem->device_id, modem->vendor, modem->device);
			found = true;

			/* Call the callback, if any. If there is no
			 * callback, just return the first match. */
			if (func) {
				func(modem, data);
			} else {
				globfree(&gl_tty);
				break;
			}
		}
		globfree(&gl_tty);
	}

	globfree(&gl);

	return (found ? UDIALD_OK : UDIALD_ENODEV);
}

static struct json_object *profile_to_json(const struct udiald_profile *p) {
	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj, "name", json_object_new_string(p->name));
	json_object_object_add(obj, "internal", json_object_new_boolean(!(p->flags & UDIALD_PROFILE_FROMUCI)));
	if (p->desc)
		json_object_object_add(obj, "description", json_object_new_string(p->desc));
	if (p->driver)
		json_object_object_add(obj, "driver", json_object_new_string(p->driver));

	if (!(p->flags & UDIALD_PROFILE_NOVENDOR)) {
		json_object_object_add(obj, "vendor", udiald_util_sprintf_json_string("0x%04x", p->vendor));
		json_object_object_add(obj, "vendor_int", json_object_new_int(p->vendor));
	}
	if (!(p->flags & UDIALD_PROFILE_NODEVICE)) {
		json_object_object_add(obj, "product", udiald_util_sprintf_json_string("0x%04x", p->device));
		json_object_object_add(obj, "product_int", json_object_new_int(p->device));
	}
	json_object_object_add(obj, "control", json_object_new_int(p->cfg.ctlidx));
	json_object_object_add(obj, "data", json_object_new_int(p->cfg.datidx));
	struct json_object *modes = json_object_new_object();
	for (int mode = 0; mode < UDIALD_NUM_MODES; ++mode) {
		if (p->cfg.modecmd[mode])
			json_object_object_add(modes, udiald_modem_modestr(mode), json_object_new_string(p->cfg.modecmd[mode]));
	}
	json_object_object_add(obj, "modes", modes);

	return obj;
}

struct device_display_data {
	enum udiald_display_format format;
	union {
		struct json_object *array;
	} data;
};

/**
 * Helper function to add a json version of a device to an array.
 */
static void display_device(struct udiald_modem *modem, void *data) {
	struct device_display_data *d = (struct device_display_data *)data;
	if (d->format == UDIALD_FORMAT_JSON) {
		struct json_object *obj = json_object_new_object();

		json_object_object_add(obj, "id", json_object_new_string(modem->device_id));
		json_object_object_add(obj, "vendor", udiald_util_sprintf_json_string("0x%04x", modem->vendor));
		json_object_object_add(obj, "vendor_int", json_object_new_int(modem->vendor));
		json_object_object_add(obj, "product", udiald_util_sprintf_json_string("0x%04x", modem->device));
		json_object_object_add(obj, "product_int", json_object_new_int(modem->device));
		json_object_object_add(obj, "driver", json_object_new_string(modem->driver));
		json_object_object_add(obj, "ttys", json_object_new_int(modem->num_ttys));

		if (modem->profile) {
			json_object_object_add(obj, "profile", profile_to_json(modem->profile));
		}

		json_object_array_add(d->data.array, obj);
	} else if (d->format == UDIALD_FORMAT_ID) {
		printf("%s\n", modem->device_id);
	}
}

/**
 * Detect (potentially) usable devices and list them on stdout.
 */
int udiald_modem_list_devices(const struct udiald_state *state, struct udiald_device_filter *filter) {
	syslog(LOG_NOTICE, "Listing usable devices");
	/* Allocate some storage for udiald_modem_find_devices to work */
	struct udiald_modem modem;
	struct device_display_data data = {
		.format = state->format,
	};
	if (state->format == UDIALD_FORMAT_JSON)
		data.data.array = json_object_new_array();

	int e = udiald_modem_find_devices(state, &modem, display_device, &data, filter);
	if (e == UDIALD_ENODEV) {
		syslog(LOG_NOTICE, "No devices found");
	} else if (e != UDIALD_OK) {
		syslog(LOG_ERR, "Error while detecting devices");
	}
	if (state->format == UDIALD_FORMAT_JSON)
		printf("%s\n", json_object_to_json_string(data.data.array));
	return e;
}

/* Parse a single uci section of type udiald_profile into a profile */
static int udiald_modem_parse_profile(const struct uci_section *s, struct udiald_profile *p) {
	p->name = strdup(s->e.name);
	p->flags = UDIALD_PROFILE_FROMUCI | UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE;
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
			p->flags &= ~UDIALD_PROFILE_NOVENDOR;
		} else if (!strcmp(o->e.name, "product")) {
			p->device = strtoul(o->v.string, NULL, 16);
			p->flags &= ~UDIALD_PROFILE_NODEVICE;
		} else if (!strncmp(o->e.name, "mode_", 5)) {
			/* Name starts with mode_ */
			for (int i=0; i < UDIALD_NUM_MODES; ++i)
				if (!strcmp(o->e.name + 5, udiald_modem_modestr(i)))
					/* And ends with this mode name */
					p->cfg.modecmd[i] = strdup(o->v.string);
		} else {
			syslog(LOG_WARNING, "Uci section %s contains unknown option: %s", s->e.name, o->e.name);
		}
	}

	return UDIALD_OK;
}

/**
 * Load additional profiles from the uci configuration.
 */
int udiald_modem_load_profiles(struct udiald_state *state) {
	struct uci_ptr ptr = {0};
	ptr.package = state->uciname;
	uci_lookup_ptr(state->uci, &ptr, NULL, false);
	struct uci_element *se;
	uci_foreach_element(&ptr.p->sections, se) {
		struct uci_section *s = uci_to_section(se);
		if (!strcmp("udiald_profile", s->type)) {
			struct udiald_profile_list *l = calloc(1, sizeof (struct udiald_profile_list));
			if (udiald_modem_parse_profile(s, &l->p) != UDIALD_OK) {
				free(l);
				continue;
			}

			syslog(LOG_INFO, "Loaded profile \"%s\" from uci", l->p.name);
			list_add(&l->h, &state->custom_profiles);
		}
	}
	return UDIALD_OK;
}

/**
 * Output a list of all known profiles on stdout.
 */
int udiald_modem_list_profiles(const struct udiald_state *state) {
	struct udiald_profile_list *l;
	struct json_object *array = NULL;
	if (state->format == UDIALD_FORMAT_JSON)
		array = json_object_new_array();
	list_for_each_entry(l, &state->custom_profiles, h) {
		if (state->format == UDIALD_FORMAT_JSON)
			json_object_array_add(array, profile_to_json(&l->p));
		else
			printf("%s\n", l->p.name);
	}

	for (size_t i = 0; i < (sizeof(profiles) / sizeof(*profiles)); ++i) {
		const struct udiald_profile *p = &profiles[i];
		if (state->format == UDIALD_FORMAT_JSON)
			json_object_array_add(array, profile_to_json(p));
		else
			printf("%s\n", p->name);
	}
	if (state->format == UDIALD_FORMAT_JSON)
		printf("%s\n", json_object_to_json_string(array));
	return 0;
}
