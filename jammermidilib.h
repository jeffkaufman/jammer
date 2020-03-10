#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2


// #define PIANO_MIDI_NAME "Roland Digital Piano"
#define PIANO_MIDI_NAME "USB MIDI Interface"

/*
 Controls:

   rst  odr  rg   bt1  pd1  jaw  s/t
arl  rho  ham  atd  pd2  flx   HH
   arp  app  ldp  rrm  tdt  tdk  fcf
                               brc
   ...

   maj min mix               b3  b24
 */
#define FULL_RESET                  7
#define AIR_LOCK                 14

#define TOGGLE_OVERDRIVEN_RHODES     6
#define TOGGLE_RHODES            13

#define TOGGLE_REEL_JIG             5
#define TOGGLE_HAMMOND           12

#define TOGGLE_BASS_TROMBONE         4
#define TOGGLE_ATMOSPHERIC_DRONE 11

#define TOGGLE_SINE_PAD              3
#define TOGGLE_SWEEP_PAD         10

#define TOGGLE_JAWHARP               2
#define TOGGLE_ORGAN_FLEX         9

#define SELECT_SAX_TROMBONE          1
#define ROTATE_AUTO_HIHAT         8

#define TOGGLE_ARPEGGIATOR          21
#define ROTATE_ARPEGGIATOR_PATTERN  20
#define TOGGLE_LISTEN_DRUM_PEDAL    19
#define ROTATE_RHYTHM_MODE          18
#define ROTATE_DRUM_PEDAL_TSS       17
#define TOGGLE_DRUM_PEDAL_KICK      16
#define TOGGLE_FC_FEET              15

#define TOGGLE_BREATH_CHORD         22

#define CONTROL_MAX TOGGLE_BREATH_CHORD

#define BUTTON_ADJUST_3 93
#define BUTTON_ADJUST_24 92

#define BUTTON_MAJOR 98
#define BUTTON_MIXO 97
#define BUTTON_MINOR 96

#define N_ARPEGGIATOR_PATTERNS 5
#define N_RHYTHM_MODES 2
#define N_AUTO_HIHAT_MODES 9

/* endpoints */
#define ENDPOINT_SAX 0
#define ENDPOINT_TROMBONE 1
#define ENDPOINT_JAWHARP 2
#define ENDPOINT_BASS_SAX 3
#define ENDPOINT_BASS_TROMBONE 4
#define ENDPOINT_HAMMOND 5
#define ENDPOINT_ORGAN_LOW 6
#define ENDPOINT_ORGAN_FLEX 7
#define ENDPOINT_SINE_PAD 8
#define ENDPOINT_OVERDRIVEN_RHODES 9
#define ENDPOINT_RHODES 10
#define ENDPOINT_SWEEP_PAD 15
#define ENDPOINT_DRUM 16
// These three play samples from https://www.jefftk.com/Foot-Percussion--Nightingale--La-Belle-Rose
#define ENDPOINT_FOOT_1 17 // 1-b
#define ENDPOINT_FOOT_3 18 // 3-a
#define ENDPOINT_FOOT_4 19 // 4-a
// These two are from https://www.jefftk.com/p/what-noise-does-a-tambourine-make
#define ENDPOINT_TAMBOURINE_FREE 20
#define ENDPOINT_TAMBOURINE_STOPPED 21
#define N_ENDPOINTS (ENDPOINT_TAMBOURINE_STOPPED+1)

// aliases
#define ENDPOINT_FOOTBASS ENDPOINT_ORGAN_LOW

#define STATE_DEFAULT 0
#define STATE_ADJUST_3 1
#define STATE_ADJUST_24 2

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0
#define MIDI_PITCH_BEND 0xe0

#define CC_MOD 0x01
#define CC_BREATH 0x02
#define CC_07 0x07
#define CC_11 0x0b

#define MIN_TROMBONE 30  // need to be blowing this hard to make the trombone make noise
#define MAX_TROMBONE 100  // cap midi breath to the trombone at this, or it gets blatty

#define FOOTBASS_VOLUME 120

// gcmidi sends on CC 20 through 29
#define GCMIDI_MIN 20
#define GCMIDI_MAX 29

#define CC_ROLL 30
#define CC_PITCH 31

#define MIDI_DRUM_KICK  35
#define MIDI_DRUM_KICK_A  106
#define MIDI_DRUM_KICK_B  36
#define MIDI_DRUM_RIM 37
#define MIDI_DRUM_SNARE 38
#define MIDI_DRUM_HIHAT 46
#define MIDI_DRUM_HIHAT_CLOSED 42
#define MIDI_DRUM_TAMBOURINE 54

#define MIDI_DRUM_PEDAL_1 46
#define MIDI_DRUM_PEDAL_2 38
#define MIDI_DRUM_PEDAL_3 51
#define MIDI_DRUM_PEDAL_4 59

#define MIDI_MAX 127

#define TICK_MS 1  // try to tick every N milliseconds

#define MODE_MAJOR 0
#define MODE_MIXO 1
#define MODE_MINOR 2

#define KICK_TIMES_LENGTH 12
#define SNARE_TIMES_LENGTH 12

#define NS_PER_SEC 1000000000L

#define N_SUBBEATS 5

#define MAX_SCHEDULED_NOTES 64

#define MAX_HAMMOND_NOTE 96  // the hammond can't play higher than this note

/**
 * Drum settings
 *  - regular: left is kick, right are tss, computer interprets left for kick, sound is from drumkit brain
 *  - chordal: right is kick and chord, left is snare or bass, computer interprets right for kick, sound is from computer
 *  - hybrid: left is kick, right are tss, computer interprets left for kick, left sound is from computer right sound is from drumkit brain
 *
 * These are implemented as:
 *
 * - kit 62 or 64: regular
 * - kit 63 + listen_drum_pedal: chordal
 * - kit 61: hybrid
 */

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')


void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void attempt(OSStatus result, char* errmsg) {
  if (result != noErr) {
    die(errmsg);
  }
}

uint64_t now() {
  return clock_gettime_nsec_np(CLOCK_MONOTONIC);
}

struct ScheduledNote {
  uint64_t ts;
  char actionType;
  int noteNo;
  int velocity;
  int endpoint;
};

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;
MIDIPortRef midiport_breath_controller;
MIDIPortRef midiport_game_controller;
MIDIPortRef midiport_feet_controller;
MIDIPortRef midiport_whistle;
MIDIPortRef midiport_piano;

bool piano_on = false;  // Initialized based on availablity of piano.


/* Anything mentioned here should be initialized in voices_reset */
bool jawharp_on;
bool bass_trombone_on;
bool vbass_trombone_on;
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
bool sax_on;
bool listen_drum_pedal;
int most_recent_drum_pedal;
int current_drum_pedal_kick_note;
int current_drum_pedal_tss_note;
bool atmospheric_drone;
bool atmospheric_drone_notes[MIDI_MAX];
bool piano_notes[MIDI_MAX];
bool breath_chord_notes[MIDI_MAX];
bool arpeggiator_on;
int current_arpeggiator_pattern;
int current_arpeggiator_note;
int current_drum_note;
int button_endpoint;
int root_note;
bool air_locked;
double locked_air;
int musical_mode;
unsigned int whistle_anchor_note;
uint64_t last_hihat_ns;
uint64_t kick_times[KICK_TIMES_LENGTH];
int kick_times_index;
uint64_t snare_times[SNARE_TIMES_LENGTH];
int snare_times_index;
uint64_t next_ns[N_SUBBEATS];
uint64_t tambourine_next_ns[N_SUBBEATS];
bool fc_feet_on;
int rhythm_mode;
float beat_location_3;
float beat_location_24;
int state;
bool jig_time;
struct ScheduledNote scheduled_notes[MAX_SCHEDULED_NOTES];
uint64 current_beat_ns;
bool breath_chord_on;
bool breath_chord_playing;
bool is_minor_chord;  // only used when listen_drum_pedal=true
int current_drum_vel;

void voices_reset() {
  jawharp_on = false;
  bass_trombone_on = false;
  vbass_trombone_on = false;
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
  listen_drum_pedal = false;
  most_recent_drum_pedal = MIDI_DRUM_PEDAL_2;
  current_drum_pedal_kick_note = 0;
  current_drum_pedal_tss_note = 0;
  atmospheric_drone = false;
  for (int i = 0; i < MIDI_MAX; i++) {
    atmospheric_drone_notes[i] = false;
    piano_notes[i] = false;
    breath_chord_notes[i] = false;
  }
  arpeggiator_on = false;
  current_arpeggiator_pattern = 0;
  current_arpeggiator_note = -1;
  current_drum_note = -1;

  button_endpoint = ENDPOINT_SAX;
  sax_on = true;
  root_note = 26;  // D @ 37Hz

  air_locked = false;
  locked_air = 0;

  musical_mode = MODE_MAJOR;
  whistle_anchor_note = 60; // this is arbitrary

  last_hihat_ns = 0;
  for (int i = 0; i < KICK_TIMES_LENGTH; i++) {
    kick_times[i] = 0;
  }
  kick_times_index = 0;

  for (int i = 0; i < SNARE_TIMES_LENGTH; i++) {
    snare_times[i] = 0;
  }
  snare_times_index = 0;

  for (int i = 0; i < N_SUBBEATS; i++) {
    next_ns[i] = 0;
    tambourine_next_ns[i] = 0;
  }

  fc_feet_on = false;
  rhythm_mode = 0;
  beat_location_3 = 0;
  beat_location_24 = 0;

  state = STATE_DEFAULT;
  jig_time = false;

  for (int i = 0; i < MAX_SCHEDULED_NOTES; i++) {
    scheduled_notes[i].ts = 0;
    // Any ScheduledNote with ts = 0 is considered unset.
  }

  current_beat_ns = 0;

  breath_chord_on = false;
  breath_chord_playing = false;

  is_minor_chord = false;

  current_drum_vel = 0;
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

#define PACKET_BUF_SIZE (3+64) /* 3 for message, 32 for structure vars */
void send_midi(char actionType, int noteNo, int v, int endpoint) {
  /*if (endpoint == ENDPOINT_RHODES) {
    printf("sending %d %x:%d = %d\n",
           endpoint,
           (unsigned char) actionType,
           noteNo,
           v);
           }*/

  Byte buffer[PACKET_BUF_SIZE];
  Byte msg[3];
  msg[0] = actionType;
  msg[1] = noteNo;
  msg[2] = v;

  MIDIPacketList *packetList = (MIDIPacketList*) buffer;
  MIDIPacket *curPacket = MIDIPacketListInit(packetList);

  curPacket = MIDIPacketListAdd(packetList,
				PACKET_BUF_SIZE,
				curPacket,
				AudioGetCurrentHostTime(),
				3,
				msg);
  if (!curPacket) {
      die("packet list allocation failed");
  }

  attempt(MIDIReceived(endpoints[endpoint], packetList), "error sending midi");
}

// Return the distance between two notes in MIDI space, ignoring
// octave.  For example, if noteA is A57, then G55, G67, and G79 are
// all two notes away.
double distance(double noteA, double noteB) {
  double dist = noteA - noteB;
  while (dist < -6) {
    dist += 12;
  }
  while (dist > 6) {
    dist -= 12;
  }
  return fabs(dist);
}

int current_drum_pedal_note() {
  int note = root_note;

  is_minor_chord = false;
  if (musical_mode == MODE_MAJOR) {
    if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_1) {
      note = root_note - 3;  // vi
      is_minor_chord = true;
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_3) {
      note = root_note + 5;  // IV
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_4) {
      note = root_note + 7;  // V
    }
  } else if (musical_mode == MODE_MIXO) {
    if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_1) {
      note = root_note - 2; // VII
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_3) {
      note = root_note + 5;  // IV
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_4) {
      note = root_note + 7;  // V
    }
  } else if (musical_mode == MODE_MINOR) {
    if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_3) {
      note = root_note - 2;  // VII
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_2) {
      note = root_note - 4;  // VI
    } else if (most_recent_drum_pedal == MIDI_DRUM_PEDAL_1) {
      note = root_note - 5;  // V
    } else {
      is_minor_chord = true;  // i
    }
    note += 12;
  }

  return note;
}

char active_note() {
  if (listen_drum_pedal) {
    return current_drum_pedal_note();
  }
  return root_note;
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

void bass_trombone_off() {
  if (current_note[ENDPOINT_TROMBONE] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_TROMBONE], 0, ENDPOINT_TROMBONE);
    current_note[ENDPOINT_TROMBONE] = -1;
  }
}

void vbass_trombone_off() {
  if (current_note[ENDPOINT_BASS_TROMBONE] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_BASS_TROMBONE], 0, ENDPOINT_BASS_TROMBONE);
    current_note[ENDPOINT_BASS_TROMBONE] = -1;
  }
}

void arpeggiate_tambourine(int subbeat) {
  bool downbeat = subbeat == 4;
  bool upbeat = (jig_time && subbeat == 1) || subbeat == 2;
  bool emphasize = jig_time && subbeat == 2;
    
  if ((auto_hihat_mode == 1 && downbeat) ||
      (auto_hihat_mode == 2 && (downbeat || upbeat)) ||
      (auto_hihat_mode == 6 && (downbeat || upbeat))) {
    send_midi(MIDI_ON, MIDI_DRUM_HIHAT_CLOSED, 100, emphasize ? ENDPOINT_TAMBOURINE_STOPPED : ENDPOINT_TAMBOURINE_FREE);
  }     
}  

void arpeggiate(int subbeat) {
  if (current_drum_note != -1) {
    send_midi(MIDI_OFF, current_drum_note, 0, ENDPOINT_DRUM);
    current_drum_note = -1;
  }

  bool send_note = true;
  bool end_note = true;
  int note_out = -1;
  if (arpeggiator_on) {
    note_out = active_note();
  
    if (current_arpeggiator_pattern == 0) {
      if (subbeat == 0) {
      } else if (subbeat == 2) {
	note_out += 12;
      } else {
	send_note = false;
	end_note = false;
      }
    } else if (current_arpeggiator_pattern == 1) {
      if (subbeat == 0 || subbeat == 1) {
      } else if (subbeat == 2 || subbeat == 3) {
	note_out += 12;
      } else {
	send_note = false;
      }
    } else if (current_arpeggiator_pattern == 2) {
      if (subbeat == 0) {
      } else if (subbeat == 1) {
	note_out += 7;
      } else if (subbeat == 2) {
	note_out += 12;
      } else if (subbeat == 3) {
	note_out += 12 + 7;
      } else {
	send_note = false;
      }
    } else if (current_arpeggiator_pattern == 3) {
      if (subbeat == 0) {
      } else if (subbeat == 1) {
	note_out += 12;
      } else if (subbeat == 2) {
	note_out += 12 + 7;
      } else if (subbeat == 3) {
	note_out += 12 + 12;
      } else {
	send_note = false;
      }
    } else if (current_arpeggiator_pattern == 4) {
      if (subbeat == 0) {
      } else if (subbeat == 1) {
	note_out += 12;
      } else if (subbeat == 2) {
	note_out += 12 + 12;
      } else if (subbeat == 3) {
	note_out += 12 + 12 + 12;
      } else {
	send_note = false;
      }
    }
  }

  if (end_note && current_arpeggiator_note != -1) {
    send_midi(MIDI_OFF, current_arpeggiator_note, 0, ENDPOINT_FOOTBASS); 
    current_arpeggiator_note = -1;
  }

  if (send_note) {
    if (note_out != -1) {
      send_midi(MIDI_ON, note_out, 90, ENDPOINT_FOOTBASS);
      current_arpeggiator_note = note_out;
    }
  }

  int auto_hihat_1_vol = 0;
  int auto_hihat_2_vol = 0;
  int auto_hihat_3_vol = 0;
  int auto_hihat_4_vol = 0;
  if (auto_hihat_mode == 0 ||
      auto_hihat_mode == 1 ||
      auto_hihat_mode == 2) {
  } else if (auto_hihat_mode == 3) {
    auto_hihat_3_vol = 100;
  } else if (auto_hihat_mode == 4) {
    auto_hihat_1_vol = 100;
    auto_hihat_3_vol = 100;
  } else if (auto_hihat_mode == 5) {
    auto_hihat_3_vol = 100;
    auto_hihat_4_vol = 90;
  } else if (auto_hihat_mode == 6) {
    auto_hihat_3_vol = 100;
    auto_hihat_4_vol = 90;
  } else if (auto_hihat_mode == 7) {
    auto_hihat_1_vol = 70;
    auto_hihat_2_vol = 60;
    auto_hihat_3_vol = 100;
    auto_hihat_4_vol = 60;
  } else if (auto_hihat_mode == 8) {
    auto_hihat_1_vol = 70;
    auto_hihat_2_vol = 60;
    auto_hihat_3_vol = 100;
    auto_hihat_4_vol = 100;
  }

  int auto_hihat_vol = 0;
  if (subbeat == 4) {
    auto_hihat_vol = auto_hihat_1_vol;
  } else if (subbeat == 1) {
    auto_hihat_vol = auto_hihat_2_vol;
  } else if (subbeat == 2) {
    auto_hihat_vol = auto_hihat_3_vol;
  } else if (subbeat == 3) {
    auto_hihat_vol = auto_hihat_4_vol;
  }

  if (auto_hihat_vol) {
    int vel = current_drum_vel * auto_hihat_vol / 100;

    if (fc_feet_on) {
      send_midi(MIDI_ON, MIDI_DRUM_HIHAT_CLOSED, vel, subbeat == 2 ? ENDPOINT_FOOT_3 : ENDPOINT_FOOT_4);
    } else {
      send_midi(MIDI_ON, MIDI_DRUM_HIHAT_CLOSED, vel, ENDPOINT_DRUM);
    }
    current_drum_note = MIDI_DRUM_HIHAT_CLOSED;
  }
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

void estimate_tempo(uint64_t current_time, bool imaginary, bool is_low) {
  current_beat_ns = 0;

  // Take a super naive approach: for each candidate tempo, consider
  // how much error that would imply for each recent hit we've seen,
  // and take the tempo with the lowest error.
  //
  // If the hit doesn't fit the pattern, don't give up, treat it as
  // an extra hit and ignore it.
  
  float best_bpm = -1;
  // something way high, in case we fail to find anything
  uint64_t best_error = NS_PER_SEC * 100; 

  int n_downbeats_to_consider = 4;

  for (float candidate_bpm = 100;
       candidate_bpm <= 140;
       candidate_bpm += 0.25) {

    uint64_t whole_note_ns = 60L * NS_PER_SEC / candidate_bpm;
    uint64_t candidate_error = 0;

    // Look for a kick or snare on every past downbeat, but allow
    // kicks preceeding a snare to be half a beat early.
    for (int i = 0; i < n_downbeats_to_consider; i++) {
      uint64_t target = current_time - (i+1)*whole_note_ns;
      uint64_t error = min(best_match_hit(target, kick_times, KICK_TIMES_LENGTH),
			   best_match_hit(target, snare_times, SNARE_TIMES_LENGTH));
      bool early_kick_allowed = i % 2 == (is_low ? 1 : 0);
      if (early_kick_allowed) {
	uint64_t early_kick_target = target - (whole_note_ns/2);
	uint64_t early_kick_error = best_match_hit(early_kick_target, kick_times, KICK_TIMES_LENGTH);
	error = min(error, early_kick_error);
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
    return;
  }

  uint64_t whole_beat = NS_PER_SEC * 60 / best_bpm;

  if (imaginary) {
    // If this is an imaginary beat, require there to have been at
    // kick hit about half a beat ago, since we're trying to handle
    // the case where the kick is played early.
    uint64_t target = current_time - (whole_beat / 2);
    uint64_t error = best_match_hit(target, kick_times, KICK_TIMES_LENGTH);
    if (error > whole_beat / 16) {
      printf("not firing imaginary beat: looked for %llu, best was %.2f%%",
	     whole_beat / 2,  100*(float)error / (float) (whole_beat / 16));
      return;
    }
  }

  // Allow error of up to 1/32 note on each of the downbeats.
  uint64_t max_allowed_error = (whole_beat * n_downbeats_to_consider) / 32;

  bool acceptable_error = best_error < max_allowed_error;

  printf("%c BPM estimate: %f  (error: %llu, max allowed: %llu, frac: %.2f%%)\n",
         acceptable_error ? ' ' : '!',
         best_bpm,
	 best_error,
	 max_allowed_error,
	 100 * (float)best_error / (float)max_allowed_error);
  
  if (acceptable_error) {
    current_beat_ns = whole_beat;
    arpeggiate(0);

    next_ns[0] = current_time;
    next_ns[4] = current_time + whole_beat;

    if (jig_time) {
      uint64_t third_beat = whole_beat / 3;
      next_ns[1] = current_time + third_beat;
      next_ns[2] = current_time + 2*third_beat;
      next_ns[3] = 0;

      next_ns[1] += (int64_t)(beat_location_24 * third_beat);
      next_ns[2] += (int64_t)(beat_location_3 * third_beat);
    } else {
      uint64_t quarter_beat = whole_beat / 4;
      for (int i = 1; i < N_SUBBEATS; i++) {
	next_ns[i] = next_ns[i-1] + quarter_beat;
      }
      next_ns[1] += (int64_t)(beat_location_24 * quarter_beat);
      next_ns[2] += (int64_t)(beat_location_3 * quarter_beat);
      next_ns[3] += (int64_t)(beat_location_24 * quarter_beat);
    }

    for (int i = 0 ; i < N_SUBBEATS; i++) {
      if (next_ns[i] > 0) {
	tambourine_next_ns[i] = next_ns[i] - (uint64_t)(0.094 * NS_PER_SEC);
      }
    }
  }
}

void count_drum_hit(bool is_low) {
  if (is_low) {
    kick_times[kick_times_index] = now();
    estimate_tempo(kick_times[kick_times_index], /*imaginary=*/false, /*is_low=*/true);
    kick_times_index++;
    kick_times_index = kick_times_index % KICK_TIMES_LENGTH;
  } else {
    snare_times[snare_times_index] = now();
    estimate_tempo(snare_times[snare_times_index], /*imaginary=*/false, /*is_low=*/false);
    snare_times_index++;
    snare_times_index = snare_times_index % SNARE_TIMES_LENGTH;
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
  
  if (listen_drum_pedal) {
    int third = note_out + (is_minor_chord ? 3 : 4);
    while (third + 12 <= MAX_HAMMOND_NOTE) third += 12;
    breath_chord_note_on(third);
  }
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

  if (breath < MIN_TROMBONE) {
    bass_trombone_off();
    vbass_trombone_off();
    return;
  }

  int trombone_note = note_out + 12;
  if (trombone_note < 40) {
    trombone_note += 12;
  }
  trombone_note += 12;
  if (bass_trombone_on && current_note[ENDPOINT_TROMBONE] != trombone_note) {
    bass_trombone_off();
    send_midi(MIDI_ON, trombone_note, piano_left_hand_velocity, ENDPOINT_TROMBONE);
    current_note[ENDPOINT_TROMBONE] = trombone_note;
  }
  int bass_trombone_note = trombone_note - 12;
  if (bass_trombone_note < 32) {
    bass_trombone_note += 12;
  }
  bass_trombone_note += 12;
  if (vbass_trombone_on && current_note[ENDPOINT_BASS_TROMBONE] != bass_trombone_note) {
    vbass_trombone_off();
    send_midi(MIDI_ON, bass_trombone_note, piano_left_hand_velocity, ENDPOINT_BASS_TROMBONE);
    current_note[ENDPOINT_BASS_TROMBONE] = bass_trombone_note;
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

void print_endpoints() {
  printf("MIDI Endpoints Detected:\n");

  int n_sources = MIDIGetNumberOfSources();
   for (int i = 0; i < n_sources ; i++) {
    MIDIEndpointRef src = MIDIGetSource(i);
    if (!src) continue;

    CFStringRef name;
    MIDIObjectGetStringProperty (src, kMIDIPropertyName, &name);

    printf("    %s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8));
  }
  printf("\n");
}

bool get_endpoint_ref(CFStringRef target_name, MIDIEndpointRef* endpoint_ref) {
  int n_sources = MIDIGetNumberOfSources();
  if (!n_sources) {
    die("no midi sources found");
  }
  for (int i = 0; i < n_sources ; i++) {
    MIDIEndpointRef src = MIDIGetSource(i);
    if (!src) continue;

    CFStringRef name;
    MIDIObjectGetStringProperty (src, kMIDIPropertyName, &name);

    if (CFStringCompare(name, target_name, 0) == kCFCompareEqualTo) {
      *endpoint_ref = src;
      return true;
    }
  }
  return false;  // failed to find one
}


void endpoint_notes_off(int endpoint) {
  for (int note = 0 ; note < 128; note++) {
    send_midi(MIDI_OFF, note, 0, endpoint);
  }
  // send an explicit all notes off as well
  send_midi(MIDI_CC, 123, 0, endpoint);
}

void all_notes_off() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    endpoint_notes_off(endpoint);
  }
}

int to_root(note_out) {
  return (note_out - 2) % 12 + 26;
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

void handle_control_helper(unsigned int note_in) {
  switch (note_in) {

  case FULL_RESET:
    full_reset();
    return;

  case AIR_LOCK:
    air_lock();
    return;

  case SELECT_SAX_TROMBONE:
    all_notes_off();
    sax_on = !sax_on;
    change_button_endpoint(sax_on ? ENDPOINT_SAX : ENDPOINT_TROMBONE);
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

  case TOGGLE_BASS_TROMBONE:
    endpoint_notes_off(ENDPOINT_TROMBONE);
    endpoint_notes_off(ENDPOINT_BASS_TROMBONE);

    if (note_in == TOGGLE_BASS_TROMBONE) {
      bass_trombone_on = !bass_trombone_on;
      vbass_trombone_on = !vbass_trombone_on;
    }

    if (bass_trombone_on) {
      update_bass();
    } else {
      bass_trombone_off();
      vbass_trombone_off();
    }
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

  case TOGGLE_DRUM_PEDAL_KICK:
    if (current_drum_pedal_kick_note != MIDI_DRUM_KICK) {
      current_drum_pedal_kick_note = MIDI_DRUM_KICK;
    } else {
      current_drum_pedal_kick_note = 0;
    }
    return;

  case ROTATE_DRUM_PEDAL_TSS:
    if (current_drum_pedal_tss_note == 0) {
      current_drum_pedal_tss_note = MIDI_DRUM_RIM;
    } else if (current_drum_pedal_tss_note == MIDI_DRUM_RIM) {
      current_drum_pedal_tss_note = MIDI_DRUM_TAMBOURINE;
    } else if (current_drum_pedal_tss_note == MIDI_DRUM_TAMBOURINE) {
      current_drum_pedal_tss_note = MIDI_DRUM_SNARE;
    } else {
      current_drum_pedal_tss_note = 0;
    }
    return;

  case ROTATE_RHYTHM_MODE:
    rhythm_mode = (rhythm_mode + 1) % N_RHYTHM_MODES;
    if (rhythm_mode == 0) {
      beat_location_3 = 0;
      beat_location_24 = 0;
    } else if (rhythm_mode == 1) {
      /* Sometimes we want even divisions, and that's simple, but other
       * times we want it swung.  Here are some measurements from
       * playing around on mandolin:
       *
       *  0    0.000   0.000   0.000     00.0%   00.0%   00.0%      00.0%
       *  2    0.154   0.139   0.168     26.9%   26.0%   30.6%      27.8%
       *  4    0.275   0.253   0.264     47.9%   47.3%   48.1%      47.8%
       *  6    0.444   0.421   0.420     77.4%   78.7%   76.5%      77.5%
       *  8    0.574   0.535   0.549    100.0%  100.0%  100.0%     100.0%
       *
       * Here are some measurements of pushing the beat while playing strict
       * upbeats:
       *
       * 47.2 49.2 48.0 49.8 -> 48.6
       *
       * These are just 3s; I wasn't playing 2 or 4.
       *
       * It looks like being up to 12% ahead/behind is a good range.
       *
       * Let's set an easy way to go to the measured mandolin setup.
       */
      beat_location_3 = -0.09;
      beat_location_24 = 0.10;
    }
    return;

  case TOGGLE_LISTEN_DRUM_PEDAL:
    listen_drum_pedal = !listen_drum_pedal;
    if (listen_drum_pedal) {
      current_drum_pedal_kick_note = MIDI_DRUM_KICK;
      current_drum_pedal_tss_note = MIDI_DRUM_RIM;
    } 
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
    if (arpeggiator_on) {
      send_midi(MIDI_CC, CC_11, FOOTBASS_VOLUME, ENDPOINT_FOOTBASS);
    }
    return;

  case ROTATE_ARPEGGIATOR_PATTERN:
    current_arpeggiator_pattern = (current_arpeggiator_pattern + 1) % N_ARPEGGIATOR_PATTERNS;
    return;

  case ROTATE_AUTO_HIHAT:
    auto_hihat_mode = (auto_hihat_mode + 1) % N_AUTO_HIHAT_MODES;
    return;

  case TOGGLE_FC_FEET:
    fc_feet_on = !fc_feet_on;
    if (fc_feet_on) {
      auto_hihat_mode = 5;
      current_drum_pedal_kick_note = MIDI_DRUM_KICK;
    }      
    return;

  case TOGGLE_REEL_JIG:
    jig_time = !jig_time;
    return;

  }
}

void handle_control(unsigned int note_in) {
  handle_control_helper(note_in);
}

int remap(int val, int min, int max) {
  int range = max - min;
  return val * range / MIDI_MAX + min;
}

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  unsigned char note_out = mapping(note_in);

  if (button_endpoint == ENDPOINT_JAWHARP) {
    root_note = note_out;
    update_bass();
    return;
  }

  int chosen_endpoint = button_endpoint;
  if (button_endpoint == ENDPOINT_SAX) {
    note_out += 24;
    if (note_out < 54) {
      chosen_endpoint = ENDPOINT_BASS_SAX;
    }
  } else if (button_endpoint == ENDPOINT_TROMBONE) {
    if (note_out < 40) {
      chosen_endpoint = ENDPOINT_BASS_TROMBONE;
    }
  } else if (button_endpoint == ENDPOINT_ORGAN_LOW ||
             button_endpoint == ENDPOINT_JAWHARP) {
    // pass
  } else {
    note_out += 12;
  }

  if (button_endpoint == ENDPOINT_OVERDRIVEN_RHODES) {
    // This one is special: we fade from rhodes to overdriven rhodes based on
    // current breath.  That's handled as breath, so just send all note
    // triggers to both instruments.
    send_midi(mode, note_out, MIDI_MAX, ENDPOINT_OVERDRIVEN_RHODES);

    // only use 50-110 on the regular rhodes
    send_midi(mode, note_out, remap(val, 50, 110), ENDPOINT_RHODES);
    return;
  }

  if (button_endpoint == ENDPOINT_ORGAN_LOW ||
      button_endpoint == ENDPOINT_HAMMOND ||
      button_endpoint == ENDPOINT_ORGAN_FLEX ||
      button_endpoint == ENDPOINT_SINE_PAD ||
      button_endpoint == ENDPOINT_SWEEP_PAD) {
    val = MIDI_MAX;
  }

  send_midi(mode, note_out, val, chosen_endpoint);
}

void handle_feet(unsigned int mode, unsigned int note_in, unsigned int val) {
  // Handle pedals after determining the active note, so that only
  // future notes are affected.
  if (listen_drum_pedal) {
    if (note_in == MIDI_DRUM_PEDAL_1 ||
        note_in == MIDI_DRUM_PEDAL_2 ||
        note_in == MIDI_DRUM_PEDAL_3 ||
        note_in == MIDI_DRUM_PEDAL_4) {
      most_recent_drum_pedal = note_in;
    }
  }			    

  update_bass();

  bool is_low;
  if (note_in == MIDI_DRUM_KICK_A ||
      note_in == MIDI_DRUM_KICK_B) {
    is_low = true;
  } else if (note_in == MIDI_DRUM_PEDAL_1 ||
             note_in == MIDI_DRUM_PEDAL_2 ||
             note_in == MIDI_DRUM_PEDAL_3 ||
             note_in == MIDI_DRUM_PEDAL_4) {
    is_low = false;
  } else {
    return;
  }

  if (listen_drum_pedal) {
    // When using the drum pedals for note selection, left and right
    // are reversed.  It's terrible, but it's much easier than having
    // to switch the feet ahead of the beat.
    is_low = !is_low;
  }

  if (note_in == MIDI_DRUM_PEDAL_1 && !listen_drum_pedal) {
    last_hihat_ns = now();
  }

  if (mode == MIDI_ON) {
    count_drum_hit(is_low);
    if (is_low) {
      if (val < 80) {
	val = 80;
      }
      current_drum_vel = val;
    }
  }

  int drum_note = is_low ? current_drum_pedal_kick_note : current_drum_pedal_tss_note;
  if (drum_note != 0) {
    printf("sending %d to drum\n", drum_note);
    if (fc_feet_on) {
      send_midi(mode, drum_note, val, is_low ? ENDPOINT_FOOT_1 : ENDPOINT_FOOT_3);
    } else {
      if (!is_low && current_drum_pedal_tss_note == MIDI_DRUM_TAMBOURINE) {
        send_midi(mode, drum_note, val, ENDPOINT_TAMBOURINE_STOPPED);
      } else {
        send_midi(mode, drum_note, val, ENDPOINT_DRUM);
      }
      current_drum_note = drum_note;
    }
  }
}

void handle_whistle(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (mode == MIDI_OFF) {
    return;
  } else if (mode == MIDI_ON) {
    whistle_anchor_note = note_in;
  } else if (mode == MIDI_PITCH_BEND) {
    unsigned int lsb = note_in;
    unsigned int msb = val;
    unsigned int bend = lsb + (msb << 7);

    // bend = (1 + f_bend/2) * 8192 - 0.5
    // bend + 0.5 = (1 + f_bend/2) * 8192
    // (bend + 0.5) / 8192 = 1 + f_bend/2
    // (bend + 0.5) / 8192 - 1 = f_bend/2
    // ((bend + 0.5) / 8192 - 1)*2 = f_bend
    double midi_space_bend = ((((double)bend + 0.5) / 8192) - 1)*2;
    double midi_space_note = whistle_anchor_note + midi_space_bend;
    printf("whistle: %lf", midi_space_note);
  }
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc >= GCMIDI_MIN && cc <= GCMIDI_MAX) {
    send_midi(MIDI_CC, cc, val, ENDPOINT_SAX);
    send_midi(MIDI_CC, cc, val, ENDPOINT_BASS_SAX);
    send_midi(MIDI_CC, cc, val, ENDPOINT_TROMBONE);
    return;
  }

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
    } else if (breath_chord_playing && breath < 70) {
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
    if (endpoint != ENDPOINT_SAX &&
        endpoint != ENDPOINT_TROMBONE &&
        endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_ORGAN_FLEX &&
        endpoint != ENDPOINT_BASS_SAX &&
        endpoint != ENDPOINT_BASS_TROMBONE) {
      continue;
    }
    int use_val = val;
    if (use_val > MIDI_MAX) {
      use_val = MIDI_MAX;
    }
    if (endpoint == ENDPOINT_TROMBONE ||
        endpoint == ENDPOINT_BASS_TROMBONE) {
      use_val -= MIN_TROMBONE;
      if (use_val < 0) {
        use_val = 0;
      }
      if (use_val > MAX_TROMBONE) {
        use_val = MAX_TROMBONE;
      }
    }
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
    if ( ((bass_trombone_on && endpoint == ENDPOINT_TROMBONE) ||
         (vbass_trombone_on && endpoint == ENDPOINT_BASS_TROMBONE))) {
      if (breath < 2 &&
          current_note[endpoint] != -1) {
        send_midi(MIDI_OFF, current_note[endpoint], 0, endpoint);
        current_note[endpoint] = -1;
      } else if (breath > 3) {
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

const char* button_endpoint_str() {
  switch (button_endpoint) {
  case ENDPOINT_SAX:
    return "sax";
    break;
  case ENDPOINT_TROMBONE:
    return "trm";
  default:
    return "???";
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
  } else if (mode == MIDI_ON && state != STATE_DEFAULT) {
    float adjustment = 0;
    if (note_in >= 85 && note_in <= 91) {
      // move beat behind
      adjustment = (note_in - 84) * 0.015;
    } else if (note_in >= 78 && note_in <= 84) {
      // move beat ahead
      adjustment = -(note_in - 77) * 0.015;
    }
    
    if (state == STATE_ADJUST_3) {
      beat_location_3 = adjustment;
    } else if (state == STATE_ADJUST_24) {
      beat_location_24 = adjustment;
    }
    state = STATE_DEFAULT;
  }
  if (listen_drum_pedal &&
      (note_in == BUTTON_MAJOR ||
       note_in == BUTTON_MIXO || 
       note_in == BUTTON_MINOR)) {
    if (note_in == BUTTON_MAJOR) {
      musical_mode = MODE_MAJOR;
    } else if (note_in == BUTTON_MIXO) {
      musical_mode = MODE_MIXO;
    } else if (note_in == BUTTON_MINOR) {
      musical_mode = MODE_MINOR;
    }
    printf("Chose mode %d\n", musical_mode);
  } else if (note_in <= CONTROL_MAX) {
    if (mode == MIDI_ON) {
      handle_control(note_in);
    }
  } else {
    handle_button(mode, note_in, val);
  }
}

void read_midi(const MIDIPacketList *pktlist,
               void *readProcRefCon,
               void *srcConnRefCon) {
  const MIDIPacket* packet = pktlist->packet;

  for (int i = 0; i < pktlist->numPackets; i++) {
    if (packet->length == 1) {
      // foot controller sends timing sync messages we don't care about
      continue;
    } else if (packet->length != 3) {
      printf("bad packet len: %d\n", packet->length);
    } else {
      unsigned int mode = packet->data[0];
      unsigned int note_in = packet->data[1];
      unsigned int val = packet->data[2];

      if (val > 0) {
        printf("got packet %u %u %u\n", mode, note_in, val);
      }

      //unsigned int channel = mode & 0x0F;
      mode = mode & 0xF0;

      if (mode == MIDI_ON && val == 0) {
        mode = MIDI_OFF;
      }

      if (srcConnRefCon == &midiport_piano) {
        handle_piano(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_axis_49) {
	handle_axis_49(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_feet_controller) {
        handle_feet(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_whistle) {
        handle_whistle(mode, note_in, val);
      } else if (mode == MIDI_CC) {
        handle_cc(note_in, val);
      } else {
        printf("ignored\n");
      }
    }
    packet = MIDIPacketNext(packet);
  }
}

void connect_source(MIDIEndpointRef endpoint_ref, MIDIPortRef* port_ref) {
  attempt(
    MIDIInputPortCreate(
      midiclient,
      CFSTR("input port"),
      &read_midi,
      NULL /* refCon */,
      port_ref),
   "creating input port");

  attempt(
    MIDIPortConnectSource(
      *port_ref,
      endpoint_ref,
      port_ref /* connRefCon */),
    "connecting to device");
}

void create_source(MIDIEndpointRef* endpoint_ref, CFStringRef name) {
  attempt(
    MIDISourceCreate(
      midiclient,
      name,
      endpoint_ref),
   "creating OS-X virtual MIDI source." );
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

  attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &midiclient),
    "creating OS-X MIDI client object." );

  print_endpoints();

  MIDIEndpointRef axis49,
    breath_controller,
    game_controller,
    feet_controller,
    whistle,
    piano_controller;
  if (get_endpoint_ref(CFSTR("AXIS-49 2A"), &axis49)) {
    connect_source(axis49, &midiport_axis_49);
  }
  if (get_endpoint_ref(CFSTR("Breath Controller 5.0-15260BA7"), &breath_controller)) {
    connect_source(breath_controller, &midiport_breath_controller);
  }
  if (get_endpoint_ref(CFSTR("game controller"), &game_controller)) {
    connect_source(game_controller, &midiport_game_controller);
  }
  if (get_endpoint_ref(CFSTR("mio"), &feet_controller)) {
    connect_source(feet_controller, &midiport_feet_controller);
  }
  if (get_endpoint_ref(CFSTR("whistle-pitch"), &whistle)) {
    connect_source(whistle, &midiport_whistle);
  }
  if (get_endpoint_ref(CFSTR(PIANO_MIDI_NAME), &piano_controller)) {
    connect_source(piano_controller, &midiport_piano);
    piano_on = true;
  }

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }

  create_source(&endpoints[ENDPOINT_SAX],                CFSTR("jammer-sax"));
  create_source(&endpoints[ENDPOINT_TROMBONE],           CFSTR("jammer-trombone"));
  create_source(&endpoints[ENDPOINT_JAWHARP],            CFSTR("jammer-jawharp"));
  create_source(&endpoints[ENDPOINT_BASS_SAX],           CFSTR("jammer-bass-sax"));
  create_source(&endpoints[ENDPOINT_BASS_TROMBONE],      CFSTR("jammer-bass-trombone"));
  create_source(&endpoints[ENDPOINT_HAMMOND],            CFSTR("jammer-hammond"));
  create_source(&endpoints[ENDPOINT_ORGAN_LOW],          CFSTR("jammer-organ-low"));
  create_source(&endpoints[ENDPOINT_ORGAN_FLEX],         CFSTR("jammer-organ-flex"));
  create_source(&endpoints[ENDPOINT_SINE_PAD],           CFSTR("jammer-sine-pad"));
  create_source(&endpoints[ENDPOINT_SWEEP_PAD],          CFSTR("jammer-sweep-pad"));
  create_source(&endpoints[ENDPOINT_OVERDRIVEN_RHODES],  CFSTR("jammer-overdriven-rhodes"));
  create_source(&endpoints[ENDPOINT_RHODES],             CFSTR("jammer-rhodes"));
  create_source(&endpoints[ENDPOINT_DRUM],               CFSTR("jammer-drum"));
  create_source(&endpoints[ENDPOINT_FOOT_1],             CFSTR("jammer-foot-1"));
  create_source(&endpoints[ENDPOINT_FOOT_3],             CFSTR("jammer-foot-3"));
  create_source(&endpoints[ENDPOINT_FOOT_4],             CFSTR("jammer-foot-4"));
  create_source(&endpoints[ENDPOINT_TAMBOURINE_FREE],    CFSTR("jammer-tambourine-free"));
  create_source(&endpoints[ENDPOINT_TAMBOURINE_STOPPED], CFSTR("jammer-tambourine-stopped"));
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
    send_midi(MIDI_CC, CC_07, breath_chord_on ? 100 : val, ENDPOINT_HAMMOND);
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

  // If we had a manual hihat hit in the last 1s, clear it all and
  // don't trigger anything.
  if (current_time - last_hihat_ns < NS_PER_SEC) {
    for (int i = 0 ; i < N_SUBBEATS ; i++) {
      next_ns[i] = 0;
      tambourine_next_ns[i] = 0;
    }
    return;
  }

  for (int i = 1 /* 0 is triggered by kick directly */; i < N_SUBBEATS; i++) {
    if (next_ns[i] > 0 && current_time > next_ns[i]) {
      arpeggiate(i);
      next_ns[i] = 0;
      if (i == 4) {
	estimate_tempo(current_time, /*imaginary=*/true, /*is_low=*/true);
      }
    }
    if (tambourine_next_ns[i] > 0 && current_time > tambourine_next_ns[i]) {
      arpeggiate_tambourine(i);
      tambourine_next_ns[i] = 0;
    }
  }
}

void jml_tick() {
  // Called every TICK_MS
  update_air();
  forward_air();
  trigger_subbeats();
  //trigger_scheduled_notes();
}

#endif
