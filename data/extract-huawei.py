#!/usr/bin/env python

# This script is written to parse the 50-Huawei-Datacard.rules from the
# "HUAWEI Data Cards Linux Driver" available from Huawei.
#
# Run as:
#   ./extract-huawei.py < 50-Huawei-Datacard.rules > deviceconfig_huawei.h
#
#   Copyright (c) 2013 Matthijs Kooijman <matthijs@stdin.nl>
#
#   Permission is hereby granted, free of charge, to any person
#   obtaining a copy of this software and associated documentation files
#   (the "Software"), to deal in the Software without restriction,
#   including without limitation the rights to use, copy, modify, merge,
#   publish, distribute, sublicense, and/or sell copies of the Software,
#   and to permit persons to whom the Software is furnished to do so,
#   subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be
#   included in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
#   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
#   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#   SOFTWARE.

import os
import re
import sys

# Map (vid, pid) => devicename
devnames = {
    (0x12d1, 0x1001): "Huawei K3520 / E1752 / E620",
    (0x12d1, 0x1003): "Huawei E220",
    (0x12d1, 0x1433): "Huawei E173",
}

def output(vid, pid, control, data):
    if not vid or not pid:
        return

    vid = int(vid, 16)
    pid = int(pid, 16)

    if control is None or data is None:
        # Some of these devices apparently only do CDC_ether or
        # have some other (non AT modem) interface
        return

    try:
        devname = devnames[(vid, pid)]
    except KeyError:
        devname = "Huawei {vid:x}:{pid:x}".format(vid = vid, pid = pid)

    print("""
	{{
		.name   = "{vid:X}{pid:X}",
		.desc   = "{devname}",
		.vendor = 0x{vid:x},
		.device = 0x{pid:x},
		.cfg = {{
			.ctlidx = {control},
			.datidx = {data},
			.modecmd = HUAWEI_SYSCFG_MODECMD,
		}},
	}},""".format(devname = devname, vid = vid, pid = pid, control = control, data = data))

vid = None
pid = None
tty_num = 0
control = None
data = None

TTY_LINE = re.compile(r'ATTRS{modalias}=="usb:v([0-9A-F]+)p([0-9A-F]+)\*".*KERNEL=="tty.*"')
SYMLINK_DATA = re.compile(r'SYMLINK\+="ttyUSB_utps_modem"')
SYMLINK_CONTROL = re.compile(r'SYMLINK\+="ttyUSB_utps_pcui"')

print(
"""
// This file is autogenerated by %s. Do not make
// changes to it directly, change deviceconfig.h instead.
// Also, don't include this file, include deviceconfig.h.
""" % os.path.basename(__file__))

for line in sys.stdin:
    match = TTY_LINE.search(line)
    if match:
        if match.group(1) != vid or match.group(2) != pid:
            # new device. Output previous one and reset state
            output(vid, pid, control, data)
            vid = match.group(1)
            pid = match.group(2)
            tty_num = 0
            control = None
            data = None
        else:
            # We assume that the ttys are listed in order and none are
            # left out. Ideally, we would instead use the USB
            # bInterfaceNumber, but udiald does not support this
            # currently.
            tty_num += 1

        if SYMLINK_DATA.search(line):
            if data is not None:
                sys.stderr.write("Warning: Duplicate data tty found for %s:%s\n" % (vid, pid))
            data = tty_num

        if SYMLINK_CONTROL.search(line):
            if control is not None:
                sys.stderr.write("Warning: Duplicate control tty found for %s:%s\n" % (vid, pid))
            control = tty_num

# Output the last device
output(vid, pid, control, data)
