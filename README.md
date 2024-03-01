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

Run:

```
sudo systemctl enable fluidsynth
sudo systemctl enable jammer
sudo systemctl daemon-reload
```

Set levels for consistency:

```
$ alsamixer
> F6 select "USB Audio Device"
> F5 [All]
> Speaker: 83
```


Continue with setting up at least the keyboard listener portion of
https://github.com/jeffkaufman/whistle-synth

## Raspberry PI Setup

1. Put the micro SD card into an adapter and attach to laptop

2. Download the imager: https://www.raspberrypi.com/software/

3. Run imager

4. Install Raspberry PI OS Lite

5. Connect a keyboard and monitor and log in.  It will ask you to create a
   password: use 'jeffkaufman' with something secure.

6. Use the automated setup tool: `sudo raspi-config`

   i. Set the country to US

   ii. Configure Wi-Fi

   iii. Enable SSH

7. Log back in over ssh.

8. `sudo apt update && sudo apt upgrade`

9. `sudo apt install emacs mosh git`

