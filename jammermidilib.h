#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2

//    A    39 Synth Bass 2
//    S    38 Synth Bass 1
//    D    84 Lead 3 (calliope)
//    F    35 Electric Bass (finger)
//    G    26 Acoustic Guitar (nylon)
//    H    28 Electric Guitar (jazz)
//    J    75 Pan Flute
//    K    80 Lead 1 (square)
//    Z     4 Electric Piano 1
//    X    24 Acoustic Guitar (nylon)
//    C    85 Lead 6 (voice)
//    V    64 Soprano Sax
//    B    66 Tenor Sax
//    N    67 Baritone Sax
//    M    81 Lead 2 (sawtooth)

// For every instrument there's a default volume modifier, and also for every
// voice.  Then each combination also has a dynamic modifier, adjusted by +/-.

#define MIDI_DRUM_IN_SNARE 46
#define MIDI_DRUM_IN_SNARE_2 100
#define MIDI_DRUM_IN_KICK 38
#define MIDI_DRUM_IN_KICK_2 42
#define MIDI_DRUM_IN_KICK_3 85
#define MIDI_DRUM_IN_CRASH 59
#define MIDI_DRUM_IN_HIHAT 51

#define MIDI_DRUM_OUT_KICK_1 35
#define MIDI_DRUM_OUT_KICK_2 36
#define MIDI_DRUM_OUT_RIM 37
#define MIDI_DRUM_OUT_SNARE 38
#define MIDI_DRUM_OUT_CLAP 39
#define MIDI_DRUM_OUT_ESNARE 40
#define MIDI_DRUM_OUT_CLOSED_HIHAT 42
#define MIDI_DRUM_OUT_PEDAL_HIHAT 44
#define MIDI_DRUM_OUT_OPEN_HIHAT 46
#define MIDI_DRUM_OUT_RIDE 51
#define MIDI_DRUM_OUT_TAMBOURINE 54
#define MIDI_DRUM_OUT_HI_BONGO 60
#define MIDI_DRUM_OUT_LOW_BONGO 61
#define MIDI_DRUM_OUT_CABASA 69
#define MIDI_DRUM_OUT_MARACAS 70
#define MIDI_DRUM_OUT_CLAVES 75
#define MIDI_DRUM_OUT_HI_WOOD 76
#define MIDI_DRUM_OUT_LOW_WOOD 77

#define MIDI_MAX 127

#define TICK_MS 1  // try to tick every N milliseconds

#define KICK_TIMES_LENGTH 12
#define SNARE_TIMES_LENGTH 12
#define CRASH_TIMES_LENGTH 12
#define HIHAT_TIMES_LENGTH 12

#define NS_PER_SEC 1000000000LL

#define N_NOTE_DIVISIONS 72
#define N_SUBBEATS (N_NOTE_DIVISIONS)

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)                    \
  (byte & 0x80 ? '1' : '0'),                    \
    (byte & 0x40 ? '1' : '0'),                  \
    (byte & 0x20 ? '1' : '0'),                  \
    (byte & 0x10 ? '1' : '0'),                  \
    (byte & 0x08 ? '1' : '0'),                  \
    (byte & 0x04 ? '1' : '0'),                  \
    (byte & 0x02 ? '1' : '0'),                  \
    (byte & 0x01 ? '1' : '0')

// kbd.py sends these as 'a' (97) + N
#define F1 (98)
#define F2 (99)
#define F3 (100)
#define F4 (101)
#define F5 (102)
#define F6 (103)
#define F7 (104)
#define F8 (105)
#define F9 (106)
#define F10 (107)
#define DELETE (108)
#define ESCAPE (109)
#define UP (110)
#define LEFT (111)
#define DOWN (112)
#define RIGHT (113)
#define TAB (114)
// Not a key kbd.py has: the Mac's F8 sends this to switch speech to
// choosing the notes.  F8 itself still arms root-note entry on the Pi.  127
// because the values past TAB are the lowercase letters, which are taken.
#define SPEECH_PICKS (127)

#define MODE_MAJOR 1
#define MODE_MIXO 2
#define MODE_MINOR 3
#define MODE_BETH_COHENS 4

#define MIDI_PEDAL_1 MIDI_DRUM_IN_SNARE
#define MIDI_PEDAL_2 MIDI_DRUM_IN_KICK
#define MIDI_PEDAL_3 MIDI_DRUM_IN_CRASH
#define MIDI_PEDAL_4 MIDI_DRUM_IN_HIHAT

// Virtual pedals made by chording
#define MIDI_PEDAL_12 5  // 1 and 2
#define MIDI_PEDAL_13 6  // 1 and 3
#define MIDI_PEDAL_23 7  // 2 and 3
#define MIDI_PEDAL_24 8  // 2 and 4
#define MIDI_PEDAL_34 9  // 3 and 4
#define MIDI_PEDAL_41 10 // 4 and 1
// others are possible, but not implemented yet

#define MIDI_DRUM_CHORD_INTERVAL_MAX_NS 40000000

// Not every kit here is on a key.  The keyboard bindings are in
// handle_keypad(); the ones left unbound are kits that were measured and
// tuned but aren't wanted on the keyboard right now, and binding one again
// is a single line.
//
// With the drum endpoint selected the voice keys pick a kit instead: the
// percussion set the drum channel plays out of, and the sounds it uses from
// it -- the kick on the downbeat (DB), the snare-ish hit that goes along with
// it when SHORTISH is on, and the hihat-ish sound on the upbeats (UB).
//
// The velocity scales are what keep the kits at one level.  The sets are not
// mixed to match each other at all, so without them switching kits would be
// a jump in volume rather than a change of sound.
//
// kitlevels.c produces these: it renders each sound, measures it A-weighted
// against the Standard set, and searches for the velocity that matches.
// Don't hand-edit them without re-running it -- and in particular don't
// reason about them from peak levels, which is how they were first set and
// which was wrong by up to 20dB.  A kick's energy sits where the ear is
// least sensitive, so peak and loudness disagree wildly between a 53Hz kick
// and a 9kHz hat.  These are still a starting point rather than the last
// word: what matters is how they sit in a room.
//
// The kicks are all 6dB up on where the Standard set's kick used to sit, which
// is what the room wanted.  That spends nearly all the Standard kick's
// velocity headroom: at the nominal 90 it plays at 127, so with the drum
// following the foot (VEL) a harder stomp can't make it any louder.
#define KIT_RIM    0
#define KIT_RIM2   1
#define KIT_SNARE  2
#define KIT_CLAP   3
#define KIT_ESNARE 4
#define KIT_RIDE   5
#define KIT_BONGO  6
#define KIT_BLOCK  7
// Auditioned kits, picked by ear out of the sets the jammer never used to
// reach for.  See audition.c.
#define KIT_808_A  8
#define KIT_808_B  9
#define KIT_ROOM2  10
#define KIT_ROOM6  11
#define KIT_SYNTH  12
#define N_KITS     13

// The percussion sets, bank 128.  Everything used to come out of Standard,
// because the drum channel was never sent a program change at all.
#define PERC_STANDARD 0
#define PERC_ROOM_2   10
#define PERC_ROOM_6   14
#define PERC_808      25

// Melodic programs, bank 0, for a pitched kick.
#define PROG_SYNTH_DRUM 118

#define NO_PITCHED_KICK -1

typedef struct {
  int program;       // percussion set this kit's sounds come from
  int kick;          // a note in that set, or a pitch when kick_program is set
  int snare;
  int hihat;
  float kick_vel;
  float snare_vel;
  float hihat_vel;
  // A kick from a melodic program instead of a percussion sample: it can be
  // pitched, but it sustains, so it needs its own channel and a note-off.
  int kick_program;
  int kick_gate_ms;
} DrumKit;

static const DrumKit KITS[N_KITS] = {
  // set            kick                  snare                 hihat
  [KIT_RIM] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_RIM,
               MIDI_DRUM_OUT_CLOSED_HIHAT, 1.41, 0.65, 1.0, NO_PITCHED_KICK, 0},
  [KIT_RIM2] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_1, MIDI_DRUM_OUT_RIM,
                MIDI_DRUM_OUT_CLOSED_HIHAT, 1.41, 0.8, 1.0, NO_PITCHED_KICK, 0},
  [KIT_SNARE] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 1.41, 0.8, 1.0, NO_PITCHED_KICK, 0},
  [KIT_CLAP] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_CLAP,
                MIDI_DRUM_OUT_CLOSED_HIHAT, 1.41, 0.8, 1.0, NO_PITCHED_KICK, 0},
  [KIT_ESNARE] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_ESNARE,
                  MIDI_DRUM_OUT_CLOSED_HIHAT, 1.41, 0.8, 1.0, NO_PITCHED_KICK, 0},
  [KIT_RIDE] = {PERC_STANDARD, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_SNARE,
                MIDI_DRUM_OUT_RIDE, 1.41, 0.8, 0.61, NO_PITCHED_KICK, 0},
  [KIT_BONGO] = {PERC_STANDARD, MIDI_DRUM_OUT_LOW_BONGO,
                 MIDI_DRUM_OUT_HI_BONGO, MIDI_DRUM_OUT_CABASA,
                 0.61, 0.94, 0.69, NO_PITCHED_KICK, 0},
  // The claves need nearly full velocity to keep up, so this kit has little
  // headroom left when the foot hits hard.
  [KIT_BLOCK] = {PERC_STANDARD, MIDI_DRUM_OUT_LOW_WOOD, MIDI_DRUM_OUT_HI_WOOD,
                 MIDI_DRUM_OUT_CLAVES, 0.83, 1.11, 1.39, NO_PITCHED_KICK, 0},

  // The 808's two kicks, each with its own set's snare and hat.  The two
  // kicks measure 8dB apart despite near-identical peaks -- 35 is the short
  // one, 36 the long boom -- so they need quite different scales.
  [KIT_808_A] = {PERC_808, MIDI_DRUM_OUT_KICK_1, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 1.34, 0.46, 0.60,
                 NO_PITCHED_KICK, 0},
  [KIT_808_B] = {PERC_808, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 0.99, 0.46, 0.60,
                 NO_PITCHED_KICK, 0},
  [KIT_ROOM2] = {PERC_ROOM_2, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 1.38, 0.54, 0.84,
                 NO_PITCHED_KICK, 0},
  [KIT_ROOM6] = {PERC_ROOM_6, MIDI_DRUM_OUT_KICK_2, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 1.23, 0.84, 0.84,
                 NO_PITCHED_KICK, 0},

  // Synth Drum played at C0, with the 808's snare and hat around it -- the
  // hat can't come from Synth Drum, since note 42 of a melodic program is
  // just a higher note.
  [KIT_SYNTH] = {PERC_808, 12 /* C0 */, MIDI_DRUM_OUT_SNARE,
                 MIDI_DRUM_OUT_CLOSED_HIHAT, 0.41, 0.46, 0.72,
                 PROG_SYNTH_DRUM, 120},
};

// What the voice keys pick while a drone is selected: Rock Organ, which is
// what the drones have always been, and the pads picked out with pads.c.
// Rock Organ stays on H, where it is for every other endpoint; the rest go
// in the order they were auditioned.  Labels are for the Mac's on-screen
// keyboard, where "\n" splits lines.
//
// Each carries its own channel volume on the drones, one for the bass drones
// and one for the chord drones, since a single low note and a chord two
// octaves up at a different velocity come out nowhere near the same.  These
// replace the per-voice volumes in voices.h while it's a drone playing.
//
// `pads --levels` produces the volumes: every pad as loud, A-weighted, as
// Rock Organ was on that drone before any of this -- channel volume 65 and
// the old velocities of 70 and 30 -- and the chord drones 2dB louder than
// that, since by ear they'd been a little quiet.  Re-run it rather than
// hand-editing, and `pads --check` (part of make test-mac) says if they've
// drifted.
static const struct {
  char note;
  int program;
  const char* label;
  int bass_volume;   // CC7 on Db and Db2
  int chord_volume;  // CC7 on Dc and Dc2
} DRONE_VOICES[] = {
  {'A', 19, "Church\nOrgan",    35,  58},
  {'S', 50, "Synth\nStrings",   83,  82},
  {'D', 54, "Synth\nVoice",     96, 100},
  {'F', 62, "Synth\nBrass 1",   61,  81},
  {'G', 63, "Synth\nBrass 2",   61,  86},
  {'H', 18, "Rock\nOrgan",      40,  55},
  {'Z', 89, "Warm\nPad",        95,  95},
  {'X', 90, "Poly\nsynth",      82,  83},
  {'C', 94, "Halo\nPad",       104,  95},
  {'V', 95, "Sweep\nPad",       83,  71},
};
#define N_DRONE_VOICES ((int)(sizeof(DRONE_VOICES) / sizeof(DRONE_VOICES[0])))

// The DRONE_VOICES entry on this key, or -1.
static int drone_voice_for_note(int note) {
  for (int i = 0; i < N_DRONE_VOICES; i++) {
    if (DRONE_VOICES[i].note == note) return i;
  }
  return -1;
}

// How hard the drones strike their notes: a single bass note, or a chord.
// These were 70 and 30, which left some pads short of Rock Organ's old level
// even at full channel volume -- Halo Pad by 5dB on the bass.  At these,
// every pad in DRONE_VOICES gets there with at least 3dB of channel volume
// to spare, and the volumes there make up the difference.
#define DRONE_BASS_VELOCITY 115
#define DRONE_CHORD_VELOCITY 40

// Which of the pair of volumes a drone takes.  By endpoint rather than by its
// chord flag, so it doesn't change under you when the flag does.
static inline bool is_chord_drone(int endpoint) {
  return endpoint == ENDPOINT_DRONE_CHORD ||
         endpoint == ENDPOINT_DRONE_CHORD_2;
}

// The drone's own channel volume for this voice, or -1 if it isn't one of
// DRONE_VOICES.
static int drone_volume(int endpoint, int voice) {
  for (int i = 0; i < N_DRONE_VOICES; i++) {
    if (DRONE_VOICES[i].program != voice) continue;
    return is_chord_drone(endpoint) ? DRONE_VOICES[i].chord_volume
                                    : DRONE_VOICES[i].bass_volume;
  }
  return -1;
}

// The keys that pick a voice for the selected endpoint.
static bool is_voice_key(int note) {
  switch (note) {
  case 'A': case 'S': case 'D': case 'F': case 'G': case 'H':
  case 'Z': case 'X': case 'C': case 'V': case 'B': case 'N': case 'M':
    return true;
  }
  return false;
}

#define CHORD_MAJOR 0
#define CHORD_MINOR 1
#define CHORD_DIM   2
#define CHORD_NULL  3

#define MAX_FADE MIDI_MAX

int normalize(int val) {
  if (val > MIDI_MAX) {
    return MIDI_MAX;
  }
  if (val < 0) {
    return 0;
  }
  return val;
}

struct Configuration {
  /* Anything mentioned here should be initialized in clear_configuration */
  int selected_endpoint;
  bool flex_min;

  // Pretend voices, by choosing which notes
  int drum_voice;

  bool on[N_ENDPOINTS];
  bool downbeat[N_ENDPOINTS];
  bool upbeat[N_ENDPOINTS];
  bool upbeat_high[N_ENDPOINTS];
  bool doubled[N_ENDPOINTS];
  int current_note[N_ENDPOINTS];
  int current_fifth[N_ENDPOINTS];
  int current_len[N_ENDPOINTS];
  uint64_t last_arpeggiation[N_ENDPOINTS];
  bool shortish[N_ENDPOINTS];
  bool shorter[N_ENDPOINTS];
  bool pre_unique[N_ENDPOINTS];
  bool chord[N_ENDPOINTS];
  bool vel[N_ENDPOINTS];

  int volume_deltas[N_ENDPOINTS];
  int manual_volumes[128*9];
  int voices[N_ENDPOINTS];
  bool pans[N_ENDPOINTS];

  int octave_deltas[N_ENDPOINTS];

  bool air_lockeds[N_ENDPOINTS];
  double locked_airs[N_ENDPOINTS];
  bool follows_air[N_ENDPOINTS];
  bool ducked[N_ENDPOINTS];
};

// TODO: allow multiple of these.
struct Configuration global_config;

// TODO: pass this around
struct Configuration* c = &global_config;

/* Anything mentioned here should be initialized in voices_reset */

bool piano_notes[MIDI_MAX];
int root_note;
int last_update_bass_note;
int fifth_note;
uint64_t kick_times[KICK_TIMES_LENGTH];
int kick_times_index;
uint64_t snare_times[SNARE_TIMES_LENGTH];
int snare_times_index;
uint64_t crash_times[CRASH_TIMES_LENGTH];
int crash_times_index;
uint64_t hihat_times[HIHAT_TIMES_LENGTH];
int hihat_times_index;
uint64_t next_ns[N_SUBBEATS];
uint64_t current_beat_ns;
uint64_t last_downbeat_ns;
uint64_t next_duck_trough_ns;
uint64_t next_duck_peak_ns;
uint64_t next_downbeat_ns;
int last_fb_vel;
bool jig_time;
bool allow_all_drums_downbeat;
bool drum_chooses_notes;
bool drum_chooses_some_notes;
// Drum Some with speech choosing instead of the feet: a spoken Nashville
// number picks the chord (nashville_picks_chord, from speech.h), and pedals
// 1, 3 and 4 go back to only keeping time.  drum_chooses_some_notes stays on
// underneath, since everything downstream of the choice is Drum Some's.
// Speech is Mac-only, so on the Pi nothing ever sets this.
bool speech_chooses_notes;
int musical_mode;
int most_recent_drum_pedal;
uint64_t most_recent_choosy_drum_ts;

// these six are only used when drum_chooses_notes or
// drum_chooses_some_notes
int chord_type;
int chord_note;
int current_drum_pedal_note;
int last_drum_pedal_note;
int prev_chord_type;
int prev_chord_note;

int fade_value;
int fade_target;

int to_root(int note_out) {
  // 24-35
  return note_out % 12 + 24;
}

// The note a pedal (or pair of pedals) picks under the current mode, and the
// kind of chord that goes with it.  No side effects, so the whistle can ask
// what each pedal would give without pressing any of them.
int pedal_note(int pedal, int* chord_type_out) {
  int note = root_note;

  int selected_chord_type = CHORD_MAJOR;

  if (musical_mode == MODE_MINOR && !drum_chooses_some_notes) {
    // Minor is just major where the iv is the i.
    note = to_root(note - 9);
  }

  if (pedal == MIDI_PEDAL_1) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR) {
	note += 9;  // vi
	selected_chord_type = CHORD_MINOR;
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	note += 7;  // V
      }
    } else {
      if (musical_mode == MODE_MIXO) {
	note += 10; // bVII
      } else {
	note += 9;  // vi
	selected_chord_type = CHORD_MINOR;
      }
    }
  } else if (pedal == MIDI_PEDAL_12) {
    note += 11;  // VII
    selected_chord_type = CHORD_NULL;
  } else if (pedal == MIDI_PEDAL_13) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR) {
	note += 4;  // iii
	selected_chord_type = CHORD_MINOR;
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	note += 5;  // IV
      }
    } else {
      // This one is weird: which note it is depends on what the
      // previous note was.
      if (last_drum_pedal_note == to_root(note + 2) || // ii
	  last_drum_pedal_note == to_root(note + 4)) { // iii
	note += 3;  // biii
      } else if (last_drum_pedal_note == to_root(note)     ||   // I
		 last_drum_pedal_note == to_root(note + 9) ||   // vi
		 last_drum_pedal_note == to_root(note + 11) ) { // VII
	note+= 10; // bVII
      }
      selected_chord_type = CHORD_NULL;
    }
  } else if (pedal == MIDI_PEDAL_2) {
    // pass
  } else if (pedal == MIDI_PEDAL_23) {
    note += 2;  // ii
    selected_chord_type = CHORD_MINOR;
  } else if (pedal == MIDI_PEDAL_24) {
    note += 6;  // bV
    selected_chord_type = CHORD_NULL;
  } else if (pedal == MIDI_PEDAL_3) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR || musical_mode == MODE_MIXO ||
	  musical_mode == MODE_BETH_COHENS) {
	// pass  I
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	if (musical_mode == MODE_MINOR) {
	  selected_chord_type = CHORD_MINOR;  // i
	} else {
	  // I
	}
      }
    } else {
      note += 5;  // IV
    }
  } else if (pedal == MIDI_PEDAL_34) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR) {
	note += 7;  // V
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	note -= 4;  // bVI
      } else if (musical_mode == MODE_BETH_COHENS) {
	note -= 2;  // bVII
      }
    } else {
      note += 4;  // iii
      selected_chord_type = CHORD_MINOR;
    }
  } else if (pedal == MIDI_PEDAL_4) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR) {
	note += 5;  // IV
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	note -= 2;  // bVII
      } else if (musical_mode == MODE_BETH_COHENS) {
	note += 1;  // bII
      }
    } else {
      note += 7;  // V
    }
  } else if (pedal == MIDI_PEDAL_41) {
    if (drum_chooses_some_notes) {
      if (musical_mode == MODE_MAJOR) {
	note += 2;  // ii
	selected_chord_type = CHORD_MINOR;
      } else if (musical_mode == MODE_MINOR || musical_mode == MODE_MIXO) {
	note += 4;  // III
      }
    } else {
      note += 8;  // bVI
      selected_chord_type = CHORD_NULL;
    }
  }

  *chord_type_out = selected_chord_type;
  return to_root(note);
}

void update_drum_pedal_note() {
  bool is_composite_note =
    most_recent_drum_pedal == MIDI_PEDAL_12 ||
    most_recent_drum_pedal == MIDI_PEDAL_13 ||
    most_recent_drum_pedal == MIDI_PEDAL_23 ||
    most_recent_drum_pedal == MIDI_PEDAL_24 ||
    most_recent_drum_pedal == MIDI_PEDAL_34 ||
    most_recent_drum_pedal == MIDI_PEDAL_41;
  if (is_composite_note) {
    // don't update last_drum_pedal_note because current_drum_pedal_note
    // contains not a real note.
  } else {
    last_drum_pedal_note = current_drum_pedal_note;
  }

  int selected_chord_type;
  int note = pedal_note(most_recent_drum_pedal, &selected_chord_type);

  if (selected_chord_type == CHORD_NULL) {
    // Don't use note for chord_note.

    if (is_composite_note) {
      // Composite (two pedal) note: roll back chord that was just started.
      chord_note = prev_chord_note;
      chord_type = prev_chord_type;
    }
  } else {
    prev_chord_note = chord_note;
    prev_chord_type = chord_type;
    chord_type = selected_chord_type;
    chord_note = note;
  }

  current_drum_pedal_note = note;
}

void update_bass(bool force_refresh);

// The foot basses: the original and the two that were added beside it.  They
// play the same bass line and are handled identically everywhere -- the
// octave arithmetic, the note-ending rule, the arpeggiation -- and differ
// only in the flags they are cleared to.  A named test rather than three
// comparisons at each site, because missing one of them is a bug you hear
// rather than see.
static inline bool is_footbass(int endpoint) {
  return endpoint == ENDPOINT_FOOTBASS ||
         endpoint == ENDPOINT_FOOTBASS_2 ||
         endpoint == ENDPOINT_FOOTBASS_3;
}

// The drones: Db and Dc, and the second pair beside them.  Same reasoning as
// is_footbass -- a named test, so the second pair can't be missed anywhere
// the first is handled.
static inline bool is_drone(int endpoint) {
  return endpoint == ENDPOINT_DRONE_BASS ||
         endpoint == ENDPOINT_DRONE_CHORD ||
         endpoint == ENDPOINT_DRONE_BASS_2 ||
         endpoint == ENDPOINT_DRONE_CHORD_2;
}

// The endpoints update_bass holds a note on, rather than ones that play on
// the beat: the jawharp and the drones.
static inline bool holds_bass_note(int endpoint) {
  return endpoint == ENDPOINT_JAWHARP || is_drone(endpoint);
}

// Given a note relative to root, convert it into a note relative to fifth.
int to_fifth(int note_out) {
  return fifth_note + (note_out - root_note);
}

void psend_midi(int action, int note, int velocity, int endpoint) {
  if (endpoint != ENDPOINT_DRUM && (action == MIDI_ON || action == MIDI_OFF)) {
    if (c->voices[endpoint] == 16 ||
	c->voices[endpoint] == 18) {
      note += 12;  // organs should be up an octave
    }
    if (c->chord[endpoint]) {
      // chords should be higher
      note += 24;
    }

    // Normally this is like:
    //
    //     24 25 26 27 28 29 30 31 32 33 34 35 ->
    //      36 37 38 39 40 41 42 43 44 45 46 47
    //
    // but bass is different.  That goes:
    //
    //     24 25 26 27 28 29 30 31 32 33 34 35 ->
    //      36 37 38 39 40 41 30 31 32 33 34 35
    //       36 37 38 39 40 41 42 43 44 45 46 47
    //

    if (is_footbass(endpoint) ||
        endpoint == ENDPOINT_JAWHARP) {
      note += (c->octave_deltas[endpoint] / 2) * 12;
      if (c->octave_deltas[endpoint] % 2 == 1) {
        if (to_root(note) < 30) {
          note += 12;
        }
      }
    } else {
      note += c->octave_deltas[endpoint]*12;
    }
  }
  send_midi(action, note, velocity, endpoint);
}

// The note a pitched kick is holding, and when to let go of it.  A percussion
// sample rings out on its own; a melodic program would sustain forever.
int pitched_kick_note = -1;
uint64_t pitched_kick_off_at = 0;

void end_pitched_kick() {
  if (pitched_kick_note == -1) return;
  send_midi(MIDI_OFF, pitched_kick_note, 0, CHANNEL_PITCHED_KICK);
  pitched_kick_note = -1;
}

void endpoint_notes_off(int endpoint) {
  // send an explicit all notes off command
  psend_midi(MIDI_CC, 123, 0, endpoint);

  // The pitched kick sounds on its own channel, but it belongs to the drum
  // endpoint: switching the drum off should stop it too.
  if (endpoint == ENDPOINT_DRUM) {
    end_pitched_kick();
  }
}

void all_notes_off() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    endpoint_notes_off(endpoint);
  }
  // Not an endpoint, but it can be holding a note.  send_midi() rather than
  // psend_midi(), which would index the per-endpoint arrays with it.
  end_pitched_kick();
  send_midi(MIDI_CC, 123, 0, CHANNEL_PITCHED_KICK);
}

void reload_voice_setting(struct Configuration* c) {
  int endpoint = c->selected_endpoint;
  int voice = c->voices[endpoint];
  int volume_delta = c->volume_deltas[endpoint];
  int manual_volume = c->manual_volumes[voice];
  bool pan = c->pans[endpoint];

  if (endpoint != ENDPOINT_JAWHARP && c->ducked[endpoint]) {
    volume_delta += 20;
  }
  select_endpoint_voice(endpoint,
                        voice % 128, voice / 128,
                        volume_delta, manual_volume, pan);
}

// Point the drum channel at this kit's percussion set, and set up the
// pitched-kick channel if it has one.  Has to run whenever the kit changes,
// and at startup, because until the jammer started doing this the drum
// channel just sat on whatever program it powered up with.
void select_drum_kit(int kit) {
  c->drum_voice = kit;
  const DrumKit* k = &KITS[kit];

  choose_voice(CHANNEL_DRUM, PERCUSSION_BANK, k->program);

  end_pitched_kick();
  if (k->kick_program == NO_PITCHED_KICK) return;

  choose_voice(CHANNEL_PITCHED_KICK, 0, k->kick_program);
  send_midi(MIDI_CC, CC_07, MIDI_MAX, CHANNEL_PITCHED_KICK);
  send_midi(MIDI_CC, CC_11, fade_value, CHANNEL_PITCHED_KICK);
  send_midi(MIDI_CC, CC_PAN, 0, CHANNEL_PITCHED_KICK);
}

// A pitched kick sustains; a percussion sample doesn't.  Called every tick.
void maybe_end_pitched_kick() {
  if (pitched_kick_note == -1) return;
  if (now() >= pitched_kick_off_at) end_pitched_kick();
}

void drone_endpoint_off(int endpoint);

void select_voice(struct Configuration* c, int voice) {
  // A held note has to be struck again to sound in the new voice, and a
  // drone only strikes a note it doesn't think is already sounding -- so it
  // has to forget this one, or changing a pad's voice silences it.
  if (holds_bass_note(c->selected_endpoint)) {
    drone_endpoint_off(c->selected_endpoint);
  }
  endpoint_notes_off(c->selected_endpoint);
  c->voices[c->selected_endpoint] = voice;
  reload_voice_setting(c);
  if (is_footbass(c->selected_endpoint) ||
      c->selected_endpoint == ENDPOINT_ARP ||
      holds_bass_note(c->selected_endpoint)) {
    update_bass(/*force_refresh=*/true);
  }
}

void clear_jawharp() {
  select_voice(c, 67);
}

void clear_drone_bass() {
  select_voice(c, 18);
  c->shorter[c->selected_endpoint] = true;
}

void clear_drone_chord() {
  select_voice(c, 18);
  c->chord[c->selected_endpoint] = true;
  c->shorter[c->selected_endpoint] = true;
}

void clear_footbass() {
  select_voice(c, 39);
  c->downbeat[ENDPOINT_FOOTBASS] = true;
  c->upbeat[ENDPOINT_FOOTBASS] = true;
  c->upbeat_high[ENDPOINT_FOOTBASS] = true;
  c->doubled[ENDPOINT_FOOTBASS] = false;
}

// The foot bass with a shorter note and the doubling on: what you got by
// pressing FB, then SS, then II.
void clear_footbass_2() {
  select_voice(c, 39);
  c->downbeat[ENDPOINT_FOOTBASS_2] = true;
  c->upbeat[ENDPOINT_FOOTBASS_2] = true;
  c->upbeat_high[ENDPOINT_FOOTBASS_2] = true;
  c->shorter[ENDPOINT_FOOTBASS_2] = true;
  c->doubled[ENDPOINT_FOOTBASS_2] = true;
}

// And with both short flags, the doubling, no downbeat, and SynBass 1 rather
// than SynBass 2: FB, S, the S key's voice, SS, DB off, II.  Both short flags
// together is a real setting rather than a redundant one -- maybe_end_notes
// halves for one and quarters for the other, so the pair is an eighth.
void clear_footbass_3() {
  select_voice(c, 38);
  c->downbeat[ENDPOINT_FOOTBASS_3] = false;
  c->upbeat[ENDPOINT_FOOTBASS_3] = true;
  c->upbeat_high[ENDPOINT_FOOTBASS_3] = true;
  c->shortish[ENDPOINT_FOOTBASS_3] = true;
  c->shorter[ENDPOINT_FOOTBASS_3] = true;
  c->doubled[ENDPOINT_FOOTBASS_3] = true;
}

void clear_drum() {
  // select_voice ??
  c->downbeat[ENDPOINT_DRUM] = false;
  c->upbeat[ENDPOINT_DRUM] = true;
  c->upbeat_high[ENDPOINT_DRUM] = false;
  c->doubled[ENDPOINT_DRUM] = false;
}

void clear_arp() {
  select_voice(c, 38);

  c->downbeat[ENDPOINT_ARP] = true;
  c->upbeat[ENDPOINT_ARP] = true;
  c->upbeat_high[ENDPOINT_ARP] = true;
  c->doubled[ENDPOINT_ARP] = true;
}

void clear_flex() {
  select_voice(c, 81);

  c->flex_min = false;
}

void clear_low() {
  select_voice(c, 39);
}

void clear_high() {
  select_voice(c, 16);
}

void clear_overlay() {
  select_voice(c, 18);
}

void update_fade(int endpoint) {
  // Flex's expression is its breath, sent from flex_val(), which has the fade
  // folded in.  Setting it to the bare fade here would fight that.
  if (endpoint == ENDPOINT_FLEX) return;
  psend_midi(MIDI_CC, CC_11, fade_value, endpoint);
}

void update_fades() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    update_fade(endpoint);
  }
  send_midi(MIDI_CC, CC_11, fade_value, CHANNEL_PITCHED_KICK);
}

void progress_fades() {
  if (fade_target == fade_value) return;
  fade_value += (fade_target < fade_value ? -1 : 1);
  update_fades();
}

void clear_endpoint() {
  c->on[c->selected_endpoint] = false;
  c->downbeat[c->selected_endpoint] = true;
  c->upbeat[c->selected_endpoint] = false;
  c->upbeat_high[c->selected_endpoint] = false;
  c->doubled[c->selected_endpoint] = false;
  c->current_note[c->selected_endpoint] = -1;
  c->current_fifth[c->selected_endpoint] = -1;
  c->current_len[c->selected_endpoint] = 0;
  c->last_arpeggiation[c->selected_endpoint] = 0;
  c->shortish[c->selected_endpoint] = false;
  c->shorter[c->selected_endpoint] = false;
  c->pre_unique[c->selected_endpoint] = false;
  c->chord[c->selected_endpoint] = false;
  c->vel[c->selected_endpoint] = false;
  c->pans[c->selected_endpoint] = false;
  c->octave_deltas[c->selected_endpoint] = 0;
  c->air_lockeds[c->selected_endpoint] = false;
  c->locked_airs[c->selected_endpoint] = 0;
  c->follows_air[c->selected_endpoint] = false;
  c->ducked[c->selected_endpoint] = false;

  switch (c->selected_endpoint) {
  case ENDPOINT_JAWHARP: clear_jawharp(); break;
  case ENDPOINT_FOOTBASS: clear_footbass(); break;
  case ENDPOINT_FOOTBASS_2: clear_footbass_2(); break;
  case ENDPOINT_FOOTBASS_3: clear_footbass_3(); break;
  case ENDPOINT_DRUM: clear_drum(); break;
  case ENDPOINT_ARP: clear_arp(); break;
  case ENDPOINT_FLEX: clear_flex(); break;
  case ENDPOINT_LOW: clear_low(); break;
  case ENDPOINT_HI: clear_high(); break;
  case ENDPOINT_OVERLAY: clear_overlay(); break;
  case ENDPOINT_DRONE_BASS: clear_drone_bass(); break;
  case ENDPOINT_DRONE_CHORD: clear_drone_chord(); break;
  case ENDPOINT_DRONE_BASS_2: clear_drone_bass(); break;
  case ENDPOINT_DRONE_CHORD_2: clear_drone_chord(); break;
  }

  update_fade(c->selected_endpoint);
}

void clear_configuration() {
  for (int i = 0; i < N_ENDPOINTS; i++) {
    c->volume_deltas[i] = 0;
  }
  for (int i = 0; i < MIDI_MAX; i++) {
    c->manual_volumes[i] = -1;
  }
  for (int i = 0 ; i < N_ENDPOINTS; i++) {
    c->selected_endpoint = i;
    clear_endpoint();
  }
  // The loop above leaves the selection on whatever it cleared last, so say
  // what we actually want to start on.
  c->selected_endpoint = ENDPOINT_FOOTBASS;
  c->drum_voice = KIT_RIM;
}

void clear_status() {
  for (int i = 0; i < MIDI_MAX; i++) {
    piano_notes[i] = false;
  }
  root_note = to_root(26);  // D @ 37Hz
  fifth_note = to_root(root_note + 7);
  last_update_bass_note = 0;

  for (int i = 0; i < KICK_TIMES_LENGTH; i++) {
    kick_times[i] = 0;
  }
  kick_times_index = 0;

  for (int i = 0; i < SNARE_TIMES_LENGTH; i++) {
    snare_times[i] = 0;
  }
  snare_times_index = 0;

  for (int i = 0; i < CRASH_TIMES_LENGTH; i++) {
    crash_times[i] = 0;
  }
  crash_times_index = 0;

  for (int i = 0; i < HIHAT_TIMES_LENGTH; i++) {
    hihat_times[i] = 0;
  }
  hihat_times_index = 0;

  for (int i = 0; i < N_SUBBEATS; i++) {
    next_ns[i] = 0;
  }

  current_beat_ns = 0;
  last_downbeat_ns = 0;
  next_duck_trough_ns = 0;
  next_duck_peak_ns = 0;

  jig_time = false;
  allow_all_drums_downbeat = false;

  drum_chooses_notes = false;
  drum_chooses_some_notes = false;
  speech_chooses_notes = false;
  musical_mode = MODE_MAJOR;
  most_recent_drum_pedal = MIDI_PEDAL_2;
  most_recent_choosy_drum_ts = 0;
  chord_type = CHORD_MAJOR;
  chord_note = root_note;
  prev_chord_type = chord_type;
  prev_chord_note = chord_note;
  current_drum_pedal_note = root_note;
  last_drum_pedal_note = root_note;

  fade_value = MAX_FADE;
  fade_target = MAX_FADE;
}

void voices_reset() {
  clear_configuration();
  clear_status();

  // clear_configuration() picked a kit, but only in memory; the drum channel
  // needs telling which percussion set that is.
  select_drum_kit(c->drum_voice);

  // clear_configuration() broadcast CC11 to every endpoint using the old
  // fade_value, which on the very first run is 0 -- expression 0 is silence.
  // clear_status() then resets fade_value in memory but has nothing to send
  // it with, and progress_fades() won't, because target and value now agree.
  // So push the restored fade out explicitly.  Same story after a fade-out
  // followed by a reset.
  update_fades();
}


//  The flex organ follows flex_breath and flex_base.
//  flex_breath follows breath.
int flex_base = 0;
int flex_breath = 0;
int last_flex_val = 1;
// Flex's expression: the breath, and the fade on top of it.  Flex uses
// expression for its breath, which is the channel the fade works through
// everywhere else, so here the two are multiplied rather than one of them
// overwriting the other -- otherwise the next breath undid the fade out.
int flex_val() {
  int val = flex_breath;
  if (c->flex_min) {
    val += 60;
  }
  return val * fade_value / MAX_FADE;
}

// Only some endpoints use this, and some only use it some of the time:
//  * Always in use for jawharp
int current_note[N_ENDPOINTS];

int piano_left_hand_velocity = 100;  // most recent piano bass midi velocity

int roll = MIDI_MAX / 2;
int pitch = MIDI_MAX / 2;

int breath = 0;  // current value from breath controller
double leakage = 0;  // set by calculate_breath_speeds()
double breath_gain = 0;  // set by calculate_breath_speeds()
double max_air = 0; // set by calculate_breath_speeds()
double air = 0;  // maintained by update_air()

char active_note() {
  if (drum_chooses_notes || drum_chooses_some_notes) {
    return current_drum_pedal_note;
  }
  return root_note;
}

char active_chord() {
  if (drum_chooses_notes || drum_chooses_some_notes) {
    return chord_note;
  }
  return root_note;
}

float random_float() {
  return (float)rand()/(float)(RAND_MAX);
}

float subbeat_location() {
  if (last_downbeat_ns > 0 && current_beat_ns > 0) {
    return 1.0 * (now() - last_downbeat_ns) / current_beat_ns;
  }
  return -1;
}

void drone_endpoint_off(int endpoint) {
  if (current_note[endpoint] != -1) {
    endpoint_notes_off(endpoint);
    current_note[endpoint] = -1;
  }
}

bool downbeat(int subbeat) {
  return subbeat % 72 == 0;
}
bool preup(int subbeat) {
  return subbeat == (jig_time ? (72/3-3) : (72/4));
}
bool upbeat(int subbeat) {
  return subbeat == (jig_time ? (2*72/3-3) : (72/2-1));
}
bool predown(int subbeat) {
  if (jig_time) return false;  // This beat doesn't happen in jig time.
  return subbeat == (3*72/4);
}

void select_note(int subbeat, bool chord, bool send_downbeat,
                 bool send_upbeat, bool upbeat_high, bool pre_unique,
                 bool doubled, int* selected_note, bool* send_note) {
  if (chord) {
    *selected_note += 12;
  }

  if (downbeat(subbeat)) {
    *send_note = send_downbeat;
  } else if (upbeat(subbeat)) {
    *send_note = send_upbeat;
    if (upbeat_high && pre_unique) {
      *selected_note += 24;
    } else if (upbeat_high || pre_unique) {
      *selected_note += 12;
    }
  } else if (preup(subbeat) && !jig_time) {
    *send_note = send_downbeat && doubled;
    if (pre_unique) {
      if (upbeat_high) {
        *selected_note += 12;
      } else {
        *selected_note += 7;
      }
    }
  } else if (predown(subbeat) || preup(subbeat)) {
    *send_note = send_upbeat && doubled;

    if (upbeat_high && pre_unique) {
      *selected_note += 36;
    } else if (pre_unique) {
      *selected_note += 12 + 7;
    } else if (upbeat_high) {
      *selected_note += 12;
    }
  }
}

bool should_end_note(int current_len, bool is_shortish, bool is_shorter) {
  if (current_len != -1 && (is_shortish || is_shorter)) {
    int threshold = jig_time ? 24 : 18;  // kept if shortish only
    if (is_shortish && is_shorter) {
      threshold /= 3;
    } else if (is_shorter) {
      threshold /= 2;
    }
    if (current_len >= threshold) {
      return true;
    }
  }
  return false;
}

void arpeggiate_endpoint(int endpoint, int subbeat, uint64_t current_time, bool drone) {
  if (!c->on[endpoint]) return;
  if (drone && c->current_note[endpoint] == -1) return;

  if (c->current_note[endpoint] != -1) {
    c->current_len[endpoint]++;
  }

  int note_out = active_note();
  int selected_note = note_out;
  int fifth = to_fifth(selected_note);
  bool send_note = false;

  select_note(subbeat, c->chord[endpoint], c->downbeat[endpoint],
              c->upbeat[endpoint], c->upbeat_high[endpoint], c->pre_unique[endpoint],
              c->doubled[endpoint], &selected_note, &send_note);
  select_note(subbeat, c->chord[endpoint], c->downbeat[endpoint],
              c->upbeat[endpoint], c->upbeat_high[endpoint], c->pre_unique[endpoint],
              c->doubled[endpoint], &fifth, &send_note);

  bool end_note = send_note ||
    (!(is_footbass(endpoint) && drum_chooses_notes) &&
     should_end_note(c->current_len[endpoint], c->shortish[endpoint],
                     c->shorter[endpoint]));

  if (end_note && c->current_note[endpoint] != -1) {
    psend_midi(MIDI_OFF, c->current_note[endpoint], 0, endpoint);
    if (c->current_fifth[endpoint] != -1) {
      psend_midi(MIDI_OFF, c->current_fifth[endpoint], 0, endpoint);
    }

    c->current_note[endpoint] = -1;
    c->current_fifth[endpoint] = -1;
    c->current_len[endpoint] = -1;
  }

  if (send_note) {
    if (selected_note != -1) {
      int vel = c->vel[endpoint] ? last_fb_vel : 90;
      if (selected_note > note_out &&
          c->voices[endpoint] == 32) {
        vel -= 20;
      }


      c->current_note[endpoint] = selected_note;
      c->current_fifth[endpoint] = fifth;
      c->current_len[endpoint] = 0;

      psend_midi(MIDI_ON,
                 c->current_note[endpoint],
                 vel,
                 endpoint);

      if (c->chord[endpoint]) {
        psend_midi(MIDI_ON,
                   c->current_fifth[endpoint],
                   vel,
                   endpoint);
      }

      c->last_arpeggiation[endpoint] = current_time;
    }
  }
}

void arpeggiate_drum(int subbeat, uint64_t current_time) {
  if (!c->on[ENDPOINT_DRUM]) return;

  int vel = c->vel[ENDPOINT_DRUM] ? last_fb_vel : 90;

  const DrumKit* kit = &KITS[c->drum_voice];

  if (downbeat(subbeat) && c->downbeat[ENDPOINT_DRUM]) {
    if (kit->kick_program == NO_PITCHED_KICK) {
      psend_midi(MIDI_ON,
                 kit->kick,
                 vel * kit->kick_vel,
                 ENDPOINT_DRUM);
    } else {
      // Retrigger cleanly if the last one is somehow still held.
      end_pitched_kick();
      send_midi(MIDI_ON,
                kit->kick,
                vel * kit->kick_vel,
                CHANNEL_PITCHED_KICK);
      pitched_kick_note = kit->kick;
      pitched_kick_off_at = current_time + kit->kick_gate_ms * 1000000LL;
    }

    float snare_min = 65.0;
    float snare_max = 110.0;
    if (c->shortish[ENDPOINT_DRUM] && last_fb_vel > snare_min) {
      float snare_vel = vel * kit->snare_vel;
      if (last_fb_vel < snare_max) {
        snare_vel = ((last_fb_vel - snare_min) /
                     (snare_max - snare_min)) * snare_vel;
      }

      psend_midi(MIDI_ON,
                 kit->snare,
                 snare_vel,
                 ENDPOINT_DRUM);
    }
  }


  if (downbeat(subbeat) && c->upbeat_high[ENDPOINT_DRUM]) {
    psend_midi(MIDI_ON,
               kit->hihat,
               vel * 1.0 * kit->hihat_vel,
               ENDPOINT_DRUM);
  }


  if (upbeat(subbeat) && c->upbeat[ENDPOINT_DRUM]) {
    if (!c->upbeat_high[ENDPOINT_DRUM] &&
	!c->doubled[ENDPOINT_DRUM] &&
	!c->pre_unique[ENDPOINT_DRUM]) {
      vel *= 1.4;
    }	
	
    psend_midi(MIDI_ON,
               kit->hihat,
               vel * 0.88 * kit->hihat_vel,
               ENDPOINT_DRUM);
  }

  if (preup(subbeat) && c->doubled[ENDPOINT_DRUM]) {
    psend_midi(MIDI_ON,
               kit->hihat,
               vel * 0.74 * kit->hihat_vel,
               ENDPOINT_DRUM);
  }

  if (predown(subbeat) && c->pre_unique[ENDPOINT_DRUM]) {
    psend_midi(MIDI_ON,
               kit->hihat,
               vel * 0.88 * kit->hihat_vel,
               ENDPOINT_DRUM);
  }
}

// Called on each beat, before its notes are played, with the lock held.  For
// changes that have to land on a beat -- speech.h's -- to land on this one
// rather than just after it.  NULL on the Pi.
static void (*before_beat_hook)(void) = NULL;

void arpeggiate(int subbeat, uint64_t current_time, bool drone, bool running) {
  if (subbeat == 0 && running && before_beat_hook) before_beat_hook();
  arpeggiate_endpoint(ENDPOINT_FOOTBASS, subbeat, current_time, drone);
  arpeggiate_endpoint(ENDPOINT_FOOTBASS_2, subbeat, current_time, drone);
  arpeggiate_endpoint(ENDPOINT_FOOTBASS_3, subbeat, current_time, drone);
  if (running || c->shorter[ENDPOINT_DRUM]) {
    arpeggiate_drum(subbeat, current_time);
  }
  arpeggiate_endpoint(ENDPOINT_ARP, subbeat, current_time, drone);
}

uint64_t delta(uint64_t a, uint64_t b) {
  return a > b ? a - b : b - a;
}

uint64_t min(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

uint64_t best_match_hit(uint64_t target, uint64_t* hits, int hit_len) {
  uint64_t best_error = NS_PER_SEC;  // default to being way off
  for (int i = 0; i < hit_len; i++) {
    uint64_t error = delta(target, hits[i]);
    if (error < best_error) {
      best_error = error;
    }
  }
  return best_error;
}

float estimate_tempo_helper(uint64_t current_time, bool consider_high) {
  // Take a super naive approach: for each candidate tempo, consider
  // how much error that would imply for each recent hit we've seen,
  // and take the tempo with the lowest error.
  //
  // If the hit doesn't fit the pattern, don't give up, treat it as
  // an extra hit and ignore it.

  float best_bpm = -1;
  // something way high, in case we fail to find anything
  uint64_t best_error = NS_PER_SEC * 100L;

  int n_downbeats_to_consider = 4;

  for (float candidate_bpm = 70;
       candidate_bpm < 140;
       candidate_bpm += 0.25) {

    uint64_t whole_note_ns = 60L * NS_PER_SEC / candidate_bpm;
    uint64_t candidate_error = 0;

    // Look for a kick or snare (or maybe anything) on every past downbeat
    for (int i = 0; i < n_downbeats_to_consider; i++) {
      uint64_t target = current_time - (i+1)*whole_note_ns;
      uint64_t error =
        min(best_match_hit(target, kick_times, KICK_TIMES_LENGTH),
            best_match_hit(target, snare_times, SNARE_TIMES_LENGTH));

      if (consider_high) {
	error = min(error,
		    best_match_hit(target, crash_times, CRASH_TIMES_LENGTH));
	error = min(error,
		    best_match_hit(target, hihat_times, HIHAT_TIMES_LENGTH));
      }

      candidate_error += error;
    }

    if (candidate_error < best_error) {
      best_error = candidate_error;
      best_bpm = candidate_bpm;
    }
  }

  if (best_bpm < 0) {
    printf("best_bpm = %.2f : not expected\n", best_bpm);
    return best_bpm;
  }

  uint64_t whole_beat = NS_PER_SEC * 60 / best_bpm;

  // Allow error of up to 1/32 note on each of the downbeats.
  uint64_t max_allowed_error = (whole_beat * n_downbeats_to_consider) / 32;

  bool acceptable_error = best_error < max_allowed_error;

  if (!acceptable_error) {
    return -1;
  }

  return best_bpm;
}


void estimate_tempo(uint64_t current_time, int note_in) {
  current_beat_ns = 0;

  float best_bpm = estimate_tempo_helper(current_time, /*consider_high=*/ false);
  if (best_bpm < 0 && (allow_all_drums_downbeat || drum_chooses_notes)) {
    best_bpm = estimate_tempo_helper(current_time, /*consider_high=*/ true);
  }

  if (best_bpm <= 0) {
    if (drum_chooses_notes) {
      arpeggiate(0, current_time, /*drone=*/false, /*running=*/false);
    }
    return;
  }

  // We have a tempo: best_bpm
  printf("Tempo selected: %f\n", best_bpm);
  
  uint64_t whole_beat = NS_PER_SEC * 60 / best_bpm;
  current_beat_ns = whole_beat;

  arpeggiate(0, current_time, /*drone=*/false, /*running=*/true);
  last_downbeat_ns = current_time;

  next_ns[0] = current_time;
  for (int i = 1; i < N_SUBBEATS; i++) {
    next_ns[i] = next_ns[i-1] + (whole_beat)/72;
    if (i == (jig_time ? 60 : 50)) {
      next_duck_peak_ns = next_ns[i];
    } else if (i == (jig_time ? 30 : 20)) {
      next_duck_trough_ns = next_ns[i];
    } else if (i == N_SUBBEATS - 1) {
      next_downbeat_ns = next_ns[i];
    }
  }
}

void count_drum_hit(int note_in) {
  uint64_t current_time = now();

  // When drum_chooses_some_notes only pedals 1, 3, and 4 should
  // affect the most recent pedal; otherwise we want to use all
  // pedals.
  if (drum_chooses_notes ||
      (drum_chooses_some_notes && !speech_chooses_notes &&
       (note_in == MIDI_PEDAL_1 ||
	note_in == MIDI_PEDAL_3 ||
	note_in == MIDI_PEDAL_4))) {
    int prev_pedal = most_recent_drum_pedal;
    most_recent_drum_pedal = note_in;    

    if (current_time - most_recent_choosy_drum_ts <
	MIDI_DRUM_CHORD_INTERVAL_MAX_NS) {
      if ((prev_pedal == MIDI_PEDAL_1 && note_in == MIDI_PEDAL_2) ||
	  (prev_pedal == MIDI_PEDAL_2 && note_in == MIDI_PEDAL_1)) {
	most_recent_drum_pedal = MIDI_PEDAL_12;
      } else if ((prev_pedal == MIDI_PEDAL_1 && note_in == MIDI_PEDAL_3) ||
		 (prev_pedal == MIDI_PEDAL_3 && note_in == MIDI_PEDAL_1)) {
	most_recent_drum_pedal = MIDI_PEDAL_13;
      } else if ((prev_pedal == MIDI_PEDAL_2 && note_in == MIDI_PEDAL_3) ||
		 (prev_pedal == MIDI_PEDAL_3 && note_in == MIDI_PEDAL_2)) {
	most_recent_drum_pedal = MIDI_PEDAL_23;
      } else if ((prev_pedal == MIDI_PEDAL_2 && note_in == MIDI_PEDAL_4) ||
		 (prev_pedal == MIDI_PEDAL_4 && note_in == MIDI_PEDAL_2)) {
	most_recent_drum_pedal = MIDI_PEDAL_24;
      } else if ((prev_pedal == MIDI_PEDAL_3 && note_in == MIDI_PEDAL_4) ||
		 (prev_pedal == MIDI_PEDAL_4 && note_in == MIDI_PEDAL_3)) {
	most_recent_drum_pedal = MIDI_PEDAL_34;
      } else if ((prev_pedal == MIDI_PEDAL_4 && note_in == MIDI_PEDAL_1) ||
		 (prev_pedal == MIDI_PEDAL_1 && note_in == MIDI_PEDAL_4)) {
	most_recent_drum_pedal = MIDI_PEDAL_41;
      }
    }    
  
    update_drum_pedal_note();
    most_recent_choosy_drum_ts = current_time;
  }

  if (note_in == MIDI_DRUM_IN_KICK) {
    kick_times[kick_times_index] = current_time;
    estimate_tempo(current_time, note_in);
    kick_times_index = (kick_times_index+1) % KICK_TIMES_LENGTH;
  } else if (note_in == MIDI_DRUM_IN_SNARE) {
    snare_times[snare_times_index] = current_time;
    estimate_tempo(current_time, note_in);
    snare_times_index = (snare_times_index+1) % SNARE_TIMES_LENGTH;
  } else if (note_in == MIDI_DRUM_IN_CRASH) {
    crash_times[crash_times_index] = current_time;
    estimate_tempo(current_time, note_in);
    crash_times_index = (crash_times_index+1) % CRASH_TIMES_LENGTH;
  } else if (note_in == MIDI_DRUM_IN_HIHAT) {
    hihat_times[hihat_times_index] = current_time;
    estimate_tempo(current_time, note_in);
    hihat_times_index = (hihat_times_index+1) % HIHAT_TIMES_LENGTH;
  }
}

// A spoken Nashville number, 1-7, while speech is choosing: that degree
// of the major scale on the root, with the chord the major key puts there --
// I ii iii IV V vi vii-diminished.  Always the major key, whatever the arrow
// keys say, the way a number chart reads; the arrows still steer what the
// whistle snaps to.
void nashville_picks_chord(int number) {
  static const int DEGREE[7] = {0, 2, 4, 5, 7, 9, 11};
  static const int QUALITY[7] = {
    CHORD_MAJOR, CHORD_MINOR, CHORD_MINOR, CHORD_MAJOR, CHORD_MAJOR,
    CHORD_MINOR, CHORD_DIM,
  };
  if (!speech_chooses_notes || number < 1 || number > 7) return;

  // What update_drum_pedal_note does with a pedal's note, minus the pedal.
  int note = to_root(root_note + DEGREE[number - 1]);
  last_drum_pedal_note = current_drum_pedal_note;
  prev_chord_note = chord_note;
  prev_chord_type = chord_type;
  chord_type = QUALITY[number - 1];
  chord_note = note;
  current_drum_pedal_note = note;
  update_bass(/*force_refresh=*/false);
}

// Play in another key: the root the bass lines and drones are built on.
// When the drum or a voice has picked a chord, that chord moves
// with the key -- the IV stays the IV -- or everything built on it would stay
// in the old key until the next chord was picked.
void change_key(int pitch_class) {
  int shift = to_root(pitch_class) - root_note;
  current_drum_pedal_note = to_root(current_drum_pedal_note + shift);
  last_drum_pedal_note = to_root(last_drum_pedal_note + shift);
  chord_note = to_root(chord_note + shift);
  prev_chord_note = to_root(prev_chord_note + shift);
  root_note = to_root(pitch_class);
  fifth_note = to_root(root_note + 7);
  update_bass(/*force_refresh=*/false);
}

void send_chord(int note_out, int vel, int endpoint) {
  psend_midi(MIDI_ON, to_root(note_out), vel, endpoint);
  if (// chord_type isn't defined when reading notes from piano
      (drum_chooses_notes || drum_chooses_some_notes) &&
      // need to turn on thirds
      c->shortish[endpoint]) {
    psend_midi(MIDI_ON, to_root(note_out + (chord_type == CHORD_MAJOR ? 4 : 3)), vel, endpoint);
  }

  int fifth = note_out + 7;
  if (// chord_type isn't defined when reading notes from piano
      (drum_chooses_notes || drum_chooses_some_notes) &&
      chord_type == CHORD_DIM) {
    fifth -= 1;
  }

  psend_midi(MIDI_ON, to_root(fifth), vel, endpoint);
}

void update_bass(bool force_refresh) {
  int bass_out = active_note();
  int chord_out = active_chord();

  bool note_changed = bass_out != last_update_bass_note;

  uint64_t current_time = now();
  if (current_time - last_downbeat_ns > NS_PER_SEC && note_changed) {
    arpeggiate(0, current_time, /*drone=*/true, /*running=*/false);
  }

  last_update_bass_note = bass_out;

  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (!holds_bass_note(endpoint)) continue;
    int note_out = c->chord[endpoint] ? chord_out : bass_out;

    if (!c->on[endpoint]) continue;
    if (current_note[endpoint] == note_out &&
        !(drum_chooses_notes && c->shorter[endpoint])) continue;
    if (endpoint == ENDPOINT_JAWHARP &&
	(breath < 3 && !c->ducked[endpoint])) continue;
    if (endpoint != ENDPOINT_JAWHARP && !note_changed && !force_refresh) {
      continue;
    }

    int vel = MIDI_MAX;
    if (is_drone(endpoint)) {
      vel = c->chord[endpoint] ? DRONE_CHORD_VELOCITY : DRONE_BASS_VELOCITY;
    }

    drone_endpoint_off(endpoint);
    if (c->chord[endpoint]) {
      send_chord(note_out, vel, endpoint);
    } else {
      psend_midi(MIDI_ON, note_out, vel, endpoint);
    }
    current_note[endpoint] = note_out;
  }
}

char mapping(unsigned char note_in) {
  switch(note_in) {
  case 98: return 10;  // Bb
  case 97: return 12;  // C
  case 96: return 14;  // D
  case 95: return 16;  // E
  case 94: return 18;  // F#
  case 93: return 20;  // G#
  case 92: return 22;  // Bb

  case 91: return 15;  // Eb
  case 90: return 17;  // F
  case 89: return 19;  // G
  case 88: return 21;  // A
  case 87: return 23;  // B
  case 86: return 25;  // C#
  case 85: return 27;  // Eb

  case 84: return 22;  // Bb
  case 83: return 24;  // C
  case 82: return 26;  // D
  case 81: return 28;  // E
  case 80: return 30;  // F#
  case 79: return 32;  // G#
  case 78: return 34;  // Bb

  case 77: return 27;  // Eb
  case 76: return 29;  // F
  case 75: return 31;  // G
  case 74: return 33;  // A
  case 73: return 35;  // B
  case 72: return 37;  // C#
  case 71: return 39;  // Eb

  case 70: return 34;  // Bb
  case 69: return 36;  // C
  case 68: return 38;  // D
  case 67: return 40;  // E
  case 66: return 42;  // F#
  case 65: return 44;  // G#
  case 64: return 46;  // Bb

  case 63: return 39;  // Eb
  case 62: return 41;  // F
  case 61: return 43;  // G
  case 60: return 45;  // A
  case 59: return 47;  // B
  case 58: return 49;  // C#
  case 57: return 51;  // Eb

  case 56: return 46;  // Bb
  case 55: return 48;  // C
  case 54: return 50;  // D
  case 53: return 52;  // E
  case 52: return 54;  // F#
  case 51: return 56;  // G#
  case 50: return 58;  // Bb

  case 49: return 53;  // F
  case 48: return 55;  // G
  case 47: return 57;  // A
  case 46: return 59;  // B
  case 45: return 61;  // C#
  case 44: return 63;  // Eb
  case 43: return 65;  // F

  case 42: return 58;  // Bb
  case 41: return 60;  // C
  case 40: return 62;  // D
  case 39: return 64;  // E
  case 38: return 66;  // F#
  case 37: return 68;  // G#
  case 36: return 70;  // Bb

  case 35: return 65;  // F
  case 34: return 67;  // G
  case 33: return 69;  // A
  case 32: return 71;  // B
  case 31: return 73;  // C#
  case 30: return 75;  // Eb
  case 29: return 77;  // F

  case 28: return 70;  // Bb
  case 27: return 72;  // C
  case 26: return 74;  // D
  case 25: return 76;  // E
  case 24: return 78;  // F#
  case 23: return 80;  // G#
  case 22: return 82;  // Bb

  case 21: return 77;  // F
  case 20: return 79;  // G
  case 19: return 81;  // A
  case 18: return 82;  // B
  case 17: return 84;  // C#
  case 16: return 86;  // Eb
  case 15: return 88;  // F

  case 14: return 82;  // Bb
  case 13: return 84;  // C
  case 12: return 86;  // D
  case 11: return 88;  // E
  case 10: return 90;  // F#
  case  9: return 92;  // G#
  case  8: return 94;  // Bb

  case  7: return 89;  // F
  case  6: return 91;  // G
  case  5: return 93;  // A
  case  4: return 95;  // B
  case  3: return 97;  // C#
  case  2: return 99;  // Eb
  case  1: return 101; // F

  default:
    return 0;
  }
}

void handle_piano(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (note_in > MIDI_MAX) {
    return;
  }

  piano_notes[note_in] = (mode == MIDI_ON);

  unsigned int max_bass = 50;
  bool is_bass = note_in < max_bass;

  // When lifting a finger off, fall back to the lowest finger that is still down.
  if (is_bass) {
    int root_candidate_note_in = -1;
    if (mode == MIDI_ON) {
      piano_left_hand_velocity = val;
      root_candidate_note_in = note_in;
    } else {
      for (int i = 0; i < max_bass; i++) {
        if (piano_notes[i]) {
          root_candidate_note_in = i;
          break;
        }
      }
    }
    if (root_candidate_note_in != -1) {
      int new_root = to_root(root_candidate_note_in);
      if (new_root != root_note) {
        root_note = new_root;
        fifth_note = to_root(new_root + 7);
        update_bass(/*force_refresh=*/false);
      }
    }
  }

  if (c->on[ENDPOINT_FLEX]) {
    psend_midi(mode, note_in, MIDI_MAX, ENDPOINT_FLEX);
  }
  if (c->on[ENDPOINT_LOW] && is_bass) {
    psend_midi(mode, note_in,
              c->vel[ENDPOINT_LOW] ? val : 75,
              ENDPOINT_LOW);
  }
  if (c->on[ENDPOINT_HI] && !is_bass) {
    psend_midi(mode, note_in,
              c->vel[ENDPOINT_HI] ? val : 75,
              ENDPOINT_HI);
  }
  if (c->on[ENDPOINT_OVERLAY]) {
    psend_midi(mode, note_in,
              c->vel[ENDPOINT_OVERLAY] ? val : 75,
              ENDPOINT_OVERLAY);
  }
}

void full_reset() {
  voices_reset();
  all_notes_off();
}

void toggle_air_locked() {
  c->air_lockeds[c->selected_endpoint] = !c->air_lockeds[c->selected_endpoint];
  c->locked_airs[c->selected_endpoint] = air;
}

void toggle_follows_air() {
  c->follows_air[c->selected_endpoint] = !c->follows_air[c->selected_endpoint];
  psend_midi(MIDI_CC, CC_11,
	     c->follows_air[c->selected_endpoint] ? 0 : MIDI_MAX,
	     c->selected_endpoint);
}

void toggle_ducked() {
  c->ducked[c->selected_endpoint] = !c->ducked[c->selected_endpoint];
  psend_midi(MIDI_CC, CC_11,
	     (c->ducked[c->selected_endpoint] ||
	      c->selected_endpoint == ENDPOINT_JAWHARP) ? 0 : MIDI_MAX,
	     c->selected_endpoint);
  reload_voice_setting(c);
  update_bass(/*force_refresh=*/true);
}

void toggle_endpoint(int endpoint) {
  c->selected_endpoint = endpoint;
  endpoint_notes_off(c->selected_endpoint);
  c->on[c->selected_endpoint] = !c->on[c->selected_endpoint];

  if (holds_bass_note(endpoint)) {
    if (c->on[endpoint]) {
      update_bass(/*force_refresh=*/true);
    } else {
      drone_endpoint_off(endpoint);
    }
  }
}

void handle_keypad(unsigned int mode, unsigned char note_in, unsigned int val) {
  if (mode != MIDI_ON) return;

  printf("recv: %c\n", note_in);

  int selected_voice = c->voices[c->selected_endpoint];

  if (c->selected_endpoint == ENDPOINT_DRUM) {
    switch (note_in) {
    case 'A': select_drum_kit(KIT_RIM); return;
    case 'Z': select_drum_kit(KIT_808_A); return;
    case 'X': select_drum_kit(KIT_808_B); return;
    case 'C': select_drum_kit(KIT_ROOM2); return;
    case 'V': select_drum_kit(KIT_ROOM6); return;
    // The rest of the voice keys do nothing with the drum selected.  They
    // must still return, or they'd fall through and pick a melodic voice
    // for a channel that's playing a percussion set.
    case 'S': case 'D': case 'F': case 'G': case 'H':
    case 'B': case 'N': case 'M':
      return;
    }
  }

  // With a drone selected the voice keys pick from its own short list of
  // pads instead -- see DRONE_VOICES.  Keys without one do nothing, for the
  // same reason as with the drum: falling through would put a voice on the
  // drone that isn't on the keyboard.
  if (is_drone(c->selected_endpoint)) {
    int index = drone_voice_for_note(note_in);
    if (index >= 0) {
      select_voice(c, DRONE_VOICES[index].program);
      return;
    }
    if (is_voice_key(note_in)) return;
  }

  switch (note_in) {
  case DELETE:
    // Manual volume entry
    c->manual_volumes[selected_voice] = val;
    reload_voice_setting(c);
    return;
  case ESCAPE:
    full_reset();
    return;
  case F1:
    clear_endpoint();
    return;
  case F2:
    c->pans[c->selected_endpoint] = !c->pans[c->selected_endpoint];
    reload_voice_setting(c);
    return;
  case '-':
    c->volume_deltas[c->selected_endpoint] -= 5;
    reload_voice_setting(c);
    return;
  case '=': // +
    c->volume_deltas[c->selected_endpoint] += 5;
    reload_voice_setting(c);
    return;
  case '`':
    c->selected_endpoint = ENDPOINT_DRUM;
    return;
  case 'r': // tab
    toggle_endpoint(ENDPOINT_DRUM);
    return;
  case '1':
    c->selected_endpoint = ENDPOINT_JAWHARP;
    return;
  case 'Q':
    toggle_endpoint(ENDPOINT_JAWHARP);
    return;
  case '2':
    c->selected_endpoint = ENDPOINT_FOOTBASS;
    return;
  case 'W':
    toggle_endpoint(ENDPOINT_FOOTBASS);
    update_bass(/*force_refresh=*/true);
    return;
  case '3':
    c->selected_endpoint = ENDPOINT_ARP;
    return;
  case 'E':
    toggle_endpoint(ENDPOINT_ARP);
    update_bass(/*force_refresh=*/true);
    return;
  // The extra foot basses.  Their pseudo-notes are 's'..'v' rather than
  // anything mnemonic because the obvious ones are taken: '2' and '3' have
  // meant "select the foot bass" and "select the arp" since kbd.py, and the
  // Mac's number row keys carry these instead.  Nothing on the Pi sends them
  // yet -- kbd.py has no key spare on the number row -- so there they are
  // reachable only by adding one.
  case 't':
    c->selected_endpoint = ENDPOINT_FOOTBASS_2;
    return;
  case 's':
    toggle_endpoint(ENDPOINT_FOOTBASS_2);
    update_bass(/*force_refresh=*/true);
    return;
  case 'v':
    c->selected_endpoint = ENDPOINT_FOOTBASS_3;
    return;
  case 'u':
    toggle_endpoint(ENDPOINT_FOOTBASS_3);
    update_bass(/*force_refresh=*/true);
    return;
  case '4':
    c->selected_endpoint = ENDPOINT_FLEX;
    return;
  case 'R':
    toggle_endpoint(ENDPOINT_FLEX);
    return;
  case '5':
    c->selected_endpoint = ENDPOINT_LOW;
    return;
  case 'T':
    toggle_endpoint(ENDPOINT_LOW);
    return;
  case '6':
    c->selected_endpoint = ENDPOINT_HI;
    return;
  case 'Y':
    toggle_endpoint(ENDPOINT_HI);
    return;
  case '7':
    c->selected_endpoint = ENDPOINT_OVERLAY;
    return;
  case 'U':
    toggle_endpoint(ENDPOINT_OVERLAY);
    return;
  case '8':
    c->selected_endpoint = ENDPOINT_DRONE_BASS;
    return;
  case 'I':
    toggle_endpoint(ENDPOINT_DRONE_BASS);
    return;
  case '9':
    c->selected_endpoint = ENDPOINT_DRONE_CHORD;
    return;
  case 'O':
    toggle_endpoint(ENDPOINT_DRONE_CHORD);
    return;
  // The second drones.  'w'..'z' for the same reason the extra foot basses
  // are 's'..'v': '8' and '9' already mean "select the first drones", and
  // the Mac's own 8 and 9 carry these instead.  Nothing on the Pi sends them.
  case 'x':
    c->selected_endpoint = ENDPOINT_DRONE_BASS_2;
    return;
  case 'w':
    toggle_endpoint(ENDPOINT_DRONE_BASS_2);
    return;
  case 'z':
    c->selected_endpoint = ENDPOINT_DRONE_CHORD_2;
    return;
  case 'y':
    toggle_endpoint(ENDPOINT_DRONE_CHORD_2);
    return;

  case 'J':
    c->downbeat[c->selected_endpoint] = !c->downbeat[c->selected_endpoint];
    return;
  case 'K':
    c->upbeat[c->selected_endpoint] = !c->upbeat[c->selected_endpoint];
    return;
  case 'P':
    c->doubled[c->selected_endpoint] = !c->doubled[c->selected_endpoint];
    return;
  case '[':
    c->pre_unique[c->selected_endpoint] = !c->pre_unique[c->selected_endpoint];
    return;
  case ']':
    endpoint_notes_off(c->selected_endpoint);
    c->octave_deltas[c->selected_endpoint]++;
    return;
  case '\\':
    endpoint_notes_off(c->selected_endpoint);
    c->octave_deltas[c->selected_endpoint]--;
    return;
  case 'L':
    c->upbeat_high[c->selected_endpoint] = !c->upbeat_high[c->selected_endpoint];
    return;
  case ';':
    c->shortish[c->selected_endpoint] = !c->shortish[c->selected_endpoint];
    return;
  case '\'':
    c->shorter[c->selected_endpoint] = !c->shorter[c->selected_endpoint];
    return;
  case ',':
    c->chord[c->selected_endpoint] = !c->chord[c->selected_endpoint];
    endpoint_notes_off(c->selected_endpoint);
    return;
  case '.':
    c->vel[c->selected_endpoint] = !c->vel[c->selected_endpoint];
    return;
  case '/':
    fade_target = fade_target == 0 ? MAX_FADE : 0;
    return;
  case F4:
    toggle_ducked();
    return;
  case F5:
    // From speech choosing, this hands the choice back to the feet rather
    // than switching Drum Some off: asking for Drum Some is asking for the
    // feet to choose.
    if (speech_chooses_notes) {
      speech_chooses_notes = false;
      return;
    }
    drum_chooses_some_notes = !drum_chooses_some_notes;
    if (drum_chooses_some_notes) {
      most_recent_drum_pedal = MIDI_PEDAL_3;
    }
    return;
  case SPEECH_PICKS:
    speech_chooses_notes = !speech_chooses_notes;
    // It is Drum Some underneath, so it comes and goes with it, and it can't
    // share the job with the drum picking every note.
    drum_chooses_some_notes = speech_chooses_notes;
    if (speech_chooses_notes) {
      drum_chooses_notes = false;
      most_recent_drum_pedal = MIDI_PEDAL_3;
    }
    return;
  case F6:
    toggle_air_locked();
    return;
  case F7:
    toggle_follows_air();
    return;
  case '0':
    jig_time = !jig_time;
    return;
  case F10:
    allow_all_drums_downbeat = !allow_all_drums_downbeat;
    return;
  case F9:
    drum_chooses_notes = !drum_chooses_notes;
    if (drum_chooses_notes && speech_chooses_notes) {
      speech_chooses_notes = false;
      drum_chooses_some_notes = false;
    }
    if (drum_chooses_notes) {
      // The drum picking notes is only useful with a foot bass to play them,
      // so switching it on brings up the setup that goes with it.  Set rather
      // than toggle, so it lands the same way however things were left.
      c->selected_endpoint = ENDPOINT_FOOTBASS;
      c->upbeat[ENDPOINT_FOOTBASS] = false;  // on by default for foot bass
      c->vel[ENDPOINT_FOOTBASS] = true;
      c->octave_deltas[ENDPOINT_FOOTBASS] = 1;
      c->volume_deltas[ENDPOINT_FOOTBASS] = 35;
      if (!c->on[ENDPOINT_FOOTBASS]) {
        toggle_endpoint(ENDPOINT_FOOTBASS);
      }
      select_voice(c, 32);  // acoustic bass; refreshes the bass note for us
    }
    return;
  case UP:
    musical_mode = MODE_MAJOR;
    return;
  case LEFT:
    musical_mode = MODE_MIXO;
    return;
  case DOWN:
    musical_mode = MODE_MINOR;
    return;
  case RIGHT:
    musical_mode = MODE_BETH_COHENS;
    return;
  case F8:
    root_note = to_root(val);
    fifth_note = to_root(root_note + 7);
    update_bass(/*force_refresh=*/false);
    return;

  // punchy
  case 'A': select_voice(c, 39); return;
  case 'S': select_voice(c, 38); return;
  case 'D': select_voice(c, 32); return;
    // rejected 12, 0, 8, 45, 33, 5, 12
  case 'N': select_voice(c, 87); return;
  case 'F': select_voice(c, 16); return;
  case 'M': select_voice(c, 15); return;
  case 'G': select_voice(c, 35); return;
  case 'H': select_voice(c, 18); return;
  // continuous
  case 'Z': select_voice(c, 75); return;
  case 'X': select_voice(c, 85); return;
  case 'C': select_voice(c,  4); return;
  case 'V': select_voice(c, 67); return;
  case 'B': select_voice(c, 81); return;
  }
}

int remap(int val, int min, int max) {
  int range = max - min;
  return val * range / MIDI_MAX + min;
}

void handle_feet(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (mode != MIDI_ON) {
    return;
  }

  if (note_in == MIDI_DRUM_IN_KICK_2 ||
      note_in == MIDI_DRUM_IN_KICK_3) {
    note_in = MIDI_DRUM_IN_KICK;
  } else if (note_in == MIDI_DRUM_IN_SNARE_2) {
    note_in = MIDI_DRUM_IN_SNARE;
  }
  
  if (note_in == MIDI_DRUM_IN_KICK || drum_chooses_notes) {
    last_fb_vel = val;
  }

  //printf("foot: %d %d\n", note_in, val);
  count_drum_hit(note_in);
  if (drum_chooses_notes ||
      (drum_chooses_some_notes &&
       note_in != MIDI_DRUM_IN_KICK)) {
    update_bass(/*force_refresh=*/false);
  }
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc != CC_BREATH && cc != CC_11) {
    printf("Unknown Control change %d\n", cc);
    return;
  }

  //printf("V %d %.0f%%\n", val,
  //       100.0 * (now() - last_downbeat_ns) / 
  //       (next_downbeat_ns - last_downbeat_ns));
  
  breath = val;

  // pass other control change to all synths that care about it:
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_FLEX) {
      continue;
    }
    if (c->ducked[endpoint]) continue;
    int use_val = normalize(val);

    if (endpoint == ENDPOINT_JAWHARP) {
      if (breath < 10) {
        drone_endpoint_off(ENDPOINT_JAWHARP);
      } else if (breath > 20) {
        update_bass(/*force_refresh=*/false);
      }
    }

    if (endpoint == ENDPOINT_FLEX) {
      flex_breath = use_val;
      use_val = flex_val();
      last_flex_val = use_val;
    }
    psend_midi(MIDI_CC, CC_11, use_val, endpoint);
  }
}

const char* note_str(int note) {
  switch (note % 12) {
  case 0:
    return "C ";
  case 1:
    return "Db";
  case 2:
    return "D ";
  case 3:
    return "Eb";
  case 4:
    return "E ";
  case 5:
    return "F ";
  case 6:
    return "F#";
  case 7:
    return "G ";
  case 8:
    return "G#";
  case 9:
    return "A ";
  case 10:
    return "Bb";
  case 11:
    return "B ";
  default:
    return "??";
  }
}

void handle_axis_49(int mode, int note_in, int val) {
  handle_piano(mode, note_in, val);
}

void calculate_breath_speeds() {
  // We're modeling a bag that gets blown up from the breath controller and then
  // slowly deflates on its own.  Each tick we want to put `breath` air into the
  // bag, and let out some air for leakage.
  //
  // We want a pretty big stretchy bag, because we want to be able to take a
  // breath without losing the energy.  I can comfortably take a breath in half
  // a second, so lets say a 5s half life.
  int half_life_ms = 5000;
  int half_life_ticks = half_life_ms / TICK_MS;

  // To make the bag lose half its air in a given number of ticks we want:
  //
  //   leakage^half_life_ticks = 0.5
  //
  // So, what's leakage?  Take the log of both sides, reorder, then
  // exponentiate:
  //
  //   ln(leakage^half_life_ticks) = ln(0.5)
  //   half_life_ticks * ln(leakage) = ln(0.5)
  //   ln(leakage) = ln(0.5) / half_life_ticks
  //   leakage = e^(ln(0.5) / half_life_ticks)
  leakage = exp(log(0.5) / half_life_ticks);
  printf("Calculated that to leak half the air in %dms (%d ticks) we "
         "should scale by %.4f on each tick.\n", half_life_ms, half_life_ticks,
         leakage);

  // Model the bag as being a bit bigger than MIDI_MAX in order to allow
  // holding the synth at MIDI_MAX without constant breath.  Specifically, we
  // want half a second of breath=0 to bring the bag from its maximum volume
  // down to MIDI_MAX.
  int half_a_second_ticks = 1000 / TICK_MS;
  double half_a_second_leakage = pow(leakage, half_a_second_ticks);
  max_air = 1/half_a_second_leakage * MIDI_MAX;
  printf("Calculated that in half a second we leak down to %.0f%% full, so "
         "we should oversize the bag to %.0f%%\n", half_a_second_leakage*100,
         1/half_a_second_leakage*100);

  // Lets's blow the bag up linearly (ignoring leakage).
  // TODO: play with making the bag get somewhat full quickly, but then take
  // more effort to get all the way full.
  int fill_time_ms = 1000;
  int fill_time_ticks = fill_time_ms / TICK_MS;
  breath_gain = max_air / fill_time_ticks / MIDI_MAX;
  printf("Calculated that to fill the bag to %.2f at max breath in %dms "
         "(%d ticks) we should inflate by %.6f of the breath value each tick\n",
         max_air, fill_time_ms, fill_time_ticks, breath_gain);
}

void jml_setup() {
  calculate_breath_speeds();
  full_reset();

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }
}

void update_air() {
  // see calculate_breath_speeds()
  air *= leakage;
  air += breath * breath_gain;
  if (air > max_air) {
    air = max_air;
  }
  // It's ok that air > MIDI_MAX (because max_air > MIDI_MAX) because
  // everything that uses this will only allow a max of MIDI_MAX.
}

int last_air_val = 0;
void forward_air() {
  int val = air;

  flex_base = val;
  int flex_value = flex_val();

  if (val > MIDI_MAX) {
    val = MIDI_MAX;
  }

  if (val != last_air_val) {
    for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
      if (c->follows_air[endpoint]) {
	psend_midi(MIDI_CC, CC_11,
		   c->air_lockeds[endpoint] ? c->locked_airs[endpoint] : val,
		   endpoint);
      }
    }

    last_air_val = val;
  }
  if (flex_value != last_flex_val) {
    psend_midi(MIDI_CC, CC_11, flex_value, ENDPOINT_FLEX);
    last_flex_val = flex_value;
  }
}

int last_duck_val = 0;
void duck() {
  bool any_ducked = false;
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (c->ducked[endpoint]) {
      any_ducked = true;
    }
  }
  if (!any_ducked) return;

  uint64_t current_time = now();

  // Curve ramps down to 0 at next_duck_trough_ns, then up
  // to target_peak at next_duck_peak_ns, and back.  All linear.
  int target_peak = MIDI_MAX;
  
  uint64_t peak_to_peak = next_downbeat_ns - last_downbeat_ns;
  uint64_t future_trough_ns = next_duck_trough_ns + peak_to_peak;
  uint64_t past_peak_ns = next_duck_peak_ns - peak_to_peak;
  
  int duck_val;
  if (current_time >= future_trough_ns) {
    duck_val = 0;
  } else if (current_time >= next_duck_peak_ns) {
    // Ramping down to 0 at future_trough_ns
    uint64_t swell_time = future_trough_ns - next_duck_peak_ns;
    uint64_t progress = current_time - next_duck_peak_ns;
    duck_val = target_peak * (swell_time - progress) / swell_time;
  } else if (current_time >= next_duck_trough_ns) {
    // Ramping up to target_peak  at next_duck_peak_ns
    uint64_t swell_time = next_duck_peak_ns - next_duck_trough_ns;
    uint64_t progress = current_time - next_duck_trough_ns;
    duck_val = target_peak * progress / swell_time;
  } else {
    // Ramping down to 0 at next_duck_trough_ns
    uint64_t swell_time = next_duck_trough_ns - past_peak_ns;
    uint64_t progress = current_time - past_peak_ns;
    duck_val = target_peak * (swell_time - progress) / swell_time;	  
  }

  if (last_duck_val != duck_val) {
    for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
      if (c->ducked[endpoint]) {
	psend_midi(MIDI_CC, CC_11,
		   endpoint == ENDPOINT_JAWHARP ? duck_val * 0.8 : duck_val,
		   endpoint);
	if (holds_bass_note(endpoint)) {
	  if (duck_val < 3 && current_note[endpoint] != -1) {
	    drone_endpoint_off(endpoint);
	  } else if (current_note[endpoint] == -1) {
	    update_bass(/*force_refresh=*/true);
	  }
	}
      }
    }
    last_duck_val = duck_val;
  }  
}

void trigger_subbeats() {
  uint64_t current_time = now();

  for (int i = 1 /* 0 is triggered by kick directly */; i < N_SUBBEATS; i++) {
    if (next_ns[i] > 0 && current_time > next_ns[i]) {
      arpeggiate(i, current_time, /*drone=*/false, /*running=*/true);
      next_ns[i] = 0;
    }
  }
}

// Per foot bass, because each one carries its own short flags and so its own
// threshold: the whole point of the extra two is that they let go at
// different times from the original.
static void maybe_end_footbass_notes(int endpoint) {
  if (!c->shortish[endpoint] && !c->shorter[endpoint]) return;

  int threshold = NS_PER_SEC;
  if (c->shortish[endpoint]) {
    threshold /= 2;
  }
  if (c->shorter[endpoint]) {
    threshold /= 4;
  }

  if (now() - c->last_arpeggiation[endpoint] > threshold) {
    endpoint_notes_off(endpoint);
  }
}

void maybe_end_notes() {
  if (!drum_chooses_notes) return;
  maybe_end_footbass_notes(ENDPOINT_FOOTBASS);
  maybe_end_footbass_notes(ENDPOINT_FOOTBASS_2);
  maybe_end_footbass_notes(ENDPOINT_FOOTBASS_3);
}

uint64_t tick_n = 0;
uint64_t subtick_n = 0;
void jml_tick() {

  // play startup chime, quietly: it's to say the rig is up, not to be heard
  // across the room
  if (tick_n == 0) {
    psend_midi(MIDI_ON, 28, 30, ENDPOINT_LOW);
  } else if (tick_n == 500) {
    psend_midi(MIDI_OFF, 28, 30, ENDPOINT_LOW);
    psend_midi(MIDI_ON, 33, 30, ENDPOINT_LOW);
  } else if (tick_n == 2000) {
    psend_midi(MIDI_OFF, 33, 30, ENDPOINT_LOW);
  }

  // Called every TICK_MS
  update_air();
  forward_air();
  duck();
  trigger_subbeats();
  maybe_end_notes();
  maybe_end_pitched_kick();

  // We fade from 100 to 0 over 4000ms, so we want to progress every 40 ticks.
  if (tick_n % 40 == 0) {
    progress_fades();
  }

  if (++tick_n % 450 == 0) {
#ifdef FAKE_FEET
    handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
#endif

#ifdef FAKE_CHANGE_PITCH
    if (++subtick_n % 2 == 0) {
      root_note = to_root(root_note + 1);
      update_bass(/*force_refresh=*/false);
    }
#endif
  }
}

#endif
