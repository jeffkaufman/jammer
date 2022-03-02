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
#define MIDI_DRUM_IN_KICK 38
#define MIDI_DRUM_IN_CRASH 59
#define MIDI_DRUM_IN_HIHAT 51

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
  bool jawharp_on;
  bool jawharp_full_on;
  bool organ_low_on;
  bool organ_low_piano_vel;
  bool organ_hi_on;
  bool organ_hi_piano_vel;
  bool overlay_on;
  bool overlay_piano_vel;
  bool flex_on;

  bool fb_on;
  bool fb_downbeat;
  bool fb_upbeat;
  bool fb_upbeat_high;
  bool fb_doubled;
  int current_fb_note;
  int current_fb_fifth;
  int current_fb_len;
  bool fb_short;
  bool fb_shorter;
  bool fb_octave_up;
  bool fb_pre_unique;
  bool fb_chord;

  bool arp_on;
  bool arp_downbeat;
  bool arp_upbeat;
  bool arp_upbeat_high;
  bool arp_doubled;
  int current_arp_note;
  int current_arp_fifth;
  int current_arp_len;
  bool arp_short;
  bool arp_shorter;
  bool arp_octave_up;
  bool arp_pre_unique;
  bool arp_chord;

  bool air_locked;
  double locked_air;
  bool fb_follows_air;

  int volume_deltas[MIDI_MAX];
  int manual_volumes[MIDI_MAX];
  int voices[N_ENDPOINTS];
  bool pans[N_ENDPOINTS];
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
bool jig_time;

void print_kick_times(uint64_t current_time) {
  printf("kick times index=%d (@%lld):\n", kick_times_index, current_time);
  for (int i = 0; i < KICK_TIMES_LENGTH; i++) {
    uint64_t kick_time = kick_times[(KICK_TIMES_LENGTH + kick_times_index - i) %
                                   KICK_TIMES_LENGTH];
    printf("  %llu   %llu\n", kick_time, current_time - kick_time);
  }
}

void update_bass();

int to_root(int note_out) {
  int offset = 4;
  return (note_out - offset) % 12 + 24 + offset;
}

// Given a note relative to root, convert it into a note relative to fifth.
int to_fifth(int note_out) {
  return fifth_note + (note_out - root_note);
}

void psend_midi(int action, int note, int velocity, int endpoint) {
  if ((action == MIDI_ON || action == MIDI_OFF) &&
      c->voices[endpoint] == 16) {
    note += 12;  // Drawbar organ should be up an octave
  }
  send_midi(action, note, velocity, endpoint);
}

void endpoint_notes_off(int endpoint) {
  // send an explicit all notes off command
  psend_midi(MIDI_CC, 123, 0, endpoint);
}

void all_notes_off() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    endpoint_notes_off(endpoint);
  }
}

void reload_voice_setting(struct Configuration* c) {
  int endpoint = c->selected_endpoint;
  int voice = c->voices[endpoint];
  int volume_delta = c->volume_deltas[voice];
  int manual_volume = c->manual_volumes[voice];
  bool pan = c->pans[endpoint];
  select_endpoint_voice(endpoint, voice, volume_delta, manual_volume, pan);
}

void select_voice(struct Configuration* c, int voice) {
  endpoint_notes_off(c->selected_endpoint);
  c->voices[c->selected_endpoint] = voice;
  reload_voice_setting(c);
  if (c->selected_endpoint == ENDPOINT_FOOTBASS ||
      c->selected_endpoint == ENDPOINT_ARP ||
      c->selected_endpoint == ENDPOINT_JAWHARP) {
    update_bass();
  }
}

void clear_jawharp() {
  select_voice(c, 67);
  c->jawharp_on = false;
  c->jawharp_full_on = false;
}

void clear_footbass() {
  select_voice(c, 39);

  c->fb_on = false;
  c->fb_downbeat = true;
  c->fb_upbeat = true;
  c->fb_upbeat_high = false;
  c->fb_doubled = false;
  c->current_fb_note = -1;
  c->current_fb_fifth = -1;
  c->current_fb_len = -1;

  c->fb_follows_air = false;

  c->fb_octave_up = false;
  c->fb_pre_unique = false;
  c->fb_chord = false;

  c->fb_short = false;
  c->fb_shorter = false;
}

void clear_arp() {
  select_voice(c, 38);

  c->arp_on = false;
  c->arp_downbeat = true;
  c->arp_upbeat = true;
  c->arp_upbeat_high = true;
  c->arp_doubled = true;
  c->current_arp_note = -1;
  c->current_arp_fifth = -1;
  c->current_arp_len = -1;

  c->arp_octave_up = false;
  c->arp_pre_unique = false;
  c->arp_chord = false;

  c->arp_short = false;
  c->arp_shorter = false;
}

void clear_flex() {
  select_voice(c, 81);

  c->flex_on = false;
}

void clear_low() {
  select_voice(c, 39);

  c->organ_low_on = false;
  c->organ_low_piano_vel = false;
}

void clear_high() {
  select_voice(c, 39);

  c->organ_hi_on = false;
  c->organ_hi_piano_vel = false;
}

void clear_overlay() {
  select_voice(c, 39);

  c->overlay_on = false;
  c->overlay_piano_vel = false;
}

void clear_endpoint() {
  switch (c->selected_endpoint) {
  case ENDPOINT_JAWHARP: clear_jawharp(); return;
  case ENDPOINT_FOOTBASS: clear_footbass(); return;
  case ENDPOINT_ARP: clear_arp(); return;
  case ENDPOINT_FLEX: clear_flex(); return;
  case ENDPOINT_LOW: clear_low(); return;
  case ENDPOINT_HI: clear_high(); return;
  case ENDPOINT_OVERLAY: clear_overlay(); return;
  }
  c->pans[c->selected_endpoint] = false;
}

void clear_configuration() {
  c->air_locked = false;
  c->locked_air = 0;

  for (int i = 0; i < MIDI_MAX; i++) {
    c->volume_deltas[i] = 0;
  }
  for (int i = 0; i < MIDI_MAX; i++) {
    c->manual_volumes[i] = -1;
  }
  for (int i = 0 ; i < N_ENDPOINTS; i++) {
    c->selected_endpoint = i;
    clear_endpoint();
  }
}

void clear_status() {
  for (int i = 0; i < MIDI_MAX; i++) {
    piano_notes[i] = false;
  }
  root_note = to_root(26);  // D @ 37Hz
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

  jig_time = false;
}

void voices_reset() {
  clear_configuration();
  clear_status();
}


//  The flex organ follows flex_breath and flex_base.
//  flex_breath follows breath.
int flex_base = 0;
int flex_breath = 0;
int last_flex_val = 1;
int flex_val() {
  return flex_breath;
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
    psend_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = -1;
  }
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

void select_note(int subbeat, bool octave_up, bool chord, bool send_downbeat,
                 bool send_upbeat, bool upbeat_high, bool pre_unique,
                 bool doubled, int* selected_note, bool* send_note) {
  if (octave_up) {
    *selected_note += 12;
  }

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

bool should_end_note(int current_len, bool is_short, bool is_shorter) {
  if (current_len != -1 && (is_short || is_shorter)) {
    int threshold = jig_time ? 24 : 18;  // kept if short only
    if (is_short && is_shorter) {
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


void arpeggiate_bass(int subbeat, uint64_t current_time, bool drone) {
  if (!c->fb_on) return;
  if (drone && c->current_fb_note == -1) return;

  if (c->current_fb_note != -1) {
    c->current_fb_len++;
  }

  int note_out = active_note();
  int selected_note = note_out;
  int fifth = to_fifth(selected_note);
  bool send_note = false;

  select_note(subbeat, c->fb_octave_up, c->fb_chord, c->fb_downbeat,
              c->fb_upbeat, c->fb_upbeat_high, c->fb_pre_unique,
              c->fb_doubled, &selected_note, &send_note);
  select_note(subbeat, c->fb_octave_up, c->fb_chord, c->fb_downbeat,
              c->fb_upbeat, c->fb_upbeat_high, c->fb_pre_unique,
              c->fb_doubled, &fifth, &send_note);

  bool end_note = send_note ||
    should_end_note(c->current_fb_len, c->fb_short, c->fb_shorter);

  if (end_note && c->current_fb_note != -1) {
    psend_midi(MIDI_OFF, c->current_fb_note, 0, ENDPOINT_FOOTBASS);
    if (c->current_fb_fifth != -1) {
      psend_midi(MIDI_OFF, c->current_fb_fifth, 0, ENDPOINT_FOOTBASS);
    }

    c->current_fb_note = -1;
    c->current_fb_fifth = -1;
    c->current_fb_len = -1;
  }

  if (send_note) {
    if (selected_note != -1) {
      c->current_fb_note = selected_note;
      c->current_fb_fifth = fifth;
      c->current_fb_len = 0;

      psend_midi(MIDI_ON,
                c->current_fb_note,
                90,
                ENDPOINT_FOOTBASS);

      if (c->fb_chord) {
        psend_midi(MIDI_ON,
                  c->current_fb_fifth,
                  90,
                  ENDPOINT_FOOTBASS);
      }
    }
  }
}

void arpeggiate_arp(int subbeat, uint64_t current_time, bool drone) {
  if (!c->arp_on) return;
  if (drone && c->current_fb_note == -1) return;

  if (c->current_arp_note != -1) {
    c->current_arp_len++;
  }

  int note_out = active_note();
  int selected_note = note_out;
  int fifth = to_fifth(selected_note);
  bool send_note = false;

  select_note(subbeat, c->arp_octave_up, c->arp_chord, c->arp_downbeat,
              c->arp_upbeat, c->arp_upbeat_high, c->arp_pre_unique,
              c->arp_doubled, &selected_note, &send_note);
  select_note(subbeat, c->arp_octave_up, c->arp_chord, c->arp_downbeat,
              c->arp_upbeat, c->arp_upbeat_high, c->arp_pre_unique,
              c->arp_doubled, &fifth, &send_note);

  bool end_note = send_note ||
    should_end_note(c->current_arp_len, c->arp_short, c->arp_shorter);

  if (end_note && c->current_arp_note != -1) {
    psend_midi(MIDI_OFF, c->current_arp_note, 0, ENDPOINT_ARP);
    if (c->current_arp_fifth != -1) {
      psend_midi(MIDI_OFF, c->current_arp_fifth, 0, ENDPOINT_ARP);
    }

    c->current_arp_note = -1;
    c->current_arp_fifth = -1;
    c->current_arp_len = -1;
  }

  if (send_note) {
    if (selected_note != -1) {
      c->current_arp_note = selected_note;
      c->current_arp_fifth = fifth;
      c->current_arp_len = 0;

      psend_midi(MIDI_ON,
                c->current_arp_note,
                90,
                ENDPOINT_ARP);

      if (c->arp_chord) {
        psend_midi(MIDI_ON,
                  c->current_arp_fifth,
                  90,
                  ENDPOINT_ARP);
      }

      //printf("footbass start note %d\n", current_fb_note);
    }
  }
}

void arpeggiate(int subbeat, uint64_t current_time, bool drone) {
  arpeggiate_bass(subbeat, current_time, drone);
  arpeggiate_arp(subbeat, current_time, drone);
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

    arpeggiate(0, current_time, /*drone=*/false);
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

void update_bass() {
  int note_out = active_note();

  uint64_t current_time = now();
  if (current_time - last_downbeat_ns > NS_PER_SEC &&
      note_out != last_update_bass_note) {
    arpeggiate(0, current_time, /*drone=*/true);
  }

  last_update_bass_note = note_out;

  if (breath < 3 && !c->jawharp_full_on) return;

  if (c->jawharp_on && current_note[ENDPOINT_JAWHARP] != note_out) {
    jawharp_off();
    psend_midi(MIDI_ON, note_out, MIDI_MAX, ENDPOINT_JAWHARP);
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
        update_bass();
      }
    }
  }

  if (c->flex_on) {
    psend_midi(mode, note_in, MIDI_MAX, ENDPOINT_FLEX);
  }
  if (c->organ_low_on && is_bass) {
    psend_midi(mode, note_in,
              c->organ_low_piano_vel ? val : 75,
              ENDPOINT_LOW);
  }
  if (c->organ_hi_on && !is_bass) {
    psend_midi(mode, note_in,
              c->organ_hi_piano_vel ? val : 75,
              ENDPOINT_HI);
  }
  if (c->overlay_on) {
    psend_midi(mode, note_in,
              c->overlay_piano_vel ? val : 75,
              ENDPOINT_OVERLAY);
  }
}

void full_reset() {
  voices_reset();
  all_notes_off();
}

void air_lock() {
  c->air_locked = !c->air_locked;
  c->locked_air = air;
}

void handle_keypad(unsigned int mode, unsigned char note_in, unsigned int val) {
  if (mode != MIDI_ON) return;

  printf("recv: %c\n", note_in);

  int selected_voice = c->voices[c->selected_endpoint];

  switch (note_in) {
  case DELETE:
    // Manual volume entry
    c->manual_volumes[selected_voice] = val;
    c->volume_deltas[selected_voice] = 0;
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
    c->volume_deltas[selected_voice] -= 5;
    reload_voice_setting(c);
    return;
  case '=': // +
    c->volume_deltas[selected_voice] += 5;
    reload_voice_setting(c);
    return;
  case '1':
    c->selected_endpoint = ENDPOINT_JAWHARP;
    return;
  case 'Q':
    c->selected_endpoint = ENDPOINT_JAWHARP;
    endpoint_notes_off(ENDPOINT_JAWHARP);
    c->jawharp_on = !c->jawharp_on;

    if (c->jawharp_on) {
      update_bass();
    } else {
      jawharp_off();
    }
    return;
  case '2':
    c->selected_endpoint = ENDPOINT_FOOTBASS;
    return;
  case 'W':
    c->selected_endpoint = ENDPOINT_FOOTBASS;
    endpoint_notes_off(ENDPOINT_FOOTBASS);
    c->fb_on = !c->fb_on;
    update_bass();
    return;
  case '3':
    c->selected_endpoint = ENDPOINT_ARP;
    return;
  case 'E':
    c->selected_endpoint = ENDPOINT_ARP;
    endpoint_notes_off(ENDPOINT_ARP);
    c->arp_on = !c->arp_on;
    update_bass();
    return;
  case '4':
    c->selected_endpoint = ENDPOINT_FLEX;
    return;
  case 'R':
    c->selected_endpoint = ENDPOINT_FLEX;
    endpoint_notes_off(ENDPOINT_FLEX);
    c->flex_on = !c->flex_on;
    return;
  case '5':
    c->selected_endpoint = ENDPOINT_LOW;
    return;
  case 'T':
    c->selected_endpoint = ENDPOINT_LOW;
    endpoint_notes_off(ENDPOINT_LOW);
    c->organ_low_on = !c->organ_low_on;
    return;
  case '6':
    c->selected_endpoint = ENDPOINT_HI;
    return;
  case 'Y':
    c->selected_endpoint = ENDPOINT_HI;
    endpoint_notes_off(ENDPOINT_HI);
    c->organ_hi_on = !c->organ_hi_on;
    return;
  case '7':
    c->selected_endpoint = ENDPOINT_OVERLAY;
    return;
  case 'U':
    c->selected_endpoint = ENDPOINT_OVERLAY;
    endpoint_notes_off(ENDPOINT_OVERLAY);
    c->overlay_on = !c->overlay_on;
    return;

  case 'I':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_downbeat = !c->arp_downbeat;
    } else {
      c->fb_downbeat = !c->fb_downbeat;
    }
    return;
  case 'O':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_upbeat = !c->arp_upbeat;
    } else {
      c->fb_upbeat = !c->fb_upbeat;
    }
    return;
  case 'P':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_doubled = !c->arp_doubled;
    } else {
      c->fb_doubled = !c->fb_doubled;
    }
    return;
  case '[':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_pre_unique = !c->arp_pre_unique;
    } else {
      c->fb_pre_unique = !c->fb_pre_unique;
    }
    return;
  case ']':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_octave_up = !c->arp_octave_up;
    } else {
      c->fb_octave_up = !c->fb_octave_up;
    }
    return;
  case '\\':
    jig_time = !jig_time;
    return;
  case 'L':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_upbeat_high = !c->arp_upbeat_high;
    } else {
      c->fb_upbeat_high = !c->fb_upbeat_high;
    }
    return;
  case ';':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_short = !c->arp_short;
    } else {
      c->fb_short = !c->fb_short;
    }
    return;
  case '\'':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_shorter = !c->arp_shorter;
    } else {
      c->fb_shorter = !c->fb_shorter;
    }
    return;
  case ',':
    if (c->selected_endpoint == ENDPOINT_ARP) {
      c->arp_chord = !c->arp_chord;
      endpoint_notes_off(ENDPOINT_ARP);
    } else {
      c->fb_chord = !c->fb_chord;
      endpoint_notes_off(ENDPOINT_FOOTBASS);
    }
    return;
  case '.':
    if (c->selected_endpoint == ENDPOINT_LOW) {
      c->organ_low_piano_vel = !c->organ_low_piano_vel;
    } else if (c->selected_endpoint == ENDPOINT_HI) {
      c->organ_hi_piano_vel = !c->organ_hi_piano_vel;
    } else if (c->selected_endpoint == ENDPOINT_OVERLAY) {
      c->overlay_piano_vel = !c->overlay_piano_vel;
    }
    return;
  case '/':
    c->jawharp_full_on = !c->jawharp_full_on;
    psend_midi(MIDI_CC, CC_11,
              c->jawharp_full_on ? MIDI_MAX : 0, ENDPOINT_JAWHARP);
    update_bass();
    return;

  case 'A': select_voice(c, 39); return;
  case 'S': select_voice(c, 38); return;
  case 'D': select_voice(c, 84); return;
  case 'F': select_voice(c, 35); return;
  case 'G': select_voice(c, 26); return;
  case 'H': select_voice(c, 28); return;
  case 'J': select_voice(c, 75); return;
  case 'K': select_voice(c, 80); return;
  case 'Z': select_voice(c,  4); return;
  case 'X': select_voice(c, 24); return;
  case 'C': select_voice(c, 85); return;
  case 'V': select_voice(c, 64); return;
  case 'B': select_voice(c, 66); return;
  case 'N': select_voice(c, 67); return;
  case 'M': select_voice(c, 81); return;
  case F3:  select_voice(c,  0); return;
  case F4:  select_voice(c,  5); return;
  case F5:  select_voice(c, 16); return;
  case F6:  select_voice(c, 18); return;
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

  //printf("foot: %d %d\n", note_in, val);
  count_drum_hit(note_in);
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc != CC_BREATH && cc != CC_11) {
    printf("Unknown Control change %d\n", cc);
    return;
  }

  breath = val;

  // pass other control change to all synths that care about it:
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_FLEX) {
      continue;
    }
    int use_val = normalize(val);

    if (endpoint == ENDPOINT_JAWHARP) {
      if (breath < 10 &&
          current_note[ENDPOINT_JAWHARP] != -1) {
        psend_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0,
                  ENDPOINT_JAWHARP);
        current_note[ENDPOINT_JAWHARP] = -1;
      } else if (breath > 20) {
        update_bass();
      }
    }

    if (endpoint == ENDPOINT_FLEX) {
      flex_breath = use_val;
      use_val = flex_val();
      last_flex_val = use_val;
    }
    if (endpoint != ENDPOINT_JAWHARP || !c->jawharp_full_on) {
      psend_midi(MIDI_CC, CC_11, use_val, endpoint);
    }
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
  air += (breath * breath_gain * (c->fb_follows_air ? 0.25 : 1));
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

  if (c->air_locked) {
    val = c->locked_air;
  }

  if (val > MIDI_MAX) {
    val = MIDI_MAX;
  }

  if (val != last_air_val) {
    // add option to make instruments follow breath.  If so, they would go here
    // with psend_midi(MIDI_CC, CC_11, val, endpoint);
    last_air_val = val;
  }
  if (flex_value != last_flex_val) {
    psend_midi(MIDI_CC, CC_11, flex_value, ENDPOINT_FLEX);
    last_flex_val = flex_value;
  }
}

void trigger_subbeats() {
  uint64_t current_time = now();

  for (int i = 1 /* 0 is triggered by kick directly */; i < N_SUBBEATS; i++) {
    if (next_ns[i] > 0 && current_time > next_ns[i]) {
      arpeggiate(i, current_time, /*drone=*/false);
      next_ns[i] = 0;
    }
  }
}

uint64_t tick_n = 0;
uint64_t subtick_n = 0;
void jml_tick() {

  // play startup chime
  if (tick_n == 0) {
    psend_midi(MIDI_ON, 28, 100, ENDPOINT_LOW);
  } else if (tick_n == 500) {
    psend_midi(MIDI_OFF, 28, 100, ENDPOINT_LOW);
    psend_midi(MIDI_ON, 33, 100, ENDPOINT_LOW);
  } else if (tick_n == 2000) {
    psend_midi(MIDI_OFF, 33, 100, ENDPOINT_LOW);
  }

  // Called every TICK_MS
  update_air();
  forward_air();
  trigger_subbeats();

  if (++tick_n % 450 == 0) {
#ifdef FAKE_FEET
    handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
#endif

#ifdef FAKE_CHANGE_PITCH
    if (++subtick_n % 2 == 0) {
      root_note = to_root(root_note + 1);
      update_bass();
    }
#endif
  }
}

#endif
