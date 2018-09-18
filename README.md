# The code behind my live stage setup

This isn't really intended for other people to use directly, because it's very
tied to my specific equipment and the kind of music I'm playing.  But it may
still be useful if you want to build something similar.

Run `make run` to build this software and run it.  It will look for various
midi devices:

* AXIS 49 Keyboard
* MIO USB-MIDI representing a Yamaha DTX 500 used as foot pedals
* TE-Control breath controller
* Tilt sensor: https://github.com/jeffkaufman/yoctomidi
* Game controller: https://github.com/jeffkaufman/gcmidi (unused)

and present several virtual midi devices named "jammer-_foo_".
