#ifndef UMTS_DEVICECONFIG_H_
#define UMTS_DEVICECONFIG_H_

#include "umtsd.h"

/// *******************
/// MODEM CONFIGURATION
/// *******************


static struct umts_device {
	uint16_t vendor;
	uint16_t device;
	const struct umts_config cfg;
} devices[] = {
// DEVICE CONFIGS
	{
		.vendor = 0x0bdb,	// Ericsson
		.device = 0x1900,	// F3705G
		.cfg = {
			.ctlidx = 1,
			.datidx = 0,
			.modecmd = {
				[UMTS_MODE_AUTO] = "AT+CFUN=1\r",
				[UMTS_FORCE_UMTS] = "AT+CFUN=6\r",
				[UMTS_FORCE_GPRS] = "AT+CFUN=5\r",
			},
		},
	},

	{
		.vendor = 0x1bbb,	// alcatel
		.device = 0x0000,	// X060s
		.cfg = {
			.ctlidx = 1,
			.datidx = 2,
			.modecmd = {
				[UMTS_MODE_AUTO] = "",
			},
		},
	},

	{
		.vendor = 0x12d1,	// Huawei
		.device = 0x1001,	// K3520
		.cfg = {
			.ctlidx = 2,
			.datidx = 0,
			.modecmd = {
				[UMTS_MODE_AUTO] = "AT^SYSCFG=2,2,40000000,2,4\r",	// Set auto = prefer UMTS
				[UMTS_FORCE_UMTS] = "AT^SYSCFG=14,2,40000000,2,4\r",
				[UMTS_FORCE_GPRS] = "AT^SYSCFG=13,1,40000000,2,4\r",
				[UMTS_PREFER_UMTS] = "AT^SYSCFG=2,2,40000000,2,4\r",
				[UMTS_PREFER_GPRS] = "AT^SYSCFG=2,1,40000000,2,4\r",
			},
		},
	},


// VENDOR DEFAULT CONFIGS
	{
		.vendor = 0x12d1,	// Huawei
		.cfg = {
			.ctlidx = 1,
			.datidx = 0,
			.modecmd = {
				[UMTS_MODE_AUTO] = "AT^SYSCFG=2,2,40000000,2,4\r",	// Set auto = prefer UMTS
				[UMTS_FORCE_UMTS] = "AT^SYSCFG=14,2,40000000,2,4\r",
				[UMTS_FORCE_GPRS] = "AT^SYSCFG=13,1,40000000,2,4\r",
				[UMTS_PREFER_UMTS] = "AT^SYSCFG=2,2,40000000,2,4\r",
				[UMTS_PREFER_GPRS] = "AT^SYSCFG=2,1,40000000,2,4\r",
			},
		},
	},
	{
		.vendor = 0x19d2,	// ZTE
		.cfg = {
			.ctlidx = 1,
			.datidx = 2,
			.modecmd = {
				[UMTS_MODE_AUTO] = "AT+ZSNT=0,0,0\r",
				[UMTS_FORCE_UMTS] = "AT+ZSNT=2,0,0\r",
				[UMTS_FORCE_GPRS] = "AT+ZSNT=1,0,0\r",
				[UMTS_PREFER_UMTS] = "AT+ZSNT=0,0,2\r",
				[UMTS_PREFER_GPRS] = "AT+ZSNT=0,0,1\r",
			},
		},
	},

};


// DRIVER DEFAULT CONFIGS
static struct umts_driver {
	const char *name;
	const struct umts_config cfg;
} drivers[] = {
	{
		.name = "option",
		.cfg = {
				.ctlidx = 1,
				.datidx = 0,
				.modecmd = {
					[UMTS_MODE_AUTO] = "",
				},
		},
	},
	{
		.name = "sierra",
		.cfg = {
				.ctlidx = 0,
				.datidx = 2,
				.modecmd = {
					[UMTS_MODE_AUTO] = "",
				},
		},
	},
	{
		.name = "hso",
		.cfg = {
			.ctlidx = 0,
			.datidx = 3,
			.modecmd = {
				[UMTS_MODE_AUTO] = "at_opsys=2,2\r",	// Set auto = prefer UMTS
				[UMTS_FORCE_UMTS] = "at_opsys=1,2\r",
				[UMTS_FORCE_GPRS] = "at_opsys=0,2\r",
				[UMTS_PREFER_UMTS] = "at_opsys=2,2\r",
				[UMTS_PREFER_GPRS] = "at_opsys=3,2\r",
			},
		},
	},
};

#endif /* UMTS_DEVICECONFIG_H_ */
