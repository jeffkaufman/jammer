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

## Obsolete

### Reaper Configuration

Here's as much detail as possible on the Reaper configuration this is
intended to drive.  This section will probably get out of date.

#### Installation

* Install Reaper: https://www.reaper.fm/download.php
  * To install the license key, find it in email and copy it to the
    clipboard before opening Reaper.
* Install Native Access
  * Username and password are in password manager
  * Install Vintage Organs for the Hammond
  * Install Scarabee Mark I for the Rhodes
  * Install The Trombone 3.0
    * Needs its own binary files, which are in Google Drive
* Install SWAM Saxophones
  * Binary is in Google Drive
  * License key is in email
* Install Sforzando
  * https://www.plogue.com/downloads.html#sforzando
  * Needs the Fluid_R3 sound font
    * Binary is in Google Drive
    * Originally from
      https://member.keymusician.com/Member/FluidR3_GM/index.html
* Bass Whistle:
  * Install XCode
  * https://github.com/jeffkaufman/iPlug2
  * Install VST3 SDK
    * Visit https://www.steinberg.net/en/company/developers.html
    * Download "VST 3 Audio Plug-Ins SDK"
    * move ~/Downloads/VST_SDK/foo to iPlug2/Dependencies/IPlug2/
    * cd iPlug/Dependencies/
    $ ./copy_vst2_to_vst3_sdk.sh 
  * In XCode, "Open Another Project"
    * iPlug2 > Examples > Bass Whistle > BassWhistle.xcworkspace
  * BassWhistle-macOS
  * Build Target to VST3 > My Mac
  * Product > Build
  * Still not working...
     
* Open backup of Reaper configuration, though it may not import fully

#### Configuration

* Start the jammer router (make run) to make the midi devices
  available
* Enable all the jammer-foo midi inputs in reaper
* Map the inputs for each channel in reaper
* Configure Individual instruments
  * Sax: default settings
  * Bass Sax: default settings
  * Bass Whistle:
    * https://www.jefftk.com/bass-whistle-config-2020-01-25-big.png
  * Drum: Sforzando > Converted > sf2 > FluidR3_GM_sf2 > 128 > 000_Standard
  * Hammond High
    * Add AUi Kontakt > Vintage Organs > Default > Tonewheel Organ B3
    * Organ
      * Mode: upper
      * Percussion: off
    * Amp
      * Rotor > Balance > One notch CCW from top
      * Cabinets
        * Brit 60s
        * Air: one notch CCW from top
      * Reverb > Amount > 0%
    * Tube Amplifier > Drive > half notch CCW from top
  * Organ Low: ReaControlMIDI > General MIDI Bank 01 > Synth Bass 101
    * or Sforzando > Converted > sf2 > FluidR3_GM_sf2 > 008 > 039_Synth_Bass_4
  * Organ Flex: ReaControlMIDI > General MIDI Bank 02 > Doctor Solo
    * or Sforzando > Converted > sf2 > FluidR3_GM_sf2 > 000 > 081_Saw_Wave
  * Sine Pad: ReaControlMIDI > General MIDI Bank 01 > Sine Pad
  * Sweep Pad: ReaControlMIDI > General MIDI > Pad 8 (sweep)
  * Rhodes:
    * Add AUi Kontakt > Scarbee Mark I > Blue Ballad
    * Effect Preset: DI
    * Instrument noise: 0
    * FX Type: Insert
    * Comp: on
      * Threshold: second highest notch
    * Amp: on
      * Bass: centered
  * Overdriven Rhodes
    * Add AUi Kontakt > Scarbee Mark I > Blue Ballad
    * Effect Preset: DI
    * FX Type: Insert
    * Comp: on
      * Threshold: one notch CW from centered
      * Output: one notch CW from centered
    * Dist: on
      * Drive: 1.5 notches CCW from centered
      * Output: One notch CCW from centered
    * Amp: on
  * Trombone
    * Add AUi Kontakt > Trombone > Tenor Trombone 1
  * Bass Trombone
    * Add AUi Kontakt > Trombone > Bass Trombone
  * Jawharp: Sforzando > Converted > sf2 > FluidR3_GM_sf2 > 000 > 004_Rhodes_EP with CC7 vol max


### Dealing with a Cracked Macbook Screen

* Audacity, Export Multiple: shift + command + L

* Reaper, open preferences: command + comma


 
