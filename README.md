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


## Mac Version

There's also a minimal version that runs on a mac and is a MIDI mapper for an
electronic harp mandolin.  Run `make runmac` to build and run.

It will look for two midi devices:

* TE-Control breath controller
* Electronic Harp Mandolin, presenting as "Teensy "

and presents a virtual midi output intended for a wind instrument synth.  I've
been using it with The Trombones 3.0.
