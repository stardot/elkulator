Parallel port emulation is provided by handling writes to address &FC71 when
the Plus 1 is enabled. This document describes the extent of the support and
current limitations, and it should be updated as this support evolves.

To use these emulation features, make sure that the plus1.rom file is present
in the roms directory and that the Plus 1 is enabled in the elk.cfg
configuration file as follows:

  plus1 = 1

By default, parallel output is sent to Elkulator's standard output stream. The
-parallel option should be omitted and Elkulator run with any other desired
options.

To interact with the emulated system over a socket connection, a simple client
program is provided that can be run as follows:

  tools/serial_client.py

This will output the name of a file to be used to connect the client with
Elkulator. For example:

  Waiting for connection at: /tmp/tmpIIrGMc

Alternatively, a filename can be indicated as follows:

  tools/serial_client.py xxx

Elkulator should be run with the -parallel option indicating the appropriate
filename. For example:

  ./elkulator -parallel xxx

If Elkulator cannot connect to the client, it will start up successfully but
report an error. For example:

  Failed to connect to communications socket: No such file or directory

In the emulated Electron, the following commands are useful:

  *FX 5,1  - selects the parallel printer which can then be enabled using
             Ctrl-B or VDU 2, and disabled using Ctrl-C or VDU 3

  *FX 6    - switches off line feed suppression

  *FX 6,10 - switches on line feed suppression

The parallel printer is usually the default selection for printing, and line
feed suppression is usually enabled by default.

Currently, the serial client performs end-of-line character conversion to
simplify printer configuration.

----

Support for parallel communications is found in the following files:

src/main.c     - provides command line options for parallel emulation features
src/mem.c      - tests for access to the parallel port memory address
src/parallel.c - provides the output functionality
