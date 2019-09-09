#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2


/*
  OR  ST  J   Of  bT  b5  bS
    Sl              Bt  b8  bH
              Rh  H   Ol  Sp

  O  R

Which is:
 92 OR  Overdriven Rhodes
 93 ST  Sax vs Trombone
 94  J  Jawharp
 95 Of  Organ flex
 96 bT  Bass Trombone
 97 b5  Bass trombone up a fifth
 98 bS  Breath Ride

 85 Sl  Slide

 89 Bt  Very bass trombone
 90 b8  Bass trombone up an octave
 91 bH  Breath Hihat

 81 Rh  Rhodes
 82 H   Hammond
 83 Ol  Organ Low
 84 Sp  Sine Pad

 2  R   Reset
 1  O   All notes off
 */


void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void attempt(OSStatus result, char* errmsg) {
  if (result != noErr) {
    die(errmsg);
  }
}

/* controls */
#define ALL_NOTES_OFF          1
#define FULL_RESET             2
#define LOW_CONTROL_MAX        FULL_RESET

#define TOGGLE_OVERDRIVEN_RHODES  92
#define SELECT_SAX_TROMBONE       93
#define TOGGLE_JAWHARP            94
#define TOGGLE_ORGAN_FLEX         95
#define TOGGLE_BASS_TROMBONE      96
#define TOGGLE_BT_UP_8            97
#define TOGGLE_BREATH_RIDE        98

#define TOGGLE_SLIDE           85
#define TOGGLE_TBD_A           86
#define TOGGLE_TBD_B           87
#define TOGGLE_TBD_C           88

#define TOGGLE_VBASS_TROMBONE  89
#define TOGGLE_VBT_UP_8        90
#define TOGGLE_BREATH_HIHAT    91
#define HIGH_CONTROL_MIN       TOGGLE_RHODES

#define TOGGLE_RHODES          81
#define TOGGLE_HAMMOND         82
#define TOGGLE_ORGAN_LOW       83
#define TOGGLE_SINE_PAD        84

#define BASS_MIN               71

/* endpoints */
#define ENDPOINT_SAX 0
#define ENDPOINT_TROMBONE 1
#define N_BUTTON_ENDPOINTS (ENDPOINT_TROMBONE + 1)
#define ENDPOINT_JAWHARP 2
#define ENDPOINT_BASS_SAX 3
#define ENDPOINT_BASS_TROMBONE 4
#define ENDPOINT_HAMMOND 5
#define ENDPOINT_ORGAN_LOW 6
#define ENDPOINT_ORGAN_FLEX 7
#define ENDPOINT_SINE_PAD 8
#define ENDPOINT_OVERDRIVEN_RHODES 9
#define ENDPOINT_RHODES 10
#define ENDPOINT_TBD_A 12
#define ENDPOINT_TBD_B 13
#define ENDPOINT_TBD_C 14
#define ENDPOINT_SLIDE 15
#define ENDPOINT_BREATH_DRUM 16
#define N_ENDPOINTS (ENDPOINT_BREATH_DRUM+1)

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

// gcmidi sends on CC 20 through 29
#define GCMIDI_MIN 20
#define GCMIDI_MAX 29

#define CC_ROLL 30
#define CC_PITCH 31

#define MIDI_DRUM_LOW 36  // Acoustic Bass Drum
#define MIDI_DRUM_HIGH 42  // Closed Hi-Hat

#define MIDI_MAX 127

#define TICK_MS 10  // try to tick every N milliseconds

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

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;
MIDIPortRef midiport_breath_controller;
MIDIPortRef midiport_game_controller;
MIDIPortRef midiport_tilt_controller;
MIDIPortRef midiport_piano;
MIDIPortRef midiport_imitone;

bool piano_on = false;  // Initialized based on availablity of piano.

/* Anything mentioned here should be initialized in voices_reset */
bool tilt_on;
bool jawharp_on;
bool slide_on;
bool bass_trombone_on;
bool bass_trombone_up_8;
bool vbass_trombone_up_8;
bool vbass_trombone_on;
bool hammond_on;
bool organ_low_on;
bool organ_flex_on;
bool sine_pad_on;
bool overdriven_rhodes_on;
bool rhodes_on;
bool tbd_a_on;
bool tbd_b_on;
bool tbd_c_on;
bool breath_ride_on;
bool breath_hihat_on;
bool sax_on;
int button_endpoint;
int root_note;

void voices_reset() {
  tilt_on = false;
  jawharp_on = false;
  slide_on = false;
  bass_trombone_on = false;
  bass_trombone_up_8 = false;
  vbass_trombone_up_8 = false;
  vbass_trombone_on = false;
  hammond_on = false;
  organ_low_on = false;
  organ_flex_on = false;
  sine_pad_on = false;
  overdriven_rhodes_on = false;
  rhodes_on = false;
  tbd_a_on = false;
  tbd_b_on = false;
  tbd_c_on = false;
  breath_ride_on = false;
  breath_hihat_on = false;

  button_endpoint = ENDPOINT_SAX;
  sax_on = true;
  root_note = 26;  // D @ 37Hz
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
//  * Always in use for slide
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
  //printf("sending %d %x:%d = %d\n",
  //       endpoint,
  //       (unsigned char) actionType,
  //       noteNo,
  //      v);

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

char active_note() {
  return root_note;
}

void jawharp_off() {
  if (current_note[ENDPOINT_JAWHARP] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = -1;
  }
}

void slide_off() {
  if (current_note[ENDPOINT_SLIDE] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_SLIDE], 0, ENDPOINT_SLIDE);
    current_note[ENDPOINT_SLIDE] = -1;
  }
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

int slide_note() {
  int octave = breath < 100 ? 0 : 1;
  return active_note() + 12*octave;
}

void update_bass() {
  if (breath < 3) return;

  int note_out = active_note();
  if (jawharp_on && current_note[ENDPOINT_JAWHARP] != note_out) {
    jawharp_off();
    send_midi(MIDI_ON, note_out, MIDI_MAX, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = note_out;
  }

  int slide_note_out = slide_note();
  if (slide_on && current_note[ENDPOINT_SLIDE] != slide_note_out) {
    slide_off();
    printf("slide: %d\n", slide_note_out);
    send_midi(MIDI_ON, slide_note_out, MIDI_MAX, ENDPOINT_SLIDE);
    current_note[ENDPOINT_SLIDE] = slide_note_out;
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
  if (bass_trombone_up_8) {
    trombone_note += 12;
  }
  if (bass_trombone_on && current_note[ENDPOINT_TROMBONE] != trombone_note) {
    bass_trombone_off();
    send_midi(MIDI_ON, trombone_note, piano_left_hand_velocity, ENDPOINT_TROMBONE);
    current_note[ENDPOINT_TROMBONE] = trombone_note;
  }
  int bass_trombone_note = trombone_note - 12;
  if (bass_trombone_note < 32) {
    bass_trombone_note += 12;
  }
  if (vbass_trombone_up_8) {
    bass_trombone_note += 12;
  }
  if (vbass_trombone_on && current_note[ENDPOINT_BASS_TROMBONE] != bass_trombone_note) {
    vbass_trombone_off();
    send_midi(MIDI_ON, bass_trombone_note, piano_left_hand_velocity, ENDPOINT_BASS_TROMBONE);
    current_note[ENDPOINT_BASS_TROMBONE] = bass_trombone_note;
  }
}

char mapping(unsigned char note_in) {
  switch(note_in) {
  case 1: return 1;  // Db
  case 2: return 3;  // Eb
  case 3: return 5;  // F
  case 4: return 7;  // G
  case 5: return 9;  // A
  case 6: return 11;  // B
  case 7: return 13;  // C#
  case 8: return 8;  // Ab
  case 9: return 10;  // Bb
  case 10: return 12;  // C
  case 11: return 14;  // D
  case 12: return 16;  // E
  case 13: return 18;  // F#
  case 14: return 20;  // G#
  case 15: return 13;  // Db
  case 16: return 15;  // Eb
  case 17: return 17;  // F
  case 18: return 19;  // G
  case 19: return 21;  // A
  case 20: return 23;  // B
  case 21: return 25;  // C#
  case 22: return 20;  // Ab
  case 23: return 22;  // Bb
  case 24: return 24;  // C
  case 25: return 26;  // D
  case 26: return 28;  // E
  case 27: return 30;  // F#
  case 28: return 32;  // G#
  case 29: return 25;  // Db
  case 30: return 27;  // Eb
  case 31: return 29;  // F
  case 32: return 31;  // G
  case 33: return 33;  // A
  case 34: return 35;  // B
  case 35: return 37;  // C#
  case 36: return 32;  // Ab
  case 37: return 34;  // Bb
  case 38: return 36;  // C
  case 39: return 38;  // D
  case 40: return 40;  // E
  case 41: return 42;  // F#
  case 42: return 44;  // G#
  case 43: return 37;  // Db
  case 44: return 39;  // Eb
  case 45: return 41;  // F
  case 46: return 43;  // G
  case 47: return 45;  // A
  case 48: return 47;  // B
  case 49: return 49;  // C#
  case 50: return 44;  // Ab
  case 51: return 46;  // Bb
  case 52: return 48;  // C
  case 53: return 50;  // D
  case 54: return 52;  // E
  case 55: return 54;  // F#
  case 56: return 56;  // G#
  case 57: return 51;  // Eb
  case 58: return 53;  // F
  case 59: return 55;  // G
  case 60: return 57;  // A
  case 61: return 59;  // B
  case 62: return 61;  // C#
  case 63: return 63;  // D#
  case 64: return 56;  // Ab
  case 65: return 58;  // Bb
  case 66: return 60;  // C
  case 67: return 62;  // D
  case 68: return 64;  // E
  case 69: return 66;  // F#
  case 70: return 68;  // G#
  case 71: return 63;  // Eb
  case 72: return 65;  // F
  case 73: return 67;  // G
  case 74: return 69;  // A
  case 75: return 71;  // B
  case 76: return 73;  // C#
  case 77: return 75;  // D#
  case 78: return 68;  // Ab
  case 79: return 70;  // Bb
  case 80: return 72;  // C
  case 81: return 74;  // D
  case 82: return 76;  // E
  case 83: return 78;  // F#
  case 84: return 80;  // G#
  case 85: return 75;  // Eb
  case 86: return 77;  // F
  case 87: return 79;  // G
  case 88: return 81;  // A
  case 89: return 83;  // B
  case 90: return 85;  // C#
  case 91: return 87;  // D#
  case 92: return 80;  // Ab
  case 93: return 82;  // Bb
  case 94: return 84;  // C
  case 95: return 86;  // D
  case 96: return 88;  // E
  case 97: return 90;  // F#
  case 98: return 92;  // G
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

void all_notes_off(int max_endpoint) {
  for (int endpoint = 0; endpoint < max_endpoint; endpoint++) {
    endpoint_notes_off(endpoint);
  }
}

int to_root(note_out) {
  return (note_out - 2) % 12 + 26;
}

void handle_imitone(unsigned int mode, unsigned int note_in, unsigned int val) {
  // printf("imitone "BYTE_TO_BINARY_PATTERN" %u %u\n",
  //         BYTE_TO_BINARY(mode), note_in, val);

  if (mode == MIDI_ON) {
    root_note = note_in;
  }
}

void handle_piano(unsigned int mode, unsigned int note_in, unsigned int val) {
  bool is_bass = note_in < 50;

  if (mode == MIDI_ON && is_bass) {
    piano_left_hand_velocity = val;
    int new_root = to_root(note_in);
    if (new_root != root_note) {
      //printf("New root: %d (vel=%d)\n", root_note, val);
      root_note = new_root;
      update_bass();
    }
  }

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
  if (overdriven_rhodes_on) {
    send_midi(mode, note_in, val, ENDPOINT_OVERDRIVEN_RHODES);
  }
  if (rhodes_on) {
    send_midi(mode, note_in, val, ENDPOINT_RHODES);
  }
  if (tbd_a_on) {
    send_midi(mode, note_in, val, ENDPOINT_TBD_A);
  }
  if (tbd_b_on) {
    send_midi(mode, note_in, val, ENDPOINT_TBD_B);
  }
  if (tbd_c_on) {
    send_midi(mode, note_in, val, ENDPOINT_TBD_C);
  }
}

void full_reset() {
  voices_reset();
  all_notes_off(N_ENDPOINTS);
}

void handle_control_helper(unsigned int note_in) {
  switch (note_in) {

  case ALL_NOTES_OFF:
    all_notes_off(N_ENDPOINTS);
    return;

  case FULL_RESET:
    full_reset();
    return;

  case SELECT_SAX_TROMBONE:
    all_notes_off(N_BUTTON_ENDPOINTS);
    sax_on = !sax_on;
    button_endpoint = sax_on ? ENDPOINT_SAX : ENDPOINT_TROMBONE;
    return;

  case TOGGLE_JAWHARP:
    endpoint_notes_off(ENDPOINT_JAWHARP);
    if (piano_on) {
      jawharp_on = !jawharp_on;
      if (jawharp_on) {
        update_bass();
      } else {
        jawharp_off();
      }
    } else {
      button_endpoint = ENDPOINT_JAWHARP;
    }
    return;

  case TOGGLE_SLIDE:
    endpoint_notes_off(ENDPOINT_SLIDE);
    slide_on = !slide_on;
    if (slide_on) {
      update_bass();
    } else {
      slide_off();
    }
    if (!piano_on) {
      button_endpoint = ENDPOINT_SLIDE;
    }
    return;

  case TOGGLE_BASS_TROMBONE:
  case TOGGLE_BT_UP_8:
    endpoint_notes_off(ENDPOINT_TROMBONE);

    if (note_in == TOGGLE_BASS_TROMBONE) {
      bass_trombone_on = !bass_trombone_on;
    } else if (note_in == TOGGLE_BT_UP_8) {
      bass_trombone_up_8 = !bass_trombone_up_8;
    }

    if (bass_trombone_on) {
      update_bass();
    } else {
      bass_trombone_off();
    }
    return;

  case TOGGLE_VBASS_TROMBONE:
  case TOGGLE_VBT_UP_8:
    endpoint_notes_off(ENDPOINT_BASS_TROMBONE);

    if (note_in == TOGGLE_VBASS_TROMBONE) {
      vbass_trombone_on = !vbass_trombone_on;
    } else if (note_in == TOGGLE_VBT_UP_8) {
      vbass_trombone_up_8 = !vbass_trombone_up_8;
    }

    if (vbass_trombone_on) {
      update_bass();
    } else {
      vbass_trombone_off();
    }
    return;

  case TOGGLE_HAMMOND:
    endpoint_notes_off(ENDPOINT_HAMMOND);
    hammond_on = !hammond_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_HAMMOND;
    }
    return;

  case TOGGLE_ORGAN_FLEX:
    endpoint_notes_off(ENDPOINT_ORGAN_FLEX);
    organ_flex_on = !organ_flex_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_ORGAN_FLEX;
    }
    return;

  case TOGGLE_ORGAN_LOW:
    endpoint_notes_off(ENDPOINT_ORGAN_LOW);
    organ_low_on = !organ_low_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_ORGAN_LOW;
    }
    return;

  case TOGGLE_SINE_PAD:
    endpoint_notes_off(ENDPOINT_SINE_PAD);
    sine_pad_on = !sine_pad_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_SINE_PAD;
    }
    return;

  case TOGGLE_OVERDRIVEN_RHODES:
    endpoint_notes_off(ENDPOINT_OVERDRIVEN_RHODES);
    overdriven_rhodes_on = !overdriven_rhodes_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_OVERDRIVEN_RHODES;
    }
    return;

  case TOGGLE_RHODES:
    endpoint_notes_off(ENDPOINT_RHODES);
    rhodes_on = !rhodes_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_RHODES;
    }
    return;

  case TOGGLE_TBD_A:
    endpoint_notes_off(ENDPOINT_TBD_A);
    tbd_a_on = !tbd_a_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_TBD_A;
    }
    return;

  case TOGGLE_TBD_B:
    endpoint_notes_off(ENDPOINT_TBD_B);
    tbd_b_on = !tbd_b_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_TBD_B;
    }
    return;

  case TOGGLE_TBD_C:
    endpoint_notes_off(ENDPOINT_TBD_C);
    tbd_c_on = !tbd_c_on;
    if (!piano_on) {
      button_endpoint = ENDPOINT_TBD_C;
    }
    return;

  case TOGGLE_BREATH_RIDE:
    endpoint_notes_off(ENDPOINT_BREATH_DRUM);
    breath_ride_on = !breath_ride_on;
    return;

  case TOGGLE_BREATH_HIHAT:
    endpoint_notes_off(ENDPOINT_BREATH_DRUM);
    breath_hihat_on = !breath_hihat_on;
    return;
  }


}

void handle_control(unsigned int note_in) {
  handle_control_helper(note_in);
}

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  unsigned char note_out = mapping(note_in) + 12 + 2;
  if (note_in >= BASS_MIN) {
    if (mode == MIDI_ON) {
      root_note = to_root(note_out);
      update_bass();
    }

    if (vbass_trombone_on) {
      send_midi(mode, note_out - (12*4) +
                (vbass_trombone_up_8 ? 12 : 0),
                val, ENDPOINT_BASS_TROMBONE);
    }
    if (bass_trombone_on) {
      send_midi(mode, note_out - (12*3) +
                (bass_trombone_up_8 ? 12 : 0),
                val, ENDPOINT_TROMBONE);
    }

    return;
  }

  // At this point, the signal is telling us to play a button instrument.

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

  if (button_endpoint == ENDPOINT_SLIDE ||
      button_endpoint == ENDPOINT_TBD_A ||
      button_endpoint == ENDPOINT_TBD_B ||
      button_endpoint == ENDPOINT_TBD_C) {
    val = breath;
  } else if (button_endpoint == ENDPOINT_OVERDRIVEN_RHODES ||
             button_endpoint == ENDPOINT_RHODES) {
    val = (breath + MIDI_MAX) / 2;
  } else if (button_endpoint == ENDPOINT_ORGAN_LOW ||
             button_endpoint == ENDPOINT_HAMMOND ||
             button_endpoint == ENDPOINT_ORGAN_FLEX ||
             button_endpoint == ENDPOINT_JAWHARP ||
             button_endpoint == ENDPOINT_SINE_PAD) {
    val = MIDI_MAX;
  }


  send_midi(mode, note_out, val, chosen_endpoint);
}

bool breath_ride_triggered = false;
bool breath_hihat_triggered = false;

int breath_ride_note = 51;
int breath_hihat_note = 42;

int breath_ride_threshold = 20;
int breath_hihat_threshold = 100;

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

  send_midi(MIDI_CC, CC_MOD, val, ENDPOINT_HAMMOND);

  breath = val;

  if (breath_ride_triggered && breath < breath_ride_threshold - 10) {
    send_midi(MIDI_OFF, breath_ride_note, 0, ENDPOINT_BREATH_DRUM);
    breath_ride_triggered = false;
  } else if (breath_ride_on && !breath_ride_triggered &&
             breath > breath_ride_threshold) {
    send_midi(MIDI_ON, breath_ride_note, 50, ENDPOINT_BREATH_DRUM);
    breath_ride_triggered = true;
  }

  if (breath_hihat_triggered && breath < breath_hihat_threshold - 10) {
    send_midi(MIDI_OFF, breath_hihat_note, 0, ENDPOINT_BREATH_DRUM);
    breath_hihat_triggered = false;
  } else if (breath_hihat_on && !breath_hihat_triggered &&
             breath > breath_hihat_threshold) {
    send_midi(MIDI_ON, breath_hihat_note, 100, ENDPOINT_BREATH_DRUM);
    breath_hihat_triggered = true;
  }

  // pass other control change to all synths that care about it:
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (endpoint != ENDPOINT_SAX &&
        endpoint != ENDPOINT_TROMBONE &&
        endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_SLIDE &&
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
    if (endpoint == ENDPOINT_SLIDE) {
      if (breath < 10 &&
          current_note[ENDPOINT_SLIDE] != -1) {
        send_midi(MIDI_OFF, current_note[ENDPOINT_SLIDE], 0,
                  ENDPOINT_SLIDE);
        current_note[ENDPOINT_SLIDE] = -1;
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

void print_status() {
  printf("%s %s %s %s %s %s %s %s %3d %.2f\n",
         (jawharp_on ? "J" : " "),
         (slide_on ? "S" : " "),
         (bass_trombone_on ? "bT" : "  "),
         (bass_trombone_up_8 ? "b8" : "  "),
         (vbass_trombone_on ? "BT" : "  "),
         (vbass_trombone_up_8 ? "v8" : "  "),
         button_endpoint_str(),
         note_str(active_note()),
         breath,
         air);
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

//      if (val > 0) {
//        printf("got packet %u %u %u\n", mode, note_in, val);
//      }

      //unsigned int channel = mode & 0x0F;
      mode = mode & 0xF0;

      if (mode == MIDI_ON && val == 0) {
        mode = MIDI_OFF;
      }

      if (srcConnRefCon == &midiport_piano) {
        handle_piano(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_imitone) {
        handle_imitone(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_axis_49) {
        if (note_in <= LOW_CONTROL_MAX ||
            note_in >= HIGH_CONTROL_MIN) {
          if (mode == MIDI_ON) {
            handle_control(note_in);
          }
        } else {
          handle_button(mode, note_in, val);
        }
      } else if (mode == MIDI_CC) {
        handle_cc(note_in, val);
      } else {
        printf("ignored\n");
      }
      //print_status();
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
    tilt_controller,
    piano_controller,
    imitone_controller;
  if (get_endpoint_ref(CFSTR("AXIS-49 2A"), &axis49)) {
    connect_source(axis49, &midiport_axis_49);
  }
  if (get_endpoint_ref(CFSTR("Breath Controller 5.0-15260BA7"), &breath_controller)) {
    connect_source(breath_controller, &midiport_breath_controller);
  }
  if (get_endpoint_ref(CFSTR("game controller"), &game_controller)) {
    connect_source(game_controller, &midiport_game_controller);
  }
  if (get_endpoint_ref(CFSTR("yocto 3d v2"), &tilt_controller)) {
    connect_source(tilt_controller, &midiport_tilt_controller);
  }
  // The piano at work is "Roland Digital Piano"
  if (get_endpoint_ref(CFSTR("USB MIDI Interface"), &piano_controller)) {
    connect_source(piano_controller, &midiport_piano);
    piano_on = true;
  }
  if (get_endpoint_ref(CFSTR("imitone"), &imitone_controller)) {
    connect_source(imitone_controller, &midiport_imitone);
  }

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }

  create_source(&endpoints[ENDPOINT_SAX],               CFSTR("jammer-sax"));
  create_source(&endpoints[ENDPOINT_TROMBONE],          CFSTR("jammer-trombone"));
  create_source(&endpoints[ENDPOINT_JAWHARP],           CFSTR("jammer-jawharp"));
  create_source(&endpoints[ENDPOINT_SLIDE],             CFSTR("jammer-slide"));
  create_source(&endpoints[ENDPOINT_BASS_SAX],          CFSTR("jammer-bass-sax"));
  create_source(&endpoints[ENDPOINT_BASS_TROMBONE],     CFSTR("jammer-bass-trombone"));
  create_source(&endpoints[ENDPOINT_HAMMOND],           CFSTR("jammer-hammond"));
  create_source(&endpoints[ENDPOINT_ORGAN_LOW],         CFSTR("jammer-organ-low"));
  create_source(&endpoints[ENDPOINT_ORGAN_FLEX],        CFSTR("jammer-organ-flex"));
  create_source(&endpoints[ENDPOINT_SINE_PAD],          CFSTR("jammer-sine-pad"));
  create_source(&endpoints[ENDPOINT_OVERDRIVEN_RHODES], CFSTR("jammer-overdriven-rhodes"));
  create_source(&endpoints[ENDPOINT_RHODES],            CFSTR("jammer-rhodes"));
  create_source(&endpoints[ENDPOINT_TBD_A],             CFSTR("jammer-tbd-a"));
  create_source(&endpoints[ENDPOINT_TBD_B],             CFSTR("jammer-tbd-b"));
  create_source(&endpoints[ENDPOINT_TBD_C],             CFSTR("jammer-tbd-c"));
  create_source(&endpoints[ENDPOINT_BREATH_DRUM],       CFSTR("jammer-drum"));
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
  if (val > MIDI_MAX) {
    val = MIDI_MAX;
  }
  organ_flex_base = val;
  int organ_flex_value = organ_flex_val();

  if (!piano_on) {
    val = organ_flex_value;
  }

  if (val != last_air_val) {
    send_midi(MIDI_CC, CC_11, val, ENDPOINT_ORGAN_LOW);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_HAMMOND);
    send_midi(MIDI_CC, CC_11, val, ENDPOINT_SINE_PAD);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_OVERDRIVEN_RHODES);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_RHODES);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_TBD_A);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_TBD_B);
    send_midi(MIDI_CC, CC_07, val, ENDPOINT_TBD_C);
    last_air_val = val;
  }
  if (organ_flex_value != last_organ_flex_val) {
    send_midi(MIDI_CC, CC_11, organ_flex_value, ENDPOINT_ORGAN_FLEX);
    last_organ_flex_val = organ_flex_value;
  }
}

void jml_tick() {
  // Called every TICK_MS
  update_air();
  forward_air();
}

#endif
