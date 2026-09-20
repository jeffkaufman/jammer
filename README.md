# The code behind my live stage setup

This isn't really intended for other people to use directly, because it's very
tied to my specific equipment and the kind of music I'm playing.  But it may
still be useful if you want to build something similar.

Run `make run` to build this software and run it.  It will look for various
midi devices:

* MIO USB-MIDI representing a Yamaha DTX 500 used as foot pedals
* TE-Control breath controller
* Piano as "USB MIDI Interface" or any other unknown interface

And send audio to fluidsynth.

## Setup

First set up the Raspberry PI (see below)

Install deps

```
sudo apt install fluidsynth fluid-soundfont-gm alsa-utils jackd2 libasound2-dev
```

(When JACK asks if it can have realtime priority, say yes)

Use a deploy key to check out this repo and put it at `/home/jeffkaufman/jammer/`.

```
$ cd ~/jammer
$ make
```

To run on boot, `/etc/systemd/system/fluidsynth.service` should have:

```
[Unit]
Description=Fluidsynth Synthesizer

[Service]
ExecStart=sudo /home/jeffkaufman/jammer/run-fluidsynth.sh
Restart=always
KillSignal=SIGQUIT
Type=simple
[A
[Install]
WantedBy=multi-user.target
```

And `/etc/systemd/system/jammer.service` should have:

```
[Unit]
Description=Remap MIDI
After=fluidsynth.service

[Service]
ExecStart=/home/jeffkaufman/jammer/jammer
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

And `/etc/systemd/system/jammer-kbd.service` should have:

```
[Unit]
Description=Keyboard Control for Jammer

[Service]
ExecStart=/home/jeffkaufman/jammer/kbd.py
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

Run:

```
sudo systemctl enable fluidsynth
sudo systemctl enable jammer
sudo systemctl enable jammer-kbd
sudo systemctl daemon-reload
```

Set levels for consistency:

```
$ alsamixer
> F6 select "USB Audio Device"
> F5 [All]
> Speaker: 100
```

## Raspberry PI Setup

1. Put the micro SD card into an adapter and attach to laptop

2. Download the imager: https://www.raspberrypi.com/software/

3. Run imager

4. Install Raspberry PI OS Lite. Give it jeffkaufman for the user, ssh
   public key for login, and tell it about the WiFi

5. After booting the image, run `sudo nmap -sn 192.168.0.0/24` to learn what
   IP it came up under.

6. `sudo apt update && sudo apt upgrade`

7. `sudo apt install git`


## Running on a Mac

There's a Mac build with a GUI.  It's one process instead of the Pi's three:
fluidsynth is linked in directly, MIDI comes in over CoreMIDI, and the Mac's
own keyboard replaces `kbd.py`.  All the musical logic (`jammermidilib.h`) is
shared with the Pi build.

The window draws the computer keyboard, with every key labelled both with its
letter and with what it does, lit up to show current state:

* **Number row** (blue) picks which endpoint the modifier keys act on; the
  selected one has a heavy outline.
* **QWERTY row** (green) turns endpoints on and off.
* **Letter keys** (orange) pick the voice for the selected endpoint — or the
  drum sound, when the drum endpoint is selected.
* **Modifier keys** (purple) are the per-endpoint flags: downbeat, upbeat,
  chord, octave, and so on.  They light up for whichever endpoint is selected,
  so switching endpoints switches what's lit.
* **Function row and arrows** (teal) are whole-rig settings and the musical
  mode.

`F8` and `delete` arm a three-digit entry for the root note and for a manual
volume, same as on the Pi; the key stays lit and the status line shows the
digits as you type them.  You can also click keys with the mouse.

### Building

```
$ brew install fluid-synth
$ make soundfont    # pulls FluidR3_GM.sf2 out of the Debian package (~148MB)
$ make run-mac
```

`make app` instead builds a self-contained `Jammer.app` that carries the
soundfont and its own copies of libfluidsynth and everything under it, so it
runs on a Mac without homebrew.

`make test-mac` checks the on-screen keyboard against `handle_keypad`: that
every key is bound to something real, that no two keys collide or overlap, and
that the lit state follows the configuration.

### Audio output

The Audio Output menu lists the CoreAudio devices and remembers the choice
across launches; `$JAMMER_AUDIO_DEVICE` overrides it (exact name or any
substring, so "Scarlett" finds "Scarlett 2i2 USB").  Without a choice it
follows the system default, which on a laptop is the built-in speakers.

Don't set `audio.periods` or `audio.period-size` here the way
`run-fluidsynth.sh` does for ALSA.  On CoreAudio those make the driver open
successfully and then never request a sample -- no error, no sound -- and
which values break is device-dependent.  `$JAMMER_PERIODS` and
`$JAMMER_PERIOD_SIZE` exist for experimenting; jammer checks that audio is
actually flowing after opening a device and falls back to fluidsynth's own
buffering if it isn't.

### Notes

* Keys only register while the jammer window is frontmost.  That's deliberate —
  capturing them system-wide would need Accessibility permission.
* The F-keys work as plain F1-F12 with a single press, no `fn`.  Jammer flips
  the system "use F1, F2 etc. as standard function keys" setting while its
  window is frontmost and puts it straight back when you switch away or quit,
  so brightness and volume keep working everywhere else.  This needs no
  special permission, but it is a system-wide setting with no per-device form,
  so while jammer is frontmost it applies to the built-in keyboard too.
  It restores to whatever the System Settings checkbox says rather than to a
  value remembered at startup, so if jammer is ever killed with `kill -9` --
  the one signal it can't catch -- the next run puts things right when you
  quit it.
* MIDI devices are matched by name the same way as on the Pi (`mio`/`DTX` for
  the foot pedals, `Breath Controller`, and `Piano`/`Roland`/`USB MIDI
  Interface` for the keyboard, falling back to any unrecognized device).
  Plugging something in mid-session re-scans.

## Mac Version: harp mandolin MIDI mapper

Separately from the above, there's a minimal version that runs on a mac and is
a MIDI mapper for an electronic harp mandolin.  Run `make runmac` to build and run.

It will look for two midi devices:

* TE-Control breath controller
* Electronic Harp Mandolin, presenting as "Teensy "

and presents a virtual midi output intended for a wind instrument synth.  I've
been using it with The Trombones 3.0.
