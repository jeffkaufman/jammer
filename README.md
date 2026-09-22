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

The window draws the computer keyboard, lit up to show current state.  Each
key carries its letter in the corner, the abbreviation from the paper tab on
the physical keyboard in the middle, and what it actually does underneath:

* **`2` and `3`** (green) are Bounce Bass and Skip Bass: two more foot
  basses, on top of the one on `W`.  Same bass line, different rhythmic
  treatment, so they can run together -- see below.
* **`8` and `9`** (green) are Pad Bass and Pad Chord: a second drone bass and
  drone chord, over the first pair on `I` and `O`, for layering two pads.
* **QWERTY row** (green) turns endpoints on and off.  Hold shift to pick which
  endpoint the modifier keys act on instead of toggling it; the selected one
  gets a yellow outline whether or not it's switched on.
* **Letter keys** (orange) pick the voice for the selected endpoint — or the
  drum sound, when the drum endpoint is selected, or a pad, when a drone is.
  See below.
* **Modifier keys** (purple) are the per-endpoint flags: downbeat, upbeat,
  chord, octave, and so on.  They light up for whichever endpoint is selected,
  so switching endpoints switches what's lit.
* **Function row and arrows** (teal) are whole-rig settings and the musical
  mode.
* **`1`** (pink) is the whistle bass, which is its own synthesis engine rather
  than a fluidsynth channel -- see below.

Click the key signature at the top left to play in another key.  You can click
keyboard keys with the mouse too, shift-clicking to select.

Unlike the Pi, there's no three-digit entry on `F8` or `delete`: the root note
comes from that picker, and manual per-voice volumes aren't something worth
typing blind.  `F8` is speech recognition instead, and `F3` number
recognition -- see below.

### Speech recognition

There are two keys, which can be on in any combination: `F3`, number
recognition, is for choosing the chord by calling numbers, and `F8`, speech
recognition, is for spoken commands.  It's the same recognizers listening
either way -- the keys only say which of what they hear gets acted on -- so
with just `F8` on, a number said to the room does nothing.  Both listen on the
whistle's microphone, which has to be set up, though the whistle bass doesn't
have to be on.

`F3` is Drum Some (`F5`) with your voice choosing the chord instead of pedals
1, 3 and 4, which go back to only keeping time.  So it's one of the three ways
of choosing the chord, with `F5` and `F9`: switching one on switches the
others off.  `F8` stays as it was.

Say a Nashville number, one to seven, and the chord goes to that degree of
the major key on the root -- 1 I, 2 ii, 3 iii, 4 IV, 5 V, 6 vi, 7 vii° --
whatever the arrow keys say.  Only the words and digits count, not "to" or
"for", so talking to the room doesn't change chords.  It's Apple's on-device speech recognition (`speech.h`); the
first time it starts, macOS asks permission.  So that a bare `jammer-mac`
can ask, `Info.plist` is linked into it, which also means running it from a
terminal now shares `Jammer.app`'s saved settings.

With `F8` on, any button can be pressed by saying "press" and then its name:
"press foot bass", "press drum some", "press octave up".  "select" instead of
"press" is shift-click, for the buttons where that means something.  "change
key to B flat" and "change mode to minor" do what the key picker and the
arrow keys do.  Without the lead-in words a name does nothing, so talking to
the room is safe.

A button's name is what's written on it right now, so the voice keys answer to
the drum kits, the drones' pads or the whistle's voices when those are showing
("press warm pad" with a drone selected).  Labels that are abbreviations,
symbols, or cut short to fit also answer to spelled-out names -- "volume up",
"octave down", "clear endpoint", "electric piano", "speech recognition" (F8),
"number recognition" (F3),
"drum chooses notes" (F9), "frequency modulator" -- listed in
`SPOKEN_ALIASES` in `keypad.h`.  Spaces and number words don't matter: "room
two" is Room 2.

No button's name is the start of another's, in any selection state, and a
test holds that: a name that's the start of a longer one has to wait to see
if it's going to grow, and saying the longer one with a pause in the middle
would press the shorter.  Keys and modes do still have that shape -- "change
key to B" might be heading for "B flat" -- so those wait for 0.7s of quiet.

The recognizer is given a dictionary of every phrase it should expect, so
that "press foot bass" beats "press foot base" and "arpeggiator" beats "or
educator".  `speechphrases` prints the phrases from the jammer's own tables,
so renaming a button renames it for the recognizer too; `speechmodel.swift`
(Swift, since that's the only way Apple offers) turns them into training data,
`speech-model.bin`, with pronunciations for the unusual words from
`SW_PRONUNCIATIONS` in `speechwords.h`; and the app compiles it into a custom
language model at startup, cached under `~/Library/Caches/com.jefftk.jammer`.
`make run-mac` and `make app` build it.  The same phrases go to the
recognizer as hint words too.  On top of that, once "press" or "change key to"
has been said, sound-alikes count -- "base" for "bass", "for" for 4 -- though
never for a bare number.  The speech row shows the words it's hearing, and flags
it while the dictionary is still loading or couldn't be loaded.

Speaking is played, not just said.  Only what's loud enough to be said right
into the microphone reaches the recognizer -- the gate, set from the Speech
Recognition menu and shown as a white tick on the speech row's meter, which
brightens while it's open.  It starts strict, at -20dBFS peak, so a caller
across the room doesn't count.  And whatever's heard takes effect on the beat
two beats after you stop talking: say "four" ending on the one and the IV
comes in on the three, however long the recognizer took.  On the beat itself
-- each pedal hit is a beat, and the change is made inside the nearest one,
just before its notes, since arriving a few milliseconds after it would only
be heard on the next note.  Heard too late for its beat -- recognition
sometimes takes longer than two beats -- it goes on the next beat instead: a
beat late, but still on a beat.  With the pedals stopped there's no beat to
land on, so it's made the moment it's heard.  Talk
again before then and it waits for you to finish.  The speech row shows a
change that's waiting with "…", and jammer's output logs the timing of each.

Numbers also have a faster way in than Apple's recognizer: `numrec.h`, which
knows only one to seven, in your voice, and hears them 40-120ms after the
word ends -- as soon as it can be sure the word is over, which for "six",
with its "k", is longer than for "two".  What it hears goes in on the very
next beat rather than two beats on, and Apple's recognizer hearing the same
number a moment later is ignored.  Anything it isn't sure of it leaves to
Apple.  It only takes a whole word that follows a moment of quiet, so "press
room two" doesn't count.

It learns your voice from recordings: Speech Recognition > Record Number
Samples... shows one word at a time in a big window for about four minutes
-- the numbers, then words it should learn to ignore, then talking, then
quiet -- and saves the raw microphone and the prompts' timings to
`~/Library/Application Support/com.jefftk.jammer/numbers/` (`numtrain.h`).
Every recording there is learned at startup and after each new one; more
sessions, and ones made with the band playing, make it better.  `make
numrec-eval && ./numrec-eval` says how well it does on them.  Jammer's output
says how many numbers it has learned.

It also learns from playing.  Whenever Apple's recognizer and the fast one
disagree about whether something you said was a number, the audio is kept,
labeled with what Apple heard, in `numbers/heard/` (`numheard.h`), and
learned from at every startup along with the recordings.  Apple is usually
right but not always; each clip's `.tsv` says what both recognizers made of
it.  The ones where the fast one took a number and Apple didn't agree aren't
learned until you've said who was right -- so far it's been Apple that was
wrong -- with Speech Recognition > Review Number Clips..., which plays each
and asks what you said.  The kept clips are held to
2GB, oldest going first.

From `F3`, `F5` hands the choice back to the feet, and `F9` or `esc` end it;
`esc` leaves `F8` alone, since commands aren't musical state.
The words are `speechwords.h` and the chords `nashville_picks_chord`.

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
that the lit state follows the configuration.  For the whistle it also checks
that selecting it actually redirects the shared keys -- and, just as much,
that leaving it puts them back.

`make whistlelevels && ./whistlelevels` measures whether the whistle can
reach the endpoints it plays beside, A-weighted, the way `kitlevels` does for
the drum kits.  See below: the answer turns on the microphone, not on any
volume control.

### The extra foot basses

`2` and `3` are the foot bass again with a fixed set of flags already applied,
so that two or three of them can run at once and land on different beats
rather than being retyped between tunes:

| | `2` | `3` |
|---|---|---|
| downbeat | on | **off** |
| upbeat / up high | on | on |
| shortish (`S`) | — | **on** |
| shorter (`SS`) | **on** | **on** |
| doubled (`II`) | **on** | **on** |
| voice | SynBass 2 (39) | **SynBass 1 (38)** |

Bold is where they differ from the foot bass on `W`.  Both short flags at once
on `3` is a real setting rather than a redundant one: `maybe_end_notes` halves
the threshold for one and quarters it for the other, so the pair is an eighth.

They are ordinary endpoints in every other way -- toggled, shift-selected,
and modified like any other -- and they take channels 10 and 11.  That pushed
`CHANNEL_PITCHED_KICK` up to 15, since it was sitting on 10.  `is_footbass()`
is what the places that treat the foot bass specially now ask, rather than
naming the one endpoint: the octave arithmetic, the note-ending rule, the
arpeggiation and the volume trim all apply to the three of them.

On the Pi they aren't reachable yet: their pseudo-notes are `s`-`v`, because
`2` and `3` have meant "select the foot bass" and "select the arp" since
`kbd.py`, and there's no key spare on that number row.  The Mac's `2` and `3`
carry the new ones instead.

### The drones' pads

With any of the four drones selected (`I`, `O`, `8`, `9`) the voice keys pick
from their own list instead of the usual voices: Rock Organ, which the drones
have always been, and the pads picked out with `make pads && ./pads`.

| | | | | | |
|---|---|---|---|---|---|
| `A` Church Organ | `S` Synth Strings 1 | `D` Synth Voice | `F` Synth Brass 1 | `G` Synth Brass 2 | `H` Rock Organ |
| `Z` Warm Pad | `X` Polysynth | `C` Halo Pad | `V` Sweep Pad | | |

`B`, `N` and `M` do nothing then.  The list is `DRONE_VOICES` in
`jammermidilib.h`, and `handle_keypad` does the picking, so the Pi's keypad
gets it too.

Every pad on the list is levelled to Rock Organ as the drones used to play
it, A-weighted -- and the chord drones 2dB above that, since they'd been a
little quiet -- with its own channel volume for the bass drones and for the
chord drones.  The drones strike harder than they did (velocity 115 and 40,
up from 70 and 30) so the quieter pads can get there.  `./pads --levels` works those volumes out and `./pads --check`
(part of `make test-mac`) fails if they drift.

`8` and `9` are cleared exactly like `I` and `O`, and `is_drone()` is what
the places that treat the drones specially ask.  Like the extra foot basses
they aren't reachable from `kbd.py`: their pseudo-notes are `w`-`z`.

### The whistle bass

`1` is a whistle-controlled bass synth: whistle into the microphone and it
plays the note you whistled, an octave or three down, through one of ten
voices.  The engine is [whistle-synth](../whistle-synth)'s -- `pitch.c`,
`synth.c` and `engine.c` compiled unchanged from that repo, which makes jammer
the third front end onto them after its own command-line build and its Mac
app.  `make` expects it at `../whistle-synth`; set `WHISTLE_DIR` if it's
somewhere else.  None of this exists on the Pi, which runs whistle-synth as a
service of its own.

It's an instrument on the keyboard but **not an endpoint**: endpoints are
fluidsynth MIDI channels and this one makes its own sound, so `jammermidilib.h`
doesn't know about it and neither does the Pi.

* `1` switches it on and off.  Shift-`1` selects it, the same way shift over
  an endpoint's key selects that endpoint.
* **While it's selected** the voice keys pick its ten voices -- Bass,
  Octaveless, Reese, 808, FM, Sub FM, Square, Drawbar, High Drawbar, Accordion
  on `A S D F G H` and `Z X C V` -- and `]`/`\` and `-`/`=` move its octave
  and its volume.  The per-endpoint flags go dark, because the endpoint they'd
  act on isn't what's on screen.  Shift over any endpoint's key hands the keys
  back.
* The voices are looked up **by name** in whistle-synth's presets table at
  startup, not by index: presets there have come and gone, and an index that
  shifted would put a different instrument under every key.

The status row under the audio row shows whether it's running, what it's
listening to, the level the detector is hearing while you play, and the note
it's currently finding.  A dot fills while the gate is open, so you can see it
trigger without listening for it.

### The Whistle menu

**Which input.**  Left to itself it takes the Scarlett, not the system
default, for the same reason `run-fluidsynth.sh` names the USB interface on
the way out: the system default on a laptop is the built-in microphone, which
is a foot from the speakers and pointed at them.  Matched the same way
`resolve_audio_device` matches an output -- an exact name or any substring, so
"Scarlett" finds "Scarlett 2i2 USB" -- and `$JAMMER_WHISTLE_INPUT` names a
different one.  Picking a device from the menu pins it for good; **Automatic**
at the top of the menu goes back to taking whatever the rig has today, which
is also what happens if the device you pinned isn't plugged in.

Everything else here describes the microphone and the room rather than the
tune, so none of it is on a key: the gate (how many times the
room noise a note has to be -- the same number on any microphone), the
full-blow level (set it a bit above the level the status row shows while you
whistle hard), and the range of notes to believe.  Plus "Raw input", which
passes the microphone straight through for checking that it's live at all.

The whistle's volume is **separate from the global one**, deliberately: it's a
second synthesis engine, and the balance between it and fluidsynth is
something you set once against a rig and then leave alone while the global
knob moves everything.  `-`/`=` are the per-instrument trim on top of it, the
same way the endpoint volume keys sit on top of the global slider.

That trim starts at the **top** of the engine's 0-9 knob rather than the
middle, which is the opposite of what whistle-synth's own app does.  There the
knob is the master and you turn it up; here the whistle has to sit against
fluidsynth, and that app's default of 5 is `0.198` -- 14dB down before it
reaches the mix, which is audibly buried under the rest of the rig.  There's
nothing above the top to default to either: the engine clips at ±1 *after* its
own volume, so step 9 is its full scale, and the only way to be louder than
that is the slider, which would be clipping to do it.

#### Getting the level right

The control that sets the whistle's level is **not** its volume knob -- it's
the full-blow level in the Whistle menu.  The voice spends the player's breath
on loudness and brightness, so a full-blow level set above what the microphone
actually delivers leaves it permanently dark and quiet however far up the
volume goes.  `./whistlelevels` measures it, A-weighted, against the foot bass
as `jml_setup` leaves it:

| input peak | step 0 | step 2 | step 5 | step 9 |
|---|---|---|---|---|
| 0.300 | +1.9 | +1.8 | +1.4 | −9.4 |
| 0.100 | +1.8 | +0.9 | −7.1 | −18.5 |
| 0.030 | −3.0 | −8.5 | −17.0 | −28.0 |
| 0.010 | −11.8 | −17.5 | −25.8 | −36.1 |

dB relative to the foot bass; 0 is matched.  Two things fall out of it:

* **The engine has plenty of level** -- it beats the foot bass by about 2dB --
  but only if the input reaches it.  Every 10dB of input lost costs very
  nearly 10dB of output, and no volume control gets it back.
* **The input level is the thing to fix first.**  The status row shows what
  the detector is hearing while you play, peak-held for a second and a half so
  you can whistle at it and then go and type the number in.  A strong whistle
  into a working input reads somewhere around 0.03-0.3.  If it reads 0.001,
  that's a microphone or an input-gain problem and nothing in here will
  rescue it.

The default full-blow step is **2**, not whistle-synth's own 5: that app's 5
is 0.22, a vocal mic at the lip, and against fluidsynth it costs 7dB even with
a good microphone.  The bottom of the knob isn't free either -- full-blow is
what the voice's dynamics are measured against, so setting it under what you
actually deliver leaves you permanently maxed out with nothing left to play
with.  It's a per-rig number: set it just above what the status row shows
while you whistle hard.

#### How it gets out

There's no second audio device and no second output stream.  fluidsynth
already hands jammer a render callback; the whistle is summed into those same
buffers after fluidsynth has filled them, so it comes out of whatever the
Audio Output menu picked.  The microphone is one AUHAL input unit of its own,
reaching the output thread through a lock-free ring -- which is only tolerable
because of how the engine is built: the synth free-runs and never reads the
input, so a resynced sample perturbs the pitch detector for one analysis
window and never reaches the output directly.

#### Latency

The whistle adds, on top of whatever fluidsynth's output is already doing, the
microphone's own block plus what the ring holds between the two threads.  The
status row shows it as `+N.Nms`, and the console prints the breakdown at
startup.

Three things set it, and the third was by far the biggest:

* **The microphone's block size**, which the device picks unless asked.  512
  frames on the Mac's built-in microphone, which is 10.7ms before the detector
  has even seen the sound.  Jammer asks for 64.
* **The ring** between the microphone's thread and fluidsynth's, which has to
  hold at least one output block (the consumer takes a whole one at once) plus
  one input block.
* **fluidsynth's output block**, which is what makes the ring big.  Its
  CoreAudio driver never asks for a block size, so its client inherits the
  device's -- and a 512-frame default costs 10.7ms on the way out *and* forces
  the ring to hold another 10.7ms on the way in.  So jammer sets the output
  device to 64 frames before fluidsynth opens it.

Setting `audio.period-size` instead does not work: the driver opens happily
and then never asks for a sample.  That was already written down here, and it
was re-measured rather than taken on trust -- it is still true.  Going in
through the device is the way that works.

Measured on a Scarlett 2i2 at 48kHz, whistle-added latency:

| | input block | ring | added |
|---|---|---|---|
| before | 512 | 1024 | 32.0ms |
| microphone asked for 64 | 64 | 576 | 13.3ms |
| output device asked for 64 too | 64 | 128 | **4.0ms** |

Plus 4.0ms of detection lag, which is the analysis window rather than
buffering -- the synth free-runs, so that is how late it hears about a pitch
change, not something held up on the way through.  It moves with the note
range: reaching further down costs a longer window.

Because a small block is exactly the setting `macapi.h` warns can leave a
device open but silent, jammer checks that audio is actually flowing after the
synth starts, and puts the device's block size back and reopens if it isn't.
`$JAMMER_OUTPUT_BUFFER` overrides the 64, and `0` leaves the device alone.
Both devices are restored on the way out.

**The microphone picks the sample rate.**  The detector works out how fast the
signal is wiggling in samples, so it has to agree with the rest of the rig:
hand it 48kHz audio while it believes it's at 44.1kHz and every note comes out
a semitone and a half sharp, and resampling would be latency on the one path
where latency is the whole point.  So jammer reads the microphone's rate at
startup and runs fluidsynth at that -- which means the ordinary case changes
no device settings at all.  Switching to a microphone that runs at a different
rate asks that device to change; if it won't, the whistle doesn't start and
the status row says which rate it wanted.  (Note that this is a change for the
rest of the rig too: fluidsynth used to always run at its own default of
44100.)

### Audio output

The Audio Output menu lists the CoreAudio devices and carries a global volume
slider on top of the per-voice levels in `voices.h`.  Both are remembered
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
