umtsd2 - 3G modem dialing daemon
================================

`umtsd2` is a daemon that can talk to various 3G devices, like dongles and
phones, to make them set up a PPP data connection to the internet.

These modems use the old-school AT command set, using some new specific
commands. However, not all devices implement exactly the same protocol,
so some tweaking is sometimes needed for specific devices. The most
common tweak is to change the serial channels to be used for
communication.

Compiling
=========
When the dependencies are properly installed, compiling should be a matter or
running `make`.

If you need to provide specific `CFLAGS` etc. for your system,
you can create a `Makefile.local` file which will get included from the
main `Makefile`.

Dependencies
============
`umtsd2` currently runs only on Linux, since it makes assumptions about
the existence of `/dev/ttyUSBx` files to use for communicating with the
devices.

`umtsd2` compiles against two libraries: [uci][1] for its configuration storage
and [libubox][2] for some general utilities.

[1]: http://nbd.name/gitweb.cgi?p=uci.git;a=summary
[2]: http://nbd.name/gitweb.cgi?p=luci2/libubox.git;a=summary

Furthermore, it requires [pppd][3] to set up the actual connection and, for a lot of
devices, requires [usb-modeswitch][4] to put the device into modem mode befor
running `umtsd2`.

[3]: http://ppp.samba.org/
[4]: http://www.draisberghof.de/usb_modeswitch/

Even though `umtsd2`, uci and libubox are intended to run on OpenWRT,
they can be compiled on a regular Linux system as well. Here's is the
short-short-version of that (all command output was removed, except for the
final `umtsd` run):

	~$ mkdir work_dir
	~$ cd work_dir/
	~/work_dir$ git clone git://nbd.name/uci.git
	~/work_dir$ git clone git://nbd.name/luci2/libubox.git
	~/work_dir$ git clone git@github.com:matthijskooijman/umtsd2.git
	~/work_dir$ cd libubox
	~/work_dir/libubox$ cmake .
	~/work_dir/libubox$ make
	~/work_dir/libubox$ sudo cp libubox.so /usr/local/lib/
	~/work_dir/libubox$ cd ../uci
	~/work_dir/uci$ cmake .
	~/work_dir/uci$ make
	~/work_dir/uci$ sudo cp libuci.so /usr/local/lib/
	~/work_dir/uci$ sudo ldconfig
	~/work_dir/uci$ sudo cp uci /usr/local/bin/
	~/work_dir/uci$ cd ../umtsd2
	~/work_dir/umtsd2$ echo 'CFLAGS=-I../ -I../uci' > Makefile.local
	~/work_dir/umtsd2$ make
	~/work_dir/umtsd2$ sudo mkdir /etc/config
	~/work_dir/umtsd2$ sudo uci import network << EOF
	config network wan
	        option umts_apn "internet"
	EOF
	matthijs@grubby:~/docs/Fon/src/work_dir/umtsd2$ sudo ./umtsd
	umtsd[967]: ttyUSB1: Using control tty 1
	umtsd[967]: ttyUSB1: Using data tty 0
	umtsd[967]: ttyUSB1: Found option modem 12d1:1003
	umtsd[967]: ttyUSB1: Supported modes: auto force-umts force-gprs prefer-umts prefer-gprs
	umtsd[967]: ttyUSB1: Identified as huawei E220
	umtsd[967]: ttyUSB1: SIM card is ready
	umtsd[967]: ttyUSB1: Detected a GSM modem
	umtsd[967]: ttyUSB1: Mode set to auto
	umtsd[967]: ttyUSB1: Provider is T-Mobile  NL
	umtsd[967]: ttyUSB1: RSSI is 7
	umtsd[970]: ttyUSB1: Preparing to dial
	umtsd[970]: ttyUSB1: Echo disabled
	umtsd[970]: ttyUSB1: Modem reset
	umtsd[970]: ttyUSB1: Selected APN internet. Now dialing...
	umtsd[970]: ttyUSB1: Connected. Handover to pppd.
	Serial connection established.
	Using interface ppp0
	Connect: ppp0 <--> /dev/ttyUSB1
	No CHAP secret found for authenticating us to UMTS_CHAP_SRVR
	CHAP authentication succeeded
	CHAP authentication succeeded
	Could not determine remote IP address: defaulting to 10.64.64.64
	not replacing existing default route via 192.168.1.252
	local  IP address 178.227.114.29
	remote IP address 10.64.64.64
	primary   DNS address 84.241.226.9
	secondary DNS address 84.241.226.140

You might need to set other settings to get this working, see below.

Running
=======
TODO

Configuration
=============
TODO (see src/umts-network-uci.txt)

History
=======
`umtsd2` has been developed for Fon, for use in their Fonera routers.
During the beginning of the 2013, the sources for `umtsd2` have been
published under the GPL, to allow the OpenWRT project to also start
using `umtsd2`.

`umtsd2` is the successor to umtsd.lua, which is a rough `lua`
implementation with the same goals that is shipped on the "fon-ng"
firmware for the Fonera 2.0g and 2.0n.

Licensing
=========
 - © 2010 John Crispin <<blogic@openwrt.org>>
 - © 2010 Steven Barth <<steven@midlink.org>>
 - © 2011-2013 Matthijs Kooijman <<matthijs@stdin.nl>>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.

The full text of the GNU General Public License, version 2 is
distributed in the `COPYING` file in this package.
