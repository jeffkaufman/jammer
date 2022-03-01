# The code behind my live stage setup

This isn't really intended for other people to use directly, because it's very
tied to my specific equipment and the kind of music I'm playing.  But it may
still be useful if you want to build something similar.

Run `make run` to build this software and run it.  It will look for various
midi devices:

* MIO USB-MIDI representing a Yamaha DTX 500 used as foot pedals
* TE-Control breath controller
* Piano as "USB MIDI Interface"

And send audio to fluidsynth.

## Setup

First set up the Raspberry PI (see below)

Install deps

```
sudo apt install fluidsynth fluid-soundfont-gm alsa-utils jackd2 libasound2-dev
```

(When JACK asks if it can have realtime priority, say yes)

Check out this repo and put it at `/home/pi/jammer/`.

```
$ cd ~/jammer
$ make
```

To run on boot, `/etc/systemd/system/fluidsynth.service` should have:

```
[Unit]
Description=Fluidsynth Synthesizer

[Service]
ExecStart=sudo /home/pi/jammer/run-fluidsynth.sh
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

And `/etc/systemd/system/jammer.service` should have:

```
[Unit]
Description=Remap MIDI
After=fluidsynth.service

[Service]
ExecStart=/home/pi/jammer/jammer
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

If this box will only ever run jammer then:

```
sudo systemctl enable fluidsynth
sudo systemctl enable jammer
sudo systemctl daemon-reload
```

Otherwise, when setting up whistle-synth it will start them dynamically based
on whether we're in whistle mode or jammer mode.

Regardless, continue with setting up at least the keyboard listener portion of
https://github.com/jeffkaufman/whistle-synth

## Raspberry PI Setup

1. Put the micro SD card into an adapter and attach to laptop

2. Download the imager: https://www.raspberrypi.com/software/

3. Run imager

4. Install Raspberry PI OS Lite

5. Connect a keyboard and monitor and log in: pi/raspberry

6. Change the password to something more secure

7. Use the automated setup tool:

   i. Set the country to US

   ii. Configure Wi-Fi

   iii. Enable SSH

8. `sudo apt install emacs mosh git`

### Fluidsynth from source

```
$ sudo emacs /etc/apt/sources.list
  -> uncomment deb-src line
$ sudo apt-get update
$ sudo apt-get build-dep fluidsynth --no-install-recommends
$ git clone git@github.com:FluidSynth/fluidsynth.git
$ cd fluidsynth
$ mkdir build
$ cd build
$ cmake ..
$ make
```