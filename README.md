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

Install deps

```
sudo apt install fluidsynth fluid-soundfont-gm alsa-utils jackd2
```

(When JACK asks if it can have realtime priority, say yes)

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

Then `sudo systemctl enable fluidsynth`,
`sudo systemctl enable jammer` and
`sudo systemctl daemon-reload`.

Then set up at least the keyboard listener portion of
https://github.com/jeffkaufman/whistle-synth
