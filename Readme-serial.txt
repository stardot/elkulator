Serial port emulation is provided by emulating the SCN2681 dual asynchronous
receiver/transmitter (DUART) device supported by the Plus 1 Electron Expansion
ROM. This document describes the extent of the support and current
limitations, and it should be updated as this support evolves.

To use these emulation features, make sure that the plus1.rom file is present
in the roms directory and that the Plus 1 is enabled in the elk.cfg
configuration file as follows:

  plus1 = 1

To interact with the emulated system over a serial connection, a simple client
program is provided that can be run as follows:

  tools/serial_client.py

This will output the name of a file to be used to connect the client with
Elkulator. For example:

  Waiting for connection at: /tmp/tmpIIrGMc

Alternatively, a filename can be indicated as follows:

  tools/serial_client.py xxx

Elkulator should be run with the -serial option indicating the appropriate
filename. For example:

  ./elkulator -serial xxx

If Elkulator cannot connect to the client, it will start up successfully but
report an error. For example:

  Failed to connect to serial communications socket: No such file or directory

Elkulator can also be asked to provide more information about its serial
emulation activities using the -serialdebug option with a debugging level
argument. For example:

  ./elkulator -serial xxx -serialdebug 1

In the emulated Electron, the following commands are useful:

  *FX 2,1 - enables the RS423 input stream, disabling the keyboard, with the
            client being needed for further input

  *FX 3,1 - enables the RS423 output stream along with the VDU output

  *FX 3,3 - enables only the RS423 output, hiding VDU output

  *FX 5,2 - selects the serial printer which can then be enabled using Ctrl-B
            or VDU 2, and disabled using Ctrl-C or VDU 3

Currently, the serial client performs end-of-line character conversion to
permit its use as a simple console.

----

Support for serial communications is found in the following files:

src/6502.c   - invokes the polling routine for the emulated device
src/main.c   - provides command line options for serial emulation features
src/mem.c    - tests for access to memory addresses exposing the SCN2681 
src/serial.c - provides the basis of the SCN2681 emulation

The SCN2681 has apparently been available in a number of different profiles.
The 40-pin DIP and 44-pin PLCC profiles offer the full range of input and
output pins and ports, whereas the 24-pin and 28-pin DIP profiles offer a
reduced number of pins and ports. Since Electron expansions are likely to use
the smaller packages, and since the Plus 1 ROM code makes limited use of the
SCN2681 feature set, only limited support is provided for input and output
pins and ports.

In this emulation, of the input ports only IP0, IP1 and IP2 are tested, with
IP0 and IP1 potentially being employed for clear-to-send (CTS) control
purposes.  However, only IP2 is actually available on the 28-pin device and no
input ports are provided on the 24-pin device. For output, only OP0 and OP1
are employed, these potentially being employed for request-to-send (RTS)
control purposes. These ports are available on all but the 24-pin device.

The SCN2681 supports a degree of flexibility with regard to clock and
frequency configuration, with various input port pins permitting external
clock signals to be used: IP3 and IP4 for channel A; IP5 and IP6 for channel
B. Frequencies derived from such signals are then exposed via the baud rate
configuration on a real SCN2681 device. This emulation does not support such
external frequencies and the selection of such externally derived frequencies
will inhibit communication.

Currently, none of the detail of serial communication is emulated beyond a
simple attempt to replicate transmission and reception at a given baud rate
and to support different character sizes. The device can be configured for
different parities and stop bits, but such details are not represented in any
communication. Similarly, break signals are recorded but not represented. Such
details might in future be represented using a protocol encoding them over a
normal communications stream, or a more sophisticated medium might be
employed.

Since the first in, first out (FIFO) mechanism for receiving characters is
emulated, overrun errors should be handled. Other forms of error such as
parity and framing errors will not occur since the underlying communications
medium should not produce them. However, support is present for recording and
representing such errors.

Support for various channel features such as auto-echo and loopback is absent.
Similarly, multidrop support is absent. The general counter/timer support is
also not emulated.
