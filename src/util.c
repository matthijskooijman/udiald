/**
 *   udiald - UMTS connection manager
 *   Copyright (C) 2013 Matthijs Kooijman <matthijs@stdin.nl>
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


#define _GNU_SOURCE // Get vasprintf

#include "udiald.h"
#include <libgen.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * A version of glob that checks the return value and in case of error,
 * reports a log message and returns an udiald_errorcode instead.
 *
 * Compared to regular glob, the errfunc parameter is left out, but the
 * activity parameter is added, which should be a string for use in
 * error messages.
 */
int udiald_util_checked_glob(const char *pattern, int flags, glob_t *pglob, const char *activity) {
	int e = glob(pattern, flags, NULL, pglob);

	if (e == 0) {
		return UDIALD_OK;
	} else if (e == GLOB_NOMATCH) {
		return UDIALD_ENODEV;
	} else if (errno) {
		syslog(LOG_CRIT, "Glob error while %s: %s", activity, strerror(errno));
		return UDIALD_EINTERNAL;
	} else {
		syslog(LOG_CRIT, "Unknown glob error while %s", activity);
		return UDIALD_EINTERNAL;
	}
}

/**
 * Parse a 16 bit word from the given string, converting it from a hex
 * string to a real int.
 */
int udiald_util_parse_hex_word(const char *hex, uint16_t *res) {
	char *end;
	*res = strtoul(hex, &end, 16);
	if (*end != '\0') {
		syslog(LOG_DEBUG, "Failed to convert hex word (read: \"%s\")", hex);
		return UDIALD_EINVAL;
	}
	return UDIALD_OK;
}

/**
 * Read a 16 bit word from a file, converting it from a hex string to a
 * real int.
 *
 * If an error occurs, a DEBUG message is logged, errno is reset and
 * UDIALD_EINVAL is returned.
 */
int udiald_util_read_hex_word(const char *path, uint16_t *res) {
	const int hex_bytes = sizeof(*res) * 2;
	char buf[hex_bytes + 1];

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		syslog(LOG_DEBUG, "%s: Failed to open: %s", path, strerror(errno));
		errno = 0;
		return UDIALD_EINVAL;
	}

	int n = read(fd, buf, hex_bytes);
	close(fd);
	if (n != hex_bytes) {
		syslog(LOG_DEBUG, "%s: Failed to read %d bytes (got %d): %s", path, hex_bytes, n, strerror(errno));
		errno = 0;
		return UDIALD_EINVAL;
	}

	buf[hex_bytes] = '\0';

	return udiald_util_parse_hex_word(buf, res);
}

/**
 * Reads the target of a symlink and returns the basename of that target
 * in res.
 */
void udiald_util_read_symlink_basename(const char *path, char *res, size_t size) {
	char buf[PATH_MAX];
	int n = readlink(path, buf, sizeof(buf));
	buf[n] = '\0';
	snprintf(res, size, "%s", basename(buf));
}

/*
 * Create a json_object string from a sprintf format and arguments.
 */
struct json_object *udiald_util_sprintf_json_string(const char *fmt, ...) {
	va_list ap;
	char *str;

	va_start(ap, fmt);
	if (vasprintf(&str, fmt, ap) < 0) {
		syslog(LOG_ERR, "Failed to sprintf (format: %s)", fmt);
		return NULL;
	}
	va_end(ap);

	json_object *obj = json_object_new_string(str);
	free(str);
	return obj;
}
