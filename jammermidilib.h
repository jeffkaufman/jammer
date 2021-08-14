#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2

/*
  Controls:

  rst  odr  TBD   bt1  pd1  jaw  s/t
  arl  rho  ham  atd  pd2  flx   HH
  arp  app  ldp  rdh  rds  rdk  fcf
  rht  TBD  tdb  api  pls  TBD  brc
  ...

  maj min mix               b3  b24
*/
#define FULL_RESET                  7
#define AIR_LOCK                 14

#define TOGGLE_OVERDRIVEN_RHODES     6
#define TOGGLE_RHODES            13

#define TOGGLE_HAMMOND           12

#define TOGGLE_ATMOSPHERIC_DRONE 11

#define TOGGLE_SINE_PAD              3
#define TOGGLE_SWEEP_PAD         10

#define TOGGLE_JAWHARP               2
#define TOGGLE_ORGAN_FLEX         9

#define TOGGLE_ARP_DOWNBEAT         8
#define TOGGLE_ARP_UPBEAT_HIGH      27
#define TOGGLE_ARP_DOUBLED          24
#define ROTATE_ARP_VOICE            25
#define ROTATE_JAWHARP_VOICE        28

#define TOGGLE_ARPEGGIATOR          21
#define TOGGLE_MANUAL_TSS           17
#define TOGGLE_MANUAL_KICK          16

#define TOGGLE_DRUM_BREATH          26
#define TOGGLE_JIG_REEL             23
#define TOGGLE_BREATH_CHORD         22

#define CONTROL_MAX TOGGLE_DRUM_BREATH

// keyboard controls (0-12), used by handle_keypad
#define CFG_RESET 0
#define CFG_JAWHARP 1
#define CFG_FLEX 2
#define CFG_SINE_PAD 3
#define CFG_ARP_VOICE 4
#define CFG_ARPEGGIATOR 5
#define CFG_JIG_REEL 6
#define CFG_DOWNBEAT 7
#define CFG_UPBEAT_HIGH 8
#define CFG_DOUBLED 9
// available: 10 backspace
// available: 11 enter
#define CFG_JAWHARP_VOICE 12 // dot

#define BUTTON_ADJUST_3 93
#define BUTTON_ADJUST_24 92

#define BUTTON_MAJOR 98
#define BUTTON_MIXO 97
#define BUTTON_MINOR 96
#define BUTTON_RACOON 95

#define N_ARPEGGIATOR_PATTERNS 8
#define N_AUTO_HIHAT_MODES 9
#define N_DRUM_KIT_SOUNDS 5


#define STATE_DEFAULT 0
#define STATE_ADJUST_3 1
#define STATE_ADJUST_24 2

#define FOOTBASS_VOLUME 120

// gcmidi sends on CC 20 through 29
#define GCMIDI_MIN 20
#define GCMIDI_MAX 29

#define CC_ROLL 30
#define CC_PITCH 31

#define MIDI_DRUM_IN_SNARE 46
#define MIDI_DRUM_IN_KICK 38
#define MIDI_DRUM_IN_CRASH 59
#define MIDI_DRUM_IN_HIHAT 51

#define MIDI_MAX 127

#define TICK_MS 1  // try to tick every N milliseconds

#define MODE_MAJOR 0
#define MODE_MIXO 1
#define MODE_MINOR 2
#define MODE_RACOON 3

#define KICK_TIMES_LENGTH 12
#define SNARE_TIMES_LENGTH 12
#define CRASH_TIMES_LENGTH 12
#define HIHAT_TIMES_LENGTH 12

#define NS_PER_SEC 1000000000LL

#define N_NOTE_DIVISIONS 72
#define N_SUBBEATS (N_NOTE_DIVISIONS)

#define MAX_SCHEDULED_NOTES 64

#define MAX_HAMMOND_NOTE 96  // the hammond can't play higher than this note

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



int normalize(int val) {
  if (val > MIDI_MAX) {
    return MIDI_MAX;
  }
  if (val < 0) {
    return 0;
  }
  return val;
}

struct ScheduledNote {
  uint64_t ts;
  char actionType;
  int noteNo;
  int velocity;
  int endpoint;
};

bool piano_on = false;  // Initialized based on availablity of piano.


/* Anything mentioned here should be initialized in voices_reset */
bool jawharp_on;
bool hammond_on;
bool organ_low_on;
bool organ_flex_on;
bool sine_pad_on;
bool sweep_pad_on;
bool overdriven_rhodes_on;
bool rhodes_on;
bool tbd_a_on;
bool tbd_b_on;
int auto_hihat_mode;
bool manual_kick_on;
bool manual_tss_on;
int drum_kit_sound;
bool atmospheric_drone;
bool atmospheric_drone_notes[MIDI_MAX];
bool piano_notes[MIDI_MAX];
bool breath_chord_notes[MIDI_MAX];
bool arpeggiator_on;
bool arp_downbeat;
bool arp_upbeat_high;
bool arp_doubled;
int current_arp_voice;
int current_jawharp_voice;
bool drum_breath_on;
int current_arpeggiator_note;
int button_endpoint;
int root_note;
bool air_locked;
double locked_air;
int musical_mode;
unsigned int whistle_anchor_note;
uint64_t kick_times[KICK_TIMES_LENGTH];
int kick_times_index;
uint64_t snare_times[SNARE_TIMES_LENGTH];
int snare_times_index;
uint64_t crash_times[CRASH_TIMES_LENGTH];
int crash_times_index;
uint64_t hihat_times[HIHAT_TIMES_LENGTH];
int hihat_times_index;
uint64_t next_ns[N_SUBBEATS];
int state;
struct ScheduledNote scheduled_notes[MAX_SCHEDULED_NOTES];
uint64_t current_beat_ns;
uint64_t last_downbeat_ns;
bool breath_chord_on;
bool breath_chord_playing;
bool jig_time;

void print_kick_times(uint64_t current_time) {
  printf("kick times index=%d (@%lld):\n", kick_times_index, current_time);
  for (int i = 0; i < KICK_TIMES_LENGTH; i++) {
    uint64_t kick_time = kick_times[(KICK_TIMES_LENGTH + kick_times_index - i) %
                                   KICK_TIMES_LENGTH];
    printf("  %llu   %llu\n", kick_time, current_time - kick_time);
  }
}

void voices_reset() {
  jawharp_on = false;
  hammond_on = false;
  organ_low_on = false;
  organ_flex_on = false;
  sine_pad_on = false;
  sweep_pad_on = false;
  overdriven_rhodes_on = false;
  rhodes_on = false;
  tbd_a_on = false;
  tbd_b_on = false;
  auto_hihat_mode = 0;
  manual_kick_on = false;
  manual_tss_on = false;
  drum_kit_sound = 0;
  atmospheric_drone = false;
  for (int i = 0; i < MIDI_MAX; i++) {
    atmospheric_drone_notes[i] = false;
    piano_notes[i] = false;
    breath_chord_notes[i] = false;
  }
  arpeggiator_on = false;
  arp_downbeat = true;
  arp_upbeat_high = false;
  arp_doubled = false;
  current_arp_voice = 0;
  current_jawharp_voice = 0;
  drum_breath_on = false;

  current_arpeggiator_note = -1;

  root_note = 26;  // D @ 37Hz

  air_locked = false;
  locked_air = 0;

  musical_mode = MODE_MAJOR;
  whistle_anchor_note = 60; // this is arbitrary

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

  state = STATE_DEFAULT;

  for (int i = 0; i < MAX_SCHEDULED_NOTES; i++) {
    scheduled_notes[i].ts = 0;
    // Any ScheduledNote with ts = 0 is considered unset.
  }

  current_beat_ns = 0;
  last_downbeat_ns = 0;

  breath_chord_on = false;
  breath_chord_playing = false;

  jig_time = false;
}



struct ScheduledNote* allocate_scheduled_note() {
  for (int i = 0; i < MAX_SCHEDULED_NOTES; i++) {
    if (scheduled_notes[i].ts == 0) {
      printf("chose %d\n", i);
      return &(scheduled_notes[i]);
    }
  }
  // No notes available, just pick one.
  printf("out of notes\n");
  return &(scheduled_notes[0]);
}

void schedule_note(uint64_t wait, uint64_t length, int noteNo, int velocity, int endpoint) {
  uint64_t current_time = now();
  struct ScheduledNote* on = allocate_scheduled_note();
  on->ts = current_time + wait;
  on->actionType = MIDI_ON;
  struct ScheduledNote* off = allocate_scheduled_note();
  off->ts = on->ts + length;
  off->actionType = MIDI_OFF;

  on->noteNo = off->noteNo = noteNo;
  on->velocity = off->velocity = velocity;
  on->endpoint = off->endpoint = endpoint;

  printf("scheduled %llu %llu %llu  %d %d %d %d %d\n",
         on->ts, off->ts, length,  on->actionType, off->actionType, on->noteNo, on->velocity, on->endpoint);
}

//  The flex organ follows organ_flex_breath and organ_flex_base.
//  organ_flex_min follows air, organ_flex_breath follows breath.
int organ_flex_base = 0;
int organ_flex_breath = 0;
int last_organ_flex_val = 0;
int organ_flex_val() {
  return (organ_flex_base * 0.5) + (organ_flex_breath * 0.5);
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

void jawharp_off() {
  if (current_note[ENDPOINT_JAWHARP] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = -1;
  }
}

void atmospheric_drone_off() {
  for (int i = 0 ; i < MIDI_MAX; i++) {
    if (atmospheric_drone_notes[i]) {
      atmospheric_drone_notes[i] = false;
      send_midi(MIDI_OFF, i, 0, ENDPOINT_SINE_PAD);
      send_midi(MIDI_OFF, i, 0, ENDPOINT_SWEEP_PAD);
    }
  }
  current_note[ENDPOINT_SINE_PAD] = -1;
}

void breath_chord_off() {
  for (int i = 0 ; i < MIDI_MAX; i++) {
    if (breath_chord_notes[i]) {
      breath_chord_notes[i] = false;
      send_midi(MIDI_OFF, i, 0, ENDPOINT_HAMMOND);
    }
  }
}

void atmospheric_drone_note_on(int note) {
  send_midi(MIDI_ON, note, MIDI_MAX, ENDPOINT_SINE_PAD);
  send_midi(MIDI_ON, note, MIDI_MAX, ENDPOINT_SWEEP_PAD);
  atmospheric_drone_notes[note] = true;
}

void breath_chord_note_on(int note) {
  send_midi(MIDI_ON, note, MIDI_MAX, ENDPOINT_HAMMOND);
  breath_chord_notes[note] = true;
}

bool downbeat(int subbeat) {
  return subbeat % 72 == 0;
}
bool preup(int subbeat) {
  return subbeat == (jig_time ? (72/3-3) : (72/4-1));
}
bool upbeat(int subbeat) {
  return subbeat == (jig_time ? (2*72/3-3) : (72/2-2));
}
bool predown(int subbeat) {
  if (jig_time) return false;  // This beat doesn't happen in jig time.
  return subbeat == (3*72/4-1);
}

void arpeggiate_bass(int subbeat) {
  if (!arpeggiator_on) return;

  int note_out = active_note();
  int selected_note = note_out;
  bool send_note = false;

  if (downbeat(subbeat)) {
    send_note = arp_downbeat;
  } else if (upbeat(subbeat)) {
    send_note = true;
    if (arp_upbeat_high) {
      selected_note += 12;
    }
  } else if (preup(subbeat) && !jig_time) {
    send_note = arp_downbeat && arp_doubled;
  } else if (predown(subbeat) || preup(subbeat)) {
    send_note = arp_doubled;
    if (arp_upbeat_high) {
      selected_note += 12;
    }
  }

  bool end_note = send_note;
  if (end_note && current_arpeggiator_note != -1) {
    //printf("footbass end note %d\n", current_arpeggiator_note);
    send_midi(MIDI_OFF, current_arpeggiator_note, 0, ENDPOINT_FOOTBASS);
    current_arpeggiator_note = -1;
  }

  if (send_note) {
    if (selected_note != -1) {
      send_midi(MIDI_ON, selected_note, 90, ENDPOINT_FOOTBASS);
      current_arpeggiator_note = selected_note;
      //printf("footbass start note %d\n", current_arpeggiator_note);
    }
  }
}

void arpeggiate(int subbeat) {
  arpeggiate_bass(subbeat);
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

void estimate_tempo(uint64_t current_time, int note_in) {
  //print_kick_times(current_time);

  current_beat_ns = 0;

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

  for (float candidate_bpm = 80;
       candidate_bpm <= 160;
       candidate_bpm += 0.25) {

    uint64_t whole_note_ns = 60L * NS_PER_SEC / candidate_bpm;
    uint64_t candidate_error = 0;

    // Look for a kick or snare on every past downbeat, but allow
    // kicks preceeding a snare to be exactly half a beat early.
    for (int i = 0; i < n_downbeats_to_consider; i++) {
      uint64_t target = current_time - (i+1)*whole_note_ns;
      uint64_t error =
        min(best_match_hit(target, kick_times, KICK_TIMES_LENGTH),
            best_match_hit(target, snare_times, SNARE_TIMES_LENGTH));

      /*
      bool early_kick_allowed =
        i % 2 == ((note_in == MIDI_DRUM_IN_KICK) ? 1 : 0);
      if (early_kick_allowed) {
        uint64_t early_kick_target = target - (whole_note_ns/2);
        uint64_t early_kick_error = best_match_hit(early_kick_target, kick_times, KICK_TIMES_LENGTH);
        error = min(error, early_kick_error);
      }

      */
      candidate_error += error;
    }

    if (candidate_error < best_error) {
      best_error = candidate_error;
      best_bpm = candidate_bpm;
    }
  }

  if (best_bpm < 0) {
    printf("best_bpm = %.2f : not expected\n", best_bpm);
    return;
  }

  uint64_t whole_beat = NS_PER_SEC * 60 / best_bpm;

  // Allow error of up to 1/32 note on each of the downbeats.
  uint64_t max_allowed_error = (whole_beat * n_downbeats_to_consider) / 32;

  bool acceptable_error = best_error < max_allowed_error;

  if (false) {
    printf("%c BPM: %.0f (%lld) (err: %llu / %llu, frac: %.2f%%)\n",
           acceptable_error ? ' ' : '!',
           best_bpm,
           whole_beat,
           best_error,
           max_allowed_error,
           100 * (float)best_error / (float)max_allowed_error);
  }

  if (acceptable_error) {
    current_beat_ns = whole_beat;

    arpeggiate(0);
    last_downbeat_ns = current_time;

    next_ns[0] = current_time;
    for (int i = 1; i < N_SUBBEATS; i++) {
      next_ns[i] = next_ns[i-1] + (whole_beat)/72;
    }
  }
}

void count_drum_hit(int note_in) {
  uint64_t current_time = now();
  if (note_in == MIDI_DRUM_IN_KICK) {
    //printf("saving kick @ %llu\n", current_time);
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

void trigger_breath_chord(int note_out) {
  breath_chord_off();
  current_note[ENDPOINT_HAMMOND] = note_out;
  // voice each chord with the two highest options
  int tonic = note_out;
  while (tonic + 12 <= MAX_HAMMOND_NOTE) tonic += 12;
  breath_chord_note_on(tonic);

  int fifth = note_out + 7;
  while (fifth + 12 <= MAX_HAMMOND_NOTE) fifth += 12;
  breath_chord_note_on(fifth);
}

void update_bass() {
  int note_out = active_note();

  if (atmospheric_drone && current_note[ENDPOINT_SINE_PAD] != note_out) {
    atmospheric_drone_off();
    current_note[ENDPOINT_SINE_PAD] = note_out;

    atmospheric_drone_note_on(note_out);
    atmospheric_drone_note_on(note_out + 12);
    atmospheric_drone_note_on(note_out + 12 + 7);
  }

  if (breath_chord_playing && current_note[ENDPOINT_HAMMOND] != note_out) {
    trigger_breath_chord(note_out);
  }

  if (breath < 3) return;

  if (jawharp_on && current_note[ENDPOINT_JAWHARP] != note_out) {
    jawharp_off();
    send_midi(MIDI_ON, note_out, MIDI_MAX, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = note_out;
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

void endpoint_notes_off(int endpoint) {
  // send an explicit all notes off command
  send_midi(MIDI_CC, 123, 0, endpoint);
}

void all_notes_off() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    endpoint_notes_off(endpoint);
  }
}

int to_root(int note_out) {
  int offset = 4;
  return (note_out - offset) % 12 + 24 + offset;
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
        update_bass();
      }
    }
  }

  //if (current_beat_ns != 0 && mode == MIDI_ON) {
  //  schedule_note(current_beat_ns, current_beat_ns / 2, note_in, MIDI_MAX, ENDPOINT_HAMMOND);
  //}

  if (hammond_on && !is_bass) {
    send_midi(mode, note_in, MIDI_MAX, ENDPOINT_HAMMOND);
  }
  if (organ_low_on && is_bass) {
    send_midi(mode, note_in, MIDI_MAX, ENDPOINT_ORGAN_LOW);
  }
  if (organ_flex_on) {
    send_midi(mode, note_in, MIDI_MAX, ENDPOINT_ORGAN_FLEX);
  }
  if (sine_pad_on) {
    send_midi(mode, note_in, MIDI_MAX, ENDPOINT_SINE_PAD);
  }
  if (sweep_pad_on) {
    send_midi(mode, note_in, MIDI_MAX, ENDPOINT_SWEEP_PAD);
  }
  if (overdriven_rhodes_on) {
    send_midi(mode, note_in, val, ENDPOINT_OVERDRIVEN_RHODES);
  }
  if (rhodes_on) {
    send_midi(mode, note_in, val, ENDPOINT_RHODES);
  }
}

void full_reset() {
  voices_reset();
  all_notes_off();
}

void change_button_endpoint(int endpoint) {
  if (!piano_on && endpoint != ENDPOINT_JAWHARP) {
    jawharp_on = false;
    jawharp_off();
  }
  button_endpoint = endpoint;
}

void air_lock() {
  air_locked = !air_locked;
  locked_air = air;
}

void handle_control(unsigned int note_in) {
  switch (note_in) {

  case FULL_RESET:
    full_reset();
    return;

  case AIR_LOCK:
    air_lock();
    return;

  case TOGGLE_JAWHARP:
    endpoint_notes_off(ENDPOINT_JAWHARP);
    if (piano_on) {
      jawharp_on = !jawharp_on;
    } else {
      jawharp_on = true;
      change_button_endpoint(ENDPOINT_JAWHARP);
    }

    if (jawharp_on) {
      update_bass();
    } else {
      jawharp_off();
    }
    return;

  case TOGGLE_BREATH_CHORD:
    if (breath_chord_on) {
      breath_chord_on = false;
      breath_chord_off();
    } else {
      breath_chord_on = true;
      update_bass();
    }
    printf("bc %d\n", breath_chord_on);
    return;

  case TOGGLE_HAMMOND:
    endpoint_notes_off(ENDPOINT_HAMMOND);
    hammond_on = !hammond_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_HAMMOND);
    }
    return;

  case TOGGLE_ORGAN_FLEX:
    endpoint_notes_off(ENDPOINT_ORGAN_FLEX);
    organ_flex_on = !organ_flex_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_ORGAN_FLEX);
    }
    return;

  case TOGGLE_SINE_PAD:
    endpoint_notes_off(ENDPOINT_SINE_PAD);
    sine_pad_on = !sine_pad_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_SINE_PAD);
    }
    return;

  case TOGGLE_SWEEP_PAD:
    endpoint_notes_off(ENDPOINT_SWEEP_PAD);
    sweep_pad_on = !sweep_pad_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_SWEEP_PAD);
    }
    return;

  case TOGGLE_OVERDRIVEN_RHODES:
    endpoint_notes_off(ENDPOINT_OVERDRIVEN_RHODES);
    overdriven_rhodes_on = !overdriven_rhodes_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_OVERDRIVEN_RHODES);
    }
    return;

  case TOGGLE_RHODES:
    endpoint_notes_off(ENDPOINT_RHODES);
    rhodes_on = !rhodes_on;
    if (!piano_on) {
      change_button_endpoint(ENDPOINT_RHODES);
    }
    return;

  case TOGGLE_MANUAL_TSS:
    manual_tss_on = !manual_tss_on;
    return;

  case TOGGLE_MANUAL_KICK:
    manual_kick_on = !manual_kick_on;
    return;

  case TOGGLE_ATMOSPHERIC_DRONE:
    atmospheric_drone = !atmospheric_drone;
    if (atmospheric_drone) {
      send_midi(MIDI_CC, CC_11, 80, ENDPOINT_SWEEP_PAD);
      send_midi(MIDI_CC, CC_11, 80, ENDPOINT_SINE_PAD);
    } else {
      atmospheric_drone_off();
    }
    return;

  case TOGGLE_ARPEGGIATOR:
    arpeggiator_on = !arpeggiator_on;
    endpoint_notes_off(ENDPOINT_FOOTBASS);
    if (arpeggiator_on) {
      send_midi(MIDI_CC, CC_11, FOOTBASS_VOLUME, ENDPOINT_FOOTBASS);
    }
    return;

  case ROTATE_ARP_VOICE:
    rotate_arp_voice(&current_arp_voice);
    return;

  case ROTATE_JAWHARP_VOICE:
    rotate_jawharp_voice(&current_jawharp_voice);
    return;

  case TOGGLE_DRUM_BREATH:
    drum_breath_on = !drum_breath_on;
    return;

  case TOGGLE_ARP_DOWNBEAT:
    arp_downbeat = !arp_downbeat;
    return;

  case TOGGLE_ARP_UPBEAT_HIGH:
    arp_upbeat_high = !arp_upbeat_high;
    return;

  case TOGGLE_ARP_DOUBLED:
    arp_doubled = !arp_doubled;
    return;

  case TOGGLE_JIG_REEL:
    jig_time = !jig_time;
    return;

  }
}

void handle_keypad(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (mode != MIDI_ON) return;

  int mapped_note = -1;

  switch (note_in) {

  case CFG_RESET:
    mapped_note = FULL_RESET;
    break;

  case CFG_JAWHARP:
    mapped_note = TOGGLE_JAWHARP;
    break;

  case CFG_FLEX:
    mapped_note = TOGGLE_ORGAN_FLEX;
    break;

  case CFG_SINE_PAD:
    mapped_note = TOGGLE_SINE_PAD;
    break;

  case CFG_ARP_VOICE:
    mapped_note = ROTATE_ARP_VOICE;
    break;

  case CFG_JAWHARP_VOICE:
    mapped_note = ROTATE_JAWHARP_VOICE;
    break;

  case CFG_ARPEGGIATOR:
    mapped_note = TOGGLE_ARPEGGIATOR;
    break;

  case CFG_DOWNBEAT:
    mapped_note = TOGGLE_ARP_DOWNBEAT;
    break;

  case CFG_UPBEAT_HIGH:
    mapped_note = TOGGLE_ARP_UPBEAT_HIGH;
    break;

  case CFG_DOUBLED:
    mapped_note = TOGGLE_ARP_DOUBLED;
    break;

  case CFG_JIG_REEL:
    mapped_note = TOGGLE_JIG_REEL;
    break;

  }

  if (mapped_note == -1) return;

  handle_control(mapped_note);
}

int remap(int val, int min, int max) {
  int range = max - min;
  return val * range / MIDI_MAX + min;
}

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  /*
  unsigned char note_out = mapping(note_in);
  int endpoint = note_in % N_ENDPOINTS;
  printf("endpoint: %d\n", endpoint);
  send_midi(mode, note_out, val, endpoint);
  */
  //handle_feet(mode, MIDI_DRUM_IN_KICK, 100);


}

void handle_feet(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (mode != MIDI_ON) {
    return;
  }

  update_bass();

  //printf("foot: %d %d\n", note_in, val);
  count_drum_hit(note_in);
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc != CC_BREATH && cc != CC_11) {
    printf("Unknown Control change %d\n", cc);
    return;
  }

  breath = val;
  send_midi(MIDI_CC, CC_MOD, breath_chord_on ? MIDI_MAX : val, ENDPOINT_HAMMOND);

  if (breath_chord_on) {
    if (!breath_chord_playing && breath > 80) {
      breath_chord_playing = true;
      update_bass();
    } else if (breath_chord_playing && breath <= 80) {
      breath_chord_playing = false;
      breath_chord_off();
      current_note[ENDPOINT_HAMMOND] = -1;
    }
  }

  if (!piano_on && button_endpoint == ENDPOINT_OVERDRIVEN_RHODES) {
    send_midi(MIDI_CC, CC_07, MIDI_MAX, ENDPOINT_RHODES);
  }

  // pass other control change to all synths that care about it:
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_ORGAN_FLEX) {
      continue;
    }
    int use_val = normalize(val);

    if (endpoint == ENDPOINT_JAWHARP) {
      if (breath < 10 &&
          current_note[ENDPOINT_JAWHARP] != -1) {
        send_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0,
                  ENDPOINT_JAWHARP);
        current_note[ENDPOINT_JAWHARP] = -1;
      } else if (breath > 20) {
        update_bass();
      }
    }

    if (endpoint == ENDPOINT_ORGAN_FLEX) {
      organ_flex_breath = use_val;
      use_val = organ_flex_val();
      last_organ_flex_val = use_val;
    }
    send_midi(MIDI_CC, CC_11, use_val, endpoint);
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
  if (mode == MIDI_ON && note_in == BUTTON_ADJUST_3) {
    state = STATE_ADJUST_3;
  } else if (mode == MIDI_ON && note_in == BUTTON_ADJUST_24) {
    state = STATE_ADJUST_24;
  } else if (note_in <= CONTROL_MAX) {
    if (mode == MIDI_ON) {
      handle_control(note_in);
    }
  } else {
    handle_button(mode, note_in, val);
  }
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
  air += (breath * breath_gain);
  if (air > max_air) {
    air = max_air;
  }
  // It's ok that air > MIDI_MAX (because max_air > MIDI_MAX) because
  // everything that uses this will only allow a max of MIDI_MAX.
}

int last_air_val = 0;
void forward_air() {
  int val = air;

  organ_flex_base = val;
  int organ_flex_value = organ_flex_val();

  if (piano_on) {
    if (air_locked) {
      val = locked_air;
    }
  } else {
    val = organ_flex_value;
  }

  if (val > MIDI_MAX) {
    val = MIDI_MAX;
  }

  if (val != last_air_val) {
    if (!arpeggiator_on && ENDPOINT_ORGAN_LOW == ENDPOINT_FOOTBASS) {
      send_midi(MIDI_CC, CC_11, val, ENDPOINT_ORGAN_LOW);
    }
    send_midi(MIDI_CC, CC_07, breath_chord_on ? MIDI_MAX : val, ENDPOINT_HAMMOND);
    if (!atmospheric_drone) {
      send_midi(MIDI_CC, CC_11, val, ENDPOINT_SINE_PAD);
      send_midi(MIDI_CC, CC_11, val, ENDPOINT_SWEEP_PAD);
    }
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_OVERDRIVEN_RHODES);
    if (piano_on) {
      send_midi(MIDI_CC, CC_07, val, ENDPOINT_RHODES);
    } else {
      // handled in handle_cc
    }
    last_air_val = val;
  }
  if (organ_flex_value != last_organ_flex_val) {
    send_midi(MIDI_CC, CC_11, organ_flex_value, ENDPOINT_ORGAN_FLEX);

    last_organ_flex_val = organ_flex_value;
  }
}

void trigger_scheduled_notes() {
  uint64_t current_time = now();
  for (int i = 0; i < MAX_SCHEDULED_NOTES; i++) {
    if (scheduled_notes[i].ts != 0 && scheduled_notes[i].ts < current_time) {
      printf("trigger %d %d %d %d\n",
             scheduled_notes[i].actionType,
             scheduled_notes[i].noteNo,
             scheduled_notes[i].velocity,
             scheduled_notes[i].endpoint);
      send_midi(scheduled_notes[i].actionType,
                scheduled_notes[i].noteNo,
                scheduled_notes[i].velocity,
                scheduled_notes[i].endpoint);
      scheduled_notes[i].ts = 0;
    }
  }
}

void trigger_subbeats() {
  uint64_t current_time = now();

  for (int i = 1 /* 0 is triggered by kick directly */; i < N_SUBBEATS; i++) {
    if (next_ns[i] > 0 && current_time > next_ns[i]) {
      arpeggiate(i);
      next_ns[i] = 0;
    }
  }
}

uint64_t tick_n = 0;
uint64_t subtick_n = 0;
void jml_tick() {
  // Called every TICK_MS
  update_air();
  forward_air();
  trigger_subbeats();
  //trigger_scheduled_notes();

  if (false) {
    if (++tick_n % 450 == 0) {
      handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);

      if (++subtick_n % 2 == 0) {
        root_note = to_root(root_note + 1);
        update_bass();
      }
    }
  }
}

#endif
