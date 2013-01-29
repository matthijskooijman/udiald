#ifndef UDIALD_DEVICECONFIG_H_
#define UDIALD_DEVICECONFIG_H_

#include "udiald.h"

/// ****************************
/// MODEM CONFIGURATION PROFILES
/// ****************************
//
// Do not include this file from anywhere else than modem.c, since that
// will cause this data to be duplicated in the final binary. If you
// need anything from here, go through a function in modem.c.


/* Make sure that the correct ordering of this array is observed: First
 * specific devices, then generic per-vendor profiles and lastly generic
 * per-driver profiles.
 *
 * When autoselecting a profile from this list, the first entry that has
 * all of its conditions (vendor, device, driver) matched will be used.
 *
 * Also note that the name of a profile should never change, since
 * users might have a profile selected for their device, which should
 * remain working after an upgrade. The description can always be
 * changed.
 */
static const struct udiald_profile profiles[] = {
	{
		.name   = "0BDB3705G",
		.desc   = "Ericsson F3705G",
		.vendor = 0x0bdb,
		.device = 0x1900,
		.cfg = {
			.ctlidx = 1,
			.datidx = 0,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "AT+CFUN=1\r",
				[UDIALD_FORCE_UMTS] = "AT+CFUN=6\r",
				[UDIALD_FORCE_GPRS] = "AT+CFUN=5\r",
			},
		},
	},

	{
		.name   = "1BBB000",
		.desc   = "Alcatel X060s",
		.vendor = 0x1bbb,
		.device = 0x0000,
		.cfg = {
			.ctlidx = 1,
			.datidx = 2,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "",
			},
		},
	},

	{
		.name   = "12D11001",
		.desc = "Huawei K3520",
		.vendor = 0x12d1,
		.device = 0x1001,
		.cfg = {
			.ctlidx = 2,
			.datidx = 0,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "AT^SYSCFG=2,2,40000000,2,4\r",	// Set auto = prefer UMTS
				[UDIALD_FORCE_UMTS] = "AT^SYSCFG=14,2,40000000,2,4\r",
				[UDIALD_FORCE_GPRS] = "AT^SYSCFG=13,1,40000000,2,4\r",
				[UDIALD_PREFER_UMTS] = "AT^SYSCFG=2,2,40000000,2,4\r",
				[UDIALD_PREFER_GPRS] = "AT^SYSCFG=2,1,40000000,2,4\r",
			},
		},
	},
	{
		.name   = "12D11433",
		.desc   = "Huawei E173",
		.vendor = 0x12d1,
		.device = 0x1433,
		.cfg = {
			.ctlidx = 2,
			.datidx = 0,
			.modecmd = {
				// These haven't been well-tested (just
				// copied from the Huawei generic
				// config). Seems that the device
				// doesn't get carrier after switching
				// from (force-)gprs to umts.
				[UDIALD_MODE_AUTO] = "AT^SYSCFG=2,2,40000000,2,4\r",	// Set auto = prefer UMTS
				[UDIALD_FORCE_UMTS] = "AT^SYSCFG=14,2,40000000,2,4\r",
				[UDIALD_FORCE_GPRS] = "AT^SYSCFG=13,1,40000000,2,4\r",
				[UDIALD_PREFER_UMTS] = "AT^SYSCFG=2,2,40000000,2,4\r",
				[UDIALD_PREFER_GPRS] = "AT^SYSCFG=2,1,40000000,2,4\r",
			},
		},
	},


// VENDOR DEFAULT PROFILES
	{
		.name   = "12D1",
		.desc   = "Huawei generic",
		.vendor = 0x12d1,
		.flags  = UDIALD_PROFILE_NODEVICE,
		.cfg = {
			.ctlidx = 1,
			.datidx = 0,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "AT^SYSCFG=2,2,40000000,2,4\r",	// Set auto = prefer UMTS
				[UDIALD_FORCE_UMTS] = "AT^SYSCFG=14,2,40000000,2,4\r",
				[UDIALD_FORCE_GPRS] = "AT^SYSCFG=13,1,40000000,2,4\r",
				[UDIALD_PREFER_UMTS] = "AT^SYSCFG=2,2,40000000,2,4\r",
				[UDIALD_PREFER_GPRS] = "AT^SYSCFG=2,1,40000000,2,4\r",
			},
		},
	},
	{
		.name   = "19D2",
		.desc   = "ZTE generic",
		.vendor = 0x19d2,
		.flags  = UDIALD_PROFILE_NODEVICE,
		.cfg = {
			.ctlidx = 1,
			.datidx = 2,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "AT+ZSNT=0,0,0\r",
				[UDIALD_FORCE_UMTS] = "AT+ZSNT=2,0,0\r",
				[UDIALD_FORCE_GPRS] = "AT+ZSNT=1,0,0\r",
				[UDIALD_PREFER_UMTS] = "AT+ZSNT=0,0,2\r",
				[UDIALD_PREFER_GPRS] = "AT+ZSNT=0,0,1\r",
			},
		},
	},
// DRIVER PROFILES
	{
		.name   = "option",
		.desc   = "Option generic",
		.driver = "option",
		.flags  = UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE,
		.cfg = {
				.ctlidx = 1,
				.datidx = 0,
				.modecmd = {
					[UDIALD_MODE_AUTO] = "",
				},
		},
	},
	{
		.name   = "sierra",
		.desc   = "Sierra generic",
		.driver = "sierra",
		.flags  = UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE,
		.cfg = {
				.ctlidx = 0,
				.datidx = 2,
				.modecmd = {
					[UDIALD_MODE_AUTO] = "",
				},
		},
	},
	{
		.name   = "hso",
		.desc   = "HSO generic",
		.driver = "hso",
		.flags  = UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE,
		.cfg = {
			.ctlidx = 0,
			.datidx = 3,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "at_opsys=2,2\r",	// Set auto = prefer UMTS
				[UDIALD_FORCE_UMTS] = "at_opsys=1,2\r",
				[UDIALD_FORCE_GPRS] = "at_opsys=0,2\r",
				[UDIALD_PREFER_UMTS] = "at_opsys=2,2\r",
				[UDIALD_PREFER_GPRS] = "at_opsys=3,2\r",
			},
		},
	},
	{
		.name   = "cdc_acm",
		.desc   = "CDC generic",
		.driver = "cdc_acm",
		.flags  = UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE,
		.cfg = {
			/* These are just copied from the option generic
			 * profile */
			.ctlidx = 1,
			.datidx = 0,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "",
			},
		},
	},
	{
		.name   = "usbserial",
		.desc   = "USB serial generic",
		.driver = "usbserial",
		.flags  = UDIALD_PROFILE_NOVENDOR | UDIALD_PROFILE_NODEVICE,
		.cfg = {
			.ctlidx = 0,
			.datidx = 2,
			.modecmd = {
				[UDIALD_MODE_AUTO] = "",
			},
		},
	},
};

#endif /* UDIALD_DEVICECONFIG_H_ */
