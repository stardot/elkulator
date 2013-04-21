Elkulator v1.0 Linux supplement
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Compiling
~~~~~~~~~

You will need the following libraries :

Allegro 4.x
OpenAL
ALut
Zlib

Open a terminal window, navigate to the Elkulator directory then enter

./configure
make

then ./elkulator to run.

Elkulator has been tested on x86-32 and x86-64 machines. No other architecture is guaranteed
to work, and big-endian machines (eg PowerPC) almost certainly won't work.


Linux specifics
~~~~~~~~~~~~~~~

The menu is not available all the time. Press F11 to open it, then F11 to close again.

The debugger is only available via the command line.

Hardware line doubling mode is not available on Linux.

Fullscreen mode doesn't appear to work correctly, at least on my machine. Elkulator takes over
the screen, but the resolution never changes.

Video performance is noticeably slower than on Windows. This is largely due to the lack
of hardware acceleration support in Elkulator.


Tom Walker
b-em@bbcmicro.com
