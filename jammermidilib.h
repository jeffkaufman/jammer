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
  P  ST  J   bw  bT  Dl  Fl
                       Dh  Fh
  ...

  O

Which is:
 92 P   Piano
 93 ST  Sax vs Trombone
 94  J  Jawharp
 95 bw  Bass sax
 96 bT  Bass Trombone
 97 Dl  Drum low
 99 Fl  Footbass low

 89 Bt  Very bass trombone
 90 Dh  Drum high
 91 Fh  Footbass high

 3  fl  Feet louder
 2  fq  Feet quieter
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
#define FEET_QUIETER           2
#define FEET_LOUDER            3
#define LOW_CONTROL_MAX        FEET_LOUDER

#define TOGGLE_PIANO           92
#define SELECT_SAX_TROMBONE    93
#define TOGGLE_JAWHARP         94
#define TOGGLE_BASS_SAX        95
#define TOGGLE_BASS_TROMBONE   96
#define TOGGLE_DRUM_LOW        97
#define TOGGLE_FOOTBASS_LOW    98

#define TOGGLE_VBASS_TROMBONE  89
#define TOGGLE_DRUM_HIGH       90
#define TOGGLE_FOOTBASS_HIGH   91
#define HIGH_CONTROL_MIN       TOGGLE_VBASS_TROMBONE

#define BASS_MIN               71

/* endpoints */
#define ENDPOINT_SAX 0
#define ENDPOINT_TROMBONE 1
#define N_BUTTON_ENDPOINTS (ENDPOINT_TROMBONE + 1)
#define ENDPOINT_PIANO 2
#define ENDPOINT_DRUM_LOW 3
#define ENDPOINT_DRUM_HIGH 4
#define ENDPOINT_FOOTBASS 5
#define ENDPOINT_JAWHARP 6
#define ENDPOINT_BASS_SAX 7
#define ENDPOINT_BASS_TROMBONE 8
#define N_ENDPOINTS (ENDPOINT_BASS_TROMBONE+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0

#define CC_BREATH 0x02
#define CC_07 0x07
#define CC_11 0x0b

// gcmidi sends on CC 20 through 29
#define GCMIDI_MIN 20
#define GCMIDI_MAX 29

#define CC_ROLL 30
#define CC_PITCH 31

#define MIDI_DRUM_LOW 36  // Acoustic Bass Drum
#define MIDI_DRUM_HIGH 42  // Closed Hi-Hat

#define MIDI_MAX 127

#define LIGHT_UDP_PORT 23512

#define FOOT_MAX_VOLUME 10  // 0 - max, inclusive

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;
MIDIPortRef midiport_breath_controller;
MIDIPortRef midiport_game_controller;
MIDIPortRef midiport_tilt_controller;
MIDIPortRef midiport_feet_controller;
MIDIPortRef midiport_piano;

bool tilt_on = false;
bool jawharp_on = false;
bool bass_trombone_on = false;
bool vbass_trombone_on = false;
bool footbass_low_on = false;
bool footbass_high_on = false;
bool bass_sax_on = false;
bool drum_low_on = false;
bool drum_high_on = false;
bool piano_on = false;
int foot_volume = FOOT_MAX_VOLUME / 2;

int button_endpoint = ENDPOINT_SAX;

int root_note = 26;  // D @ 37Hz

// Only some endpoints use this, and some only use it some of the time:
//  * Always in use for jawharp
//  * Not in use for drums or footbass
int current_note[N_ENDPOINTS];

// Footbass needs to track two notes, because each pedal stays on either until
// we receive MIDI_OFF for it or we get a new MIDI_ON from it.
int footbass_low_note = -1;
int footbass_high_note = -1;

int piano_left_hand_velocity = 100;  // most recent piano bass midi velocity

int roll = MIDI_MAX / 2;
int pitch = MIDI_MAX / 2;

int breath = 0;

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

void footbass_off() {
  if (footbass_low_note != -1) {
    send_midi(MIDI_OFF, footbass_low_note, 0, ENDPOINT_FOOTBASS);
    footbass_low_note = -1;
  }
  if (footbass_high_note != -1) {
    send_midi(MIDI_OFF, footbass_high_note, 0, ENDPOINT_FOOTBASS);
    footbass_high_note = -1;
  }
}

void jawharp_off() {
  if (current_note[ENDPOINT_JAWHARP] != -1) {
    send_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = -1;
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

void update_bass() {
  if (breath < 3) return;

  int note_out = active_note();
  if (jawharp_on && current_note[ENDPOINT_JAWHARP] != note_out) {
    jawharp_off();
    send_midi(MIDI_ON, note_out, MIDI_MAX, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = note_out;
  }
  int trombone_note = note_out + 12;
  if (trombone_note < 40) {
    trombone_note += 12;
  }
  if (piano_on && bass_trombone_on && current_note[ENDPOINT_TROMBONE] != trombone_note) {
    bass_trombone_off();
    send_midi(MIDI_ON, trombone_note, piano_left_hand_velocity, ENDPOINT_TROMBONE);
    current_note[ENDPOINT_TROMBONE] = trombone_note;
  }
  int bass_trombone_note = trombone_note - 12;
  if (bass_trombone_note < 32) {
    bass_trombone_note += 12;
  }
  if (piano_on && vbass_trombone_on && current_note[ENDPOINT_BASS_TROMBONE] != bass_trombone_note) {
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

    printf("Saw %s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8));

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

void handle_piano(unsigned int mode, unsigned int note_in, unsigned int val) {
  bool is_bass = note_in < 51;

  if (mode == MIDI_ON && is_bass) {
    piano_left_hand_velocity = val;
    int new_root = to_root(note_in);
    if (new_root != root_note) {
      //printf("New root: %d (vel=%d)\n", root_note, val);
      root_note = new_root;
      update_bass();
    }
  }
  bool bass_claimed = false;
  if ((bass_sax_on && is_bass) || mode == MIDI_OFF) {
    bass_claimed = true;
    int bass_note = note_in + 24; // intentionally shifting this left on the keyboard
    send_midi(mode, bass_note, val, ENDPOINT_BASS_SAX);
  }
  if ((piano_on && !bass_claimed) || mode == MIDI_OFF) {
    int piano_note = note_in + 12;  // using a patch that's confused about location
    send_midi(mode, piano_note, val, ENDPOINT_PIANO);
  }
}

#define LIGHT_PIANO          0
#define LIGHT_BUTTONS        1
#define LIGHT_BASS_SAX       2
#define LIGHT_BASS_TROMBONE  3
#define LIGHT_VBASS_TROMBONE 4
#define LIGHT_JAWHARP        5
#define LIGHT_FOOT_LOW       6
#define LIGHT_FOOT_HIGH      7
#define N_LIGHTS 8

#define COLOR_QUANTUM 10
#define COLOR_OFF (struct Color){0, 0, 0}
#define COLOR_RED (struct Color){COLOR_QUANTUM, 0, 0}
#define COLOR_BLUE (struct Color){0, 0, COLOR_QUANTUM}
#define COLOR_GREEN (struct Color){0, COLOR_QUANTUM, 0}
#define COLOR_PURPLE (struct Color){COLOR_QUANTUM, 0, COLOR_QUANTUM}
#define COLOR_YELLOW (struct Color){COLOR_QUANTUM, COLOR_QUANTUM, 0}
#define COLOR_WHITE (struct Color){COLOR_QUANTUM, COLOR_QUANTUM, COLOR_QUANTUM}
#define COLOR_LOW (struct Color){1, 1, 1}

#define COLOR_PIANO COLOR_WHITE
#define COLOR_JAWHARP COLOR_GREEN
#define COLOR_TROMBONE COLOR_YELLOW
#define COLOR_SAX COLOR_RED
#define COLOR_DRUM COLOR_BLUE
#define COLOR_DRUM COLOR_BLUE
#define COLOR_FOOTBASS COLOR_RED
#define COLOR_FOOTBASS_DRUM COLOR_PURPLE


struct Color {
  unsigned char r, g, b;
};

unsigned char light_buf[4];
int light_fd = -1;

void set_light(unsigned char index, struct Color color) {
  // See https://github.com/jeffkaufman/blinksticknet
  if (!LIGHT_UDP_PORT || light_fd < 0) return;

  light_buf[0] = index;
  light_buf[1] = color.r;
  light_buf[2] = color.g;
  light_buf[3] = color.b;

  send(light_fd, light_buf, sizeof(light_buf), /*flags=*/0);
}

void update_lights(int control) {
  unsigned char index = 0;
  struct Color color = {0, 0, 0};

  bool low = (control == TOGGLE_FOOTBASS_LOW ||
              control == TOGGLE_DRUM_LOW);
  int footbass_on = low ? footbass_low_on : footbass_high_on;
  int drum_on = low ? drum_low_on : drum_high_on;

  switch (control) {
  case SELECT_SAX_TROMBONE:
    index = LIGHT_BUTTONS;
    color = (button_endpoint == ENDPOINT_SAX) ? COLOR_SAX : COLOR_TROMBONE;
    break;
  case TOGGLE_JAWHARP:
    index = LIGHT_JAWHARP;
    color = jawharp_on ? COLOR_JAWHARP : COLOR_OFF;
    break;
  case TOGGLE_BASS_TROMBONE:
    index = LIGHT_BASS_TROMBONE;
    color = bass_trombone_on ? COLOR_TROMBONE : COLOR_OFF;
    break;
  case TOGGLE_VBASS_TROMBONE:
    index = LIGHT_VBASS_TROMBONE;
    color = vbass_trombone_on ? COLOR_TROMBONE : COLOR_OFF;
    break;
  case TOGGLE_BASS_SAX:
    index = LIGHT_BASS_SAX;
    color = bass_sax_on ? COLOR_SAX : COLOR_OFF;
    break;
  case TOGGLE_PIANO:
    index = LIGHT_PIANO;
    color = piano_on ? COLOR_PIANO : COLOR_OFF;
    break;
  case TOGGLE_FOOTBASS_LOW:
  case TOGGLE_DRUM_LOW:
  case TOGGLE_FOOTBASS_HIGH:
  case TOGGLE_DRUM_HIGH:
    index = low ? LIGHT_FOOT_LOW : LIGHT_FOOT_HIGH;
    if (footbass_on && drum_on) {
      color = COLOR_FOOTBASS_DRUM;
    } else if (footbass_on) {
      color = COLOR_FOOTBASS;
    } else if (drum_on) {
      color = COLOR_DRUM;
    } else {
      color = COLOR_OFF;
    }
    break;
  default:
    return;
  }
  set_light(index, color);
}

void handle_control_helper(unsigned int note_in) {
  switch (note_in) {
    
  case ALL_NOTES_OFF:
    all_notes_off(N_ENDPOINTS);
    return;

  case FEET_QUIETER:
    if (foot_volume > 0) {
      foot_volume--;
    }
    return;
  
  case FEET_LOUDER:
    if (foot_volume < FOOT_MAX_VOLUME) {
      foot_volume++;
    }
    return;    

  case SELECT_SAX_TROMBONE:
    all_notes_off(N_BUTTON_ENDPOINTS);
    button_endpoint = (button_endpoint == ENDPOINT_SAX) ? ENDPOINT_TROMBONE : ENDPOINT_SAX;
    return;
    
  case TOGGLE_JAWHARP:
    endpoint_notes_off(ENDPOINT_JAWHARP);
    jawharp_on = !jawharp_on;
    if (jawharp_on) {
      update_bass();
    } else {
      jawharp_off();
    }
    return;
    
  case TOGGLE_BASS_TROMBONE:
    endpoint_notes_off(ENDPOINT_TROMBONE);
    bass_trombone_on = !bass_trombone_on;
    if (bass_trombone_on) {
      update_bass();
    } else {
      bass_trombone_off();
    }
    return;
    
  case TOGGLE_VBASS_TROMBONE:
    endpoint_notes_off(ENDPOINT_BASS_TROMBONE);
    vbass_trombone_on = !vbass_trombone_on;
    if (vbass_trombone_on) {
      update_bass();
    } else {
      vbass_trombone_off();
    }
    return;
    
  case TOGGLE_FOOTBASS_LOW:
    footbass_off();
    footbass_low_on = !footbass_low_on;
    return;
    
  case TOGGLE_FOOTBASS_HIGH:
    footbass_off();
    footbass_high_on = !footbass_high_on;
    return;
    
  case TOGGLE_BASS_SAX:
    bass_sax_on = !bass_sax_on;
    return;
    
  case TOGGLE_DRUM_LOW:
    drum_low_on = !drum_low_on;
    return;
    
  case TOGGLE_DRUM_HIGH:
    drum_high_on = !drum_high_on;
    return;
    
  case TOGGLE_PIANO:
    piano_on = !piano_on;
    return;
  }
}

void handle_control(unsigned int note_in) {
  handle_control_helper(note_in);
  update_lights(note_in);
}

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  unsigned char note_out = mapping(note_in) + 12 + 2;
  if (note_in >= BASS_MIN) {
    if (mode == MIDI_ON) {
      root_note = to_root(note_out);
      update_bass();
    }

    if (!piano_on) {
      if (bass_sax_on) {
        send_midi(mode, note_out - (12*3), val, ENDPOINT_BASS_SAX);
      }
      if (vbass_trombone_on) {
        send_midi(mode, note_out - (12*4), val, ENDPOINT_BASS_TROMBONE);
      }
      if (bass_trombone_on) {
        send_midi(mode, note_out - (12*3) + 7, val, ENDPOINT_TROMBONE);
      }
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
  } else {
    if (note_out < 40) {
      chosen_endpoint = ENDPOINT_BASS_TROMBONE;
    }
  }

  send_midi(mode, note_out, val, chosen_endpoint);
}


void handle_feet(unsigned int mode, unsigned int note_in, unsigned int val) {
  bool is_low = note_in == MIDI_DRUM_LOW;
  bool is_midi_on = mode == MIDI_ON;
  int drum_endpoint = is_low ? ENDPOINT_DRUM_LOW : ENDPOINT_DRUM_HIGH;

  // val is entirely ignored, replaced with a button-controlled volume
  val = 127 * foot_volume / FOOT_MAX_VOLUME;

  if (!is_midi_on ||
      is_low ? drum_low_on : drum_high_on) {
    int drum_val = val;
    send_midi(mode, note_in, drum_val, drum_endpoint);
  }

  int* footbass_note;
  int* other_footbass_note;
  if (footbass_low_on && is_low) {
    footbass_note = &footbass_low_note;
    other_footbass_note = &footbass_high_note;
  } else if (footbass_high_on && !is_low) {
    footbass_note = &footbass_high_note;
    other_footbass_note = &footbass_low_note;
  } else {
    return;
  }

  int note_out = active_note();
  if (!is_low) {
    note_out += 12;
    val -= 10;
  }

  if (*footbass_note != -1) {
    send_midi(MIDI_OFF, *footbass_note, 0, ENDPOINT_FOOTBASS);
    *footbass_note = -1;
  }
  if (mode == MIDI_ON) {
    send_midi(MIDI_ON, note_out, val, ENDPOINT_FOOTBASS);
    *footbass_note = note_out;
    if (*footbass_note == *other_footbass_note) {
      // take ownership of this note, so the MIDI_OFF from the other bass
      // doesn't end this note early.
      *other_footbass_note = -1;
    }
  }
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc >= GCMIDI_MIN && cc <= GCMIDI_MAX) {
    send_midi(MIDI_CC, cc, val, ENDPOINT_SAX);
    send_midi(MIDI_CC, cc, val, ENDPOINT_BASS_SAX);
    return;
  }

  if (cc != CC_BREATH && cc != CC_11) {
    printf("Unknown Control change %d\n", cc);
    return;
  }

  breath = val;

  // pass other control change to all synths that care about it:
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    if (endpoint != ENDPOINT_SAX &&
        endpoint != ENDPOINT_TROMBONE &&
        endpoint != ENDPOINT_JAWHARP &&
        endpoint != ENDPOINT_BASS_SAX &&
        endpoint != ENDPOINT_BASS_TROMBONE) {
      continue;
    }
    int use_val = val;
    if (use_val > MIDI_MAX) {
      use_val = MIDI_MAX;
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
    if (piano_on &&
        ((bass_trombone_on && endpoint == ENDPOINT_TROMBONE) ||
         (vbass_trombone_on && endpoint == ENDPOINT_BASS_TROMBONE))) {
      if (breath < 2 &&
          current_note[endpoint] != -1) {
        send_midi(MIDI_OFF, current_note[endpoint], 0, endpoint);
        current_note[endpoint] = -1;
      } else if (breath > 3) {
        update_bass();
      }
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
  printf("%s %s %s %s %s %s %s %s %s %s %s %3d\n",
         (jawharp_on ? "J" : " "),
         (footbass_low_on ? "f" : " "),
         (footbass_high_on ? "fh" : "  "),
         (drum_low_on ? "dl" : "  "),
         (drum_high_on ? "dh" : "  "),
         (piano_on ? "P" : " "),
         (bass_sax_on ? "bw" : "  "),
         (bass_trombone_on ? "bT" : "  "),
         (vbass_trombone_on ? "BT" : "  "),
         button_endpoint_str(),
         note_str(active_note()),
         breath);
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

      //if (val > 0) {
      //  printf("got packet %u %u %u\n", mode, note_in, val);
      //}

      //unsigned int channel = mode & 0x0F;
      mode = mode & 0xF0;

      if (mode == MIDI_ON && val == 0) {
        mode = MIDI_OFF;
      }

      if (srcConnRefCon == &midiport_piano) {
        handle_piano(mode, note_in, val);
      } else if (srcConnRefCon == &midiport_axis_49) {
        if (note_in <= LOW_CONTROL_MAX || note_in >= HIGH_CONTROL_MIN) {
          if (mode == MIDI_ON) {
            handle_control(note_in);
          }
        } else {
          handle_button(mode, note_in, val);
        }
      } else if (srcConnRefCon == &midiport_feet_controller) {
        handle_feet(mode, note_in, val);
      } else if (mode == MIDI_CC) {
        handle_cc(note_in, val);
      } else {
        printf("ignored\n");
      }
      print_status();
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

void jml_setup() {
  light_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (light_fd < 0) {
    perror("couldn't create light udp socket");
  } else {
    struct sockaddr_in  address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_port = htons(LIGHT_UDP_PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (connect(light_fd,
                (const struct sockaddr*)&address,
                sizeof(address)) < 0) {
      perror("couldn't connect light udp socket");
      light_fd = -1;
    }
  }

  for (int i = 0; i < N_LIGHTS; i++) {
    set_light(i, COLOR_WHITE);
  }
  for (int i = 0; i < N_LIGHTS; i++) {
    set_light(i, COLOR_OFF);
  }

  attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &midiclient),
    "creating OS-X MIDI client object." );

  MIDIEndpointRef axis49,
    breath_controller,
    game_controller,
    tilt_controller,
    feet_controller,
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
  if (get_endpoint_ref(CFSTR("yocto 3d v2"), &tilt_controller)) {
    connect_source(tilt_controller, &midiport_tilt_controller);
  }
  if (get_endpoint_ref(CFSTR("mio"), &feet_controller)) {
    connect_source(feet_controller, &midiport_feet_controller);
    handle_control(TOGGLE_DRUM_LOW);
    handle_control(TOGGLE_DRUM_HIGH);
  }
  // The piano at work is "Roland Digital Piano"
  if (get_endpoint_ref(CFSTR("USB MIDI Interface"), &piano_controller)) {
    connect_source(piano_controller, &midiport_piano);
    handle_control(TOGGLE_PIANO);
  }

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }

  create_source(&endpoints[ENDPOINT_SAX],            CFSTR("jammer-sax"));
  create_source(&endpoints[ENDPOINT_TROMBONE],       CFSTR("jammer-trombone"));
  create_source(&endpoints[ENDPOINT_PIANO],          CFSTR("jammer-piano"));
  create_source(&endpoints[ENDPOINT_FOOTBASS],       CFSTR("jammer-footbass"));
  create_source(&endpoints[ENDPOINT_DRUM_LOW],       CFSTR("jammer-drum-low"));
  create_source(&endpoints[ENDPOINT_DRUM_HIGH],      CFSTR("jammer-drum-high"));
  create_source(&endpoints[ENDPOINT_JAWHARP],        CFSTR("jammer-jawharp"));
  create_source(&endpoints[ENDPOINT_BASS_SAX],       CFSTR("jammer-bass-sax"));
  create_source(&endpoints[ENDPOINT_BASS_TROMBONE],  CFSTR("jammer-bass-trombone"));

  // toggle to trombone and then back to sax so the lights are right
  handle_control(SELECT_SAX_TROMBONE);
  handle_control(SELECT_SAX_TROMBONE);
}

#endif
