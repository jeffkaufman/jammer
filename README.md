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
* **`4`, `6` and `7`** (purple, like Pulse) are the breath sweeps: filters on
  everything fluidsynth plays that follow the breath controller, and leave it
  as it was when you aren't blowing.  `4`, Sweep Bass, takes more and more of
  the bass out the harder you blow (a high-pass from 10Hz up to 1.2kHz), and
  lets it back in as you stop.  `6`, Sweep Treble, does the same from the top
  (a low-pass from 18kHz down to 300Hz).  `7`, Sweep Peak, sweeps a resonant
  peak up from 250Hz to 5kHz, the riser sound, with the rest brought down a
  little so it doesn't overload.  Any of them can be on together.  Not the
  whistle, which isn't fluidsynth's.
* **`` ` ``** (green) is the Breath Gate: a drone chord, like `O`, that sounds
  only while you blow, so pulsing the breath chops it into a rhythm.  It
  opens a little way into the breath and shuts a little below that, so a breath at
  the edge doesn't chatter.  Each breath after the breath has come all the way
  to rest strikes the chord afresh, so it opens on the pad's attack; pulsing
  without coming to rest just chops the chord that's sounding.  It starts on
  Halo Pad, and with it selected `S D F H X C V` pick from the drones'
  pads.  The rest of the voice keys are its own voices, in place of a pad:
  the build-and-drop voices on `A`, `Z` and `G` (see [Builds and
  drops](#builds-and-drops)), and on B, N and M, empty for the other drones,
  percussion played by moving the breath rather than by how hard it is, so
  holding it steady is silence.  Its own voices, drawn in blue, are layers:
  each key switches one on or off, and any of them can play together, over
  the pad or without one.  There's only ever one pad, since it's the one
  voice that uses the Breath Gate's channel; its key again lets go of it,
  leaving the layers on their own.
  * **Guira** (B): a wire brush over a punched metal cylinder, each ridge a
    "tsch".
  * **Guiro** (N): every fortieth of the breath's range it moves is one
    click, so slow is a ratchet and fast a zip.
  * **Washboard** (M): the same over finer ridges, with a thimble on metal.

  The percussion is its own sound, not fluidsynth's, so the sweeps and Kick
  Duck leave it alone.  The pad goes through them like any other.
* **`5`** (purple, like Pulse) is Kick Duck: each kick ducks everything else fluidsynth is
  playing and brings it back up over the beat, for the pump of a sidechained
  mix.  The kick, the foot basses (`W`, `2`, `3`) and the arp (`E`) stay out
  of it, and so does the whistle, which isn't fluidsynth's.  It's set off by
  the kick pedal, whether or not the drum is on here -- so it works with the
  pedals playing a drum synth of their own -- and by the drum's kick when it
  sounds on another pedal's beat.  Skip a kick and that pump is skipped too.
  On the audio, not CC11, so it doesn't fight the fades, the breath or
  Pulse.
* **`8` and `9`** (green) are Pad Bass and Pad Chord: a second drone bass and
  drone chord, over the first pair on `I` and `O`, for layering two pads.
  They start on Warm Pad, where the first pair start on Rock Organ.
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
typing blind.  `F8` is speech recognition instead, `F3` number
recognition -- see below -- and `delete` VOICE LEAD, for the drones.

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
key to B" and "change mode to minor" do what the key picker and the arrow
keys do; only the natural keys, with no sharps or flats.  Without the
lead-in words a name does nothing, so talking to the room is safe.  These
happen the moment they're heard; only numbers wait for the beat.

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
would press the shorter.  Keys and modes don't wait either: the modes'
names are all different from the start, and a key is always one word -- only
the natural keys, no sharps or flats -- so "change key to E" is E at once
rather than maybe the start of "ef".

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
across the room doesn't count.  And a number takes effect on the beat
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
with its "k", is longer than for "two".  What it hears goes in a beat after
you started saying it -- start on the beat and that's the next one -- or the
moment it's heard, if that's later.  The beat is the pedals' while they're
going, and 116 BPM's otherwise.  Apple's recognizer hearing the same number
a moment later is ignored.  Anything it isn't sure of it leaves to
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

The review then goes on to anything especially unusual that nobody has
listened to yet, from the recordings as well as the kept clips: a word that
sounds clearly more like a different word than like any other recording of
its own -- a "four" said to the "five" prompt, a cough learned as "three".
Those are found while you go through the rest, and each plays on its own,
with what it was learned as and what it sounds like.  What you say it was
is learned from then on (for a word in a recording, as a `# review` line in
that recording's `.tsv`); Delete stops it being learned at all.  Either way
it isn't asked about again.  `./numrec-eval --unusual` lists them all.

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

`B`, `N` and `M` do nothing then -- except on the Breath Gate, where they,
and Church Organ's, Synth Brass 2's and Warm Pad's keys, are its own voices.  The list is `DRONE_VOICES` in
`jammermidilib.h`, and `handle_keypad` does the picking, so the Pi's keypad
gets it too.

Every pad on the list is levelled to Rock Organ as the drones used to play
it, A-weighted -- and the chord drones 2dB above that, since they'd been a
little quiet -- with its own channel volume for the bass drones and for the
chord drones.  The drones strike harder than they did (velocity 115 and 40,
up from 70 and 30) so the quieter pads can get there.  `./pads --levels` works those volumes out and `./pads --check`
(part of `make test-mac`) fails if they drift.

**VOICE LEAD** (`delete`) is for all the drones at once, so none of them
jump while the rest glide.  With it on, each new chord moves each voice to the nearest
note of the new chord, in whichever octave that is, and holds the notes the
two chords share rather than striking them again -- the way a pianist moves
between chords, rather than every voice jumping in parallel.  A pull back
towards the drones' usual octave stops a long run of chords from wandering
off, and the same chord again is struck again, as it would be without it.
`voice_lead` in `jammermidilib.h`.

When number recognition (`F3`) hears a new chord, voice-led drones glide
into it: each voice that moves glides from its note to the new chord's over
the half beat before the chord's beat, so it arrives just as the chord is
made (`nashville_leads`).  The common tones stay where they are.  Heard too
late for that, it glides from then to the beat; with no beat to wait for,
it glides over half a beat from when it's heard, and the chord waits for it,
made as the glide ends.  (With VOICE LEAD off, there's nothing to wait for,
and a number with no beat is made the moment it's heard, as ever.)  Once
it's gliding it's the drone's chord: a breath on the Breath Gate, or Pulse,
striking the drones again doesn't pull it back.  For that, on the Mac, each of a
voice-led drone's notes plays on a channel of its own -- three spare channels
per drone, 17 to 31, which follow the drone's program, volume and fade and
are gated and ducked with it -- and glides by bending that channel.  A move
further than an octave, or any chord change that isn't a spoken number, is
struck again rather than glided.  Switching VOICE LEAD on or off strikes the
sounding drones' chords again, onto or off their voice channels, so the very
first change after has voices to glide.

`DOUB` (`P`) and `PRE UNIQ` (`[`), which the drones had no use for, are their
**trance gate**: the pad is chopped on the beat's grid, in 8ths with `P`,
16ths with `[`, and the syncopated 1 . 3 4 with both -- or in jig time
(`0`), where a beat is three 8ths, those three, six 16ths, and 1 . 3 4 . 6.
The grid is the foot bass's, not an even one: the gate opens where the foot
bass plays, the upbeat a hair early, and in jig time the 8ths at 0, 21 and 45
of the beat's 72 subbeats rather than 0, 24 and 48, the lilt of a jig.  Only inside the beat
the last pedal hit started -- once the feet stop, the pad just holds.  It's
on the audio, per channel (`apply_trance_gates` in `macapi.h`), so the Pi
doesn't have it.

`8` and `9` are cleared exactly like `I` and `O`, and `is_drone()` is what
the places that treat the drones specially ask.  Like the extra foot basses
they aren't reachable from `kbd.py`: their pseudo-notes are `w`-`z`.

### Builds and drops

Prototypes for technocontra: ways into and out of a big moment that are all
played live.  None of them keeps going more than a beat past what you're
doing: the breath voices stop within a few milliseconds of the breath, and
the rest only ever fill the beat the last pedal hit started.

**The Breath Gate's voices.**  With `` ` `` selected, blue layers that can be
on together, and over its pad (see [the Breath Gate](#running-on-a-mac)):

| key | voice | |
|---|---|---|
| `A` | Snare Roll | the kit's snare on the beat's grid, faster and louder as you blow harder: quarters, 8ths, 16ths, 32nds.  The first hit comes with the breath. |
| `Z` | Noise Riser | noise through a band that rises from 300Hz to 12kHz as you blow harder |
| `G` | Wobble | a saw bass on the bass note, its filter swinging on the beat's grid once a beat, and 2, 3 and 4 times as you blow harder |

With no pedals the grid is 116 BPM, from the start of the breath; with them
it's theirs, and carries on at their tempo if they stop while you're still
blowing.  The Snare Roll plays the drum channel (`breath_roll_tick`), so
it's ducked and swept with the rest.  The other two are the Mac's own sound
(`macapi.h`), summed in after Kick Duck and the sweeps.  They're levelled to
sit within about 3dB of the foot bass, A-weighted, at a strong breath.

**Trance gate and voice leading** are on the drones: see [The drones'
pads](#the-drones-pads).

**The Vocoder**, on the whistle's `J`, plays the drones' chord with
whatever goes into the whistle's microphone: saws on the root, third (once
the feet or a voice have picked the chord, so it's known) and fifth, with
noise in the top bands for consonants, through 16 bands.  Each note is
played in six octaves at once under a fixed bell over pitch centred on
180Hz, like a Shepard tone: a higher chord leans on its lower octaves, so
changing chord changes the notes but not the register.  A gate that settles
on the room's noise floor -- the quietest the microphone has been in the
last 20 seconds -- keeps the band in the microphone from droning the chord,
without taking a long held note for the room.  The level goes as the square root of the input, so it sits against
the foot bass at about +2 to +12dB across a 10x range of input, and it has its
own volume slider in the Vocal FX menu, apart from the whistle's.
It's a layer rather than a voice: `J` switches it on and off over whichever
whistle voice is playing, and the two sound together -- or, over a breath
voice, the vocoder sounds while the breath voice breathes.  The lit voice key
again, while the vocoder's on, silences the voice and leaves the vocoder
alone; any voice key brings one back, and so does switching the vocoder off.

**Vocal effects** beside the Vocoder are on the row keys next to it,
`J K L ;`, which the whistle has no other use for while it's selected.
Like the Vocoder they're layers over the whistle's voice, and any of them can
be on at once, side by side -- the Vocoder with Voice Bass under it, say:
each key switches its own on and off, each
fades in and out on its own, and the lit voice key again leaves them on their
own.  The Vocal FX menu checks each one that's on, and None switches them all
off.  They're in `voicefx.h`.

| Key | Effect | |
|---|---|---|
| `J` | Vocoder | the chord, played by the voice (above) |
| `K` | Robot | ring modulated with the chord: its root, with its third (once it's known) and fifth beside it, so it changes colour with the chord as well as pitch |
| `L` | Voice Bass | a bass an octave under the voice, following its pitch and nothing else: a sub where the voice sings, with the voice itself shifted down the octave on top |
| `;` | Saw Bass | the same pitch, an octave under, as two detuned saws through a resonant lowpass that opens from 120Hz to 2.5kHz as the voice gets louder: a growling, talking bass |

Voice Bass finds the voice's pitch itself -- YIN, 70-700Hz, every 5ms, on the
input taken down to 12kHz -- since the whistle's detector only covers a
whistle's range.  The sub is a sine with a little of its octave, so it still
carries on a PA that can't reproduce the fundamental, gliding after the voice
over 8ms and following its level.  The voice goes down the octave through a
two-grain shifter whose grains are three of its periods long.  Where there's
no pitch, a consonant or a breath, the sub holds the last for 60ms and then
fades, while the shifted voice carries on.

They share the Vocoder's gate, so the band in the microphone doesn't reach the
PA through them, its volume slider and the whistle's volume keys, and a gain
that meets the microphone halfway, as it does.  Like the whistle, they all
come out of the **left output only**, where fluidsynth's endpoints are unless
CHANNEL SWAP moves them.  `make voicefx-levels && ./voicefx-levels` plays the
kept number clips through each and says how loud each came out against the
Vocoder, A-weighted; they're levelled to within 1dB of it.

**`F2`**, while the whistle's selected, picks which input the Vocoder and the
effects hear: `FX MIC 1`, the whistle's own, or `FX MIC 2`, the audio
device's second input, so a vocal mic there can go through them while the
whistle mic stays on the bass.  The whistle, the breath voices and speech
recognition always listen to input 1.  It's dead on a device with one input.

**The Vocal FX menu** has all of this apart from the whistle: which effect is
on (or None), which input it hears, the effects' volume, and a **gate** of
their own, from -70 to -10dBFS peak, which nothing quieter opens.  That's on
top of the gate that settles on the room: with a band that never stops, the
room is the band, and the room gate never shuts it out.  The whistle row
shows the effects' input as `fx -NNdB` while one is on; set the gate a little
above what it reads between phrases.  It starts at -54dB, where the gate was
fixed before, and is remembered.

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
  on `A S D F G H` and `Z X C V`, or the breath voices on `N` and `M` (see
  [Breathing into the microphone](#breathing-into-the-microphone)); `J K L ;`
  layer the Vocoder or one of the effects beside it over whichever it is (see [Builds
  and drops](#builds-and-drops)); and
  `]`/`\` and `-`/`=` move its octave
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

### Breathing into the microphone

The whistle's `N` and `M` turn the microphone into a **breath
controller**, so everything the breath drives --
Flex, the jawharp, the sweeps on `4 6 7`, the Breath Gate and its builds --
follows what the microphone hears.  Nothing reaches the audience but what the
breath shapes.  The whistle has to be on (`1`) for them to breathe; switching
it off, or to another voice, lets the breath come to rest.  A real breath
controller plugged in alongside still works, the two taking turns.

* **Whistle Breath** (`N`) is how loud you're whistling, counted only while the
  whistle's pitch detector hears a whistle -- a note inside the whistle's range,
  above its gate.  Talking is far below that range, so calling chord numbers
  doesn't breathe.  It has its own **Whistle Breath gate** and **full level**
  in the Whistle menu, apart from the bass's: a breath wants to start on the
  quietest whistle and leave room above it to whistle harder, where the bass
  wants a strict gate and a full level it reaches easily.  They start at 8
  (6x the room) and 5 (0.220).
* **Blow Noise** (`M`) is a layer rather than a voice: `M` switches it on and
  off over whichever whistle voice is playing, so a whistled bass line goes on
  while you blow, and the status row adds `+blow`.  (Over Whistle Breath,
  which is silent, Whistle Breath has the breath controller, there being only
  one.)  It's how hard you're blowing into the microphone, counted
  as far as what comes in is broad noise: its energy spread evenly across six
  octaves from 250Hz to 8kHz.  A whistle or a vowel doesn't count, but an "f",
  "th" or "s", or the breath before a word, does: they're short blows.

Blow Noise answers at once, with nothing waiting to be sure it's a breath,
and has its own **Blow gate** and **Blow full level** in the Whistle menu: the
level where breath starts, 0.0028 to begin with, and the level where it's
full, 0.110.  The status row's level is the microphone's own while it's on:
set the gate a little above what it shows while you're quiet, and the full
level a bit above what it shows when you blow hard.  Breath goes evenly in dB
between the two, and the status row shows the breath being sent.

So calling a chord breathes a little.  `make breathmic-eval &&
./breathmic-eval` runs both over synthetic noise and over the spoken number
clips the speech recognizer has kept, and says how much breath each found.  On
the 151 single numbers so far, at the default knobs: Whistle Breath never
opened the Breath Gate; Blow Noise opened it on nearly all of them, for about
460ms, peaking at 67 of 127 on average.  A higher Blow gate trades sensitivity
for less of that.  Pass it `.wav` or whistle-synth's `.f32`
recordings to try others.  The detectors are in `breathmic.h`.

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
room noise a note has to be -- the same number on any microphone -- from
4.7x at 9 to 37.7x at 0, 2dB a step; five steps stricter than whistle-synth's
own knob, whose 0 is 5 here, since that ran out on a loud stage), the
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
