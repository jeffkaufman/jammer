#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2


/*
 Thinking about different modes I'm likely to want:

 1. Accordion:

    intensity: breath
    note: buttons (optionally as radio)
    attack: none

 2. Brass (Sax / Trombone):

    intensity: breath
    note: buttons
    attack: button velocity

 3. Jaw Harp

    intensity: breath
    note: tilt
    attack: none

 4. Drums

    intensity: none
    note: pedals
    attack: pedal velocity

 5. Footbass

    intensity: none
    note: tilt
    attack: pedal velocity

 6. Keyboard

    intensity: none
    note: buttons
    attack: button velocity

 7. Lead

    intensity: none
    note: buttons (as radio)
    attack: none

 Plus playing Hands (Mandolin or Piano)

 Combinations:

 - Jaw Harp with anything
   - Combining with Accordion or Brass is logically possible, but probably
     sounds weird.  Interface doesn't need to prevent it though.
 - Drums with anything
   - Don't necessarily want all three drums enabled at once, so need to be able
     to toggle them individually.
 - Footbass with anything

 Interface:
  - Toggle: Jaw harp toggle
  - Toggle: Drums - Low (Kick)
  - Toggle: Drums - High (Hihat etc)
  - Toggle: Drums - Special (Snare etc)
  - Toggle: Footbass
  - Buttons selector (includes all notes off for covered endpoints)
    - Accordion
    - Accordion radio
    - Sax
    - Trombone
    - Keyboard
    - Lead
  - Root note (key) selector
    - where is Footbass / Jaw Harp based?
  - Scale selector
    - Major
    - Mixolydian
    - Dorian
    - Minor
  - Absolutely all notes off

This looks like:

  Mj   Mx   Dr   Mn
     Rs        J    F   Dl   Dh   Ds
  O    A    Ar   S    T    K    L

Which is:

  1 O   All notes off
  2 A   Accordion
  3 Ar  Accordion Radio
  4 S   Sax
  5 T   Trombone
  6 K   Keyboard
  7 L   Lead

  8 Rs  Root select
  9
 10 J   Jawharp (toggle)
 11 F   Footbass (toggle)
 12 Dl  Drum Low (toggle)
 13 Dh  Drum High (toggle)
 14 Ds  Drum Special (toggle)

 15 Mj  Major scale
 16 Dr  Dorian scale
 17 Mx  Mixolydian scale
 18 Mn  Minor scale
 19
 20
 21

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
#define ALL_NOTES_OFF        1

#define SELECT_ACCORDION     2
#define SELECT_ACCORDION_R   3
#define SELECT_SAX           4
#define SELECT_TROMBONE      5
#define SELECT_KEYBOARD      6
#define SELECT_LEAD          7

#define SELECT_ROOT          8
#define TOGGLE_JAWHARP      10
#define TOGGLE_FOOTBASS     11
#define TOGGLE_DRUM_LOW     12
#define TOGGLE_DRUM_HIGH    13
#define TOGGLE_DRUM_SPECIAL 14

#define SELECT_MAJOR         15
#define SELECT_MIXOLYDIAN    16
#define SELECT_DORIAN        17
#define SELECT_MINOR         18

#define N_CONTROLS (SELECT_MINOR+1)

/* endpoints */
#define ENDPOINT_ACCORDION 0
#define ENDPOINT_SAX 1
#define ENDPOINT_TROMBONE 2
#define ENDPOINT_KEYBOARD 3
#define ENDPOINT_LEAD 4
#define N_BUTTON_ENDPOINTS (ENDPOINT_LEAD + 1)
#define ENDPOINT_DRUM 5
#define ENDPOINT_FOOTBASS 6
#define ENDPOINT_JAWHARP 7
#define N_ENDPOINTS (ENDPOINT_JAWHARP+1)

/* modes */
// These need to be in the same order as the selectors, and start from zero.
#define MODE_MAJOR 0
#define MODE_MIXO 1
#define MODE_MINOR 2
#define MODE_DORIAN 3
#define N_MODES (MODE_DORIAN+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0

#define CC_BREATH 0x02
#define CC_11 0x0b

#define CC_TILT1 30
#define CC_TILT2 31

#define MIDI_DRUM_LOW 35  // Acoustic Bass Drum
#define MIDI_DRUM_HIGH 38  // Acoustic Snare
#define MIDI_DRUM_SPECIAL 49  // Crash Cymbal 1

#define MIDI_MAX 127

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;
MIDIPortRef midiport_breath_controller;
MIDIPortRef midiport_game_controller;
MIDIPortRef midiport_tilt_controller;
MIDIPortRef midiport_feet_controller;


bool jawharp_on = false;
bool footbass_on = false;
bool drum_low_on = false;
bool drum_high_on = false;
bool drum_special_on = false;

int button_endpoint = ENDPOINT_ACCORDION;
bool radio_buttons = false;

int musical_mode = MODE_MAJOR;
int degree = 1;
int root_note = 26;  // D @ 37Hz
bool selecting_root = false;

// Only some endpoints use this, and some only use it some of the time:
//  * Always in use for jawharp
//  * In use for button endpoints if radio_buttons == true
//  * Not in use for drums or footbass
int current_note[N_ENDPOINTS];

int raw_tilt1 = MIDI_MAX / 2;
int raw_tilt2 = MIDI_MAX / 2;



int choose_degree() {
  /*
   *       30-    15-   0    15+   30+
   *     +-----------------------------
   * +15 |  3     4     5     6    7
   *   0 |  6     7     1     2    3
   *
   *
   * Everything is really an octave up, though, except for the initial 6 and 7,
   * so this is really:
   *
   *       30-    15-   0    15+   30+
   *     +-----------------------------
   * +15 | 10    11    12    13    14
   *   0 |  6     7     8     9    10
   */

  // convert tilts from 0 ... MIDI_MAX to degrees between -180 and 180
  float tilt1 = (raw_tilt1 - (MIDI_MAX / 2)) * 360.0 / MIDI_MAX;
  float tilt2 = (raw_tilt2 - (MIDI_MAX / 2)) * 360.0 / MIDI_MAX;

  int chosen_degree;

  if (tilt1 > 45) {
    chosen_degree = 6;
  } else if (tilt1 > 10) {
    chosen_degree = 7;
  } else if (tilt1 > -10) {
    chosen_degree = 8;
  } else if (tilt1 > -45) {
    chosen_degree = 9;
  } else {
    chosen_degree = 10;
  }

  if (tilt2 > 15) {
    chosen_degree += 4;
  }

  //printf("Degree: %d\n", chosen_degree);
  return chosen_degree;
}

char scale_degree() {
  // return the adjustment needed for the degree-th note in the musical_mode scale
  //
  // For example, if we're in major and degree is 3, then produce 4 (four half
  // steps from C -> E).  If we're in minor, this would be 3 (three half steps
  // from C -> Eb).

  // Degree should be between 1 and 7 inclusive

  // Modes just depend on which scale degree you start on:
  //
  //   1st: major        T–T–S–T–T–T–S  0  2  4  5  7  9 11   zero flats
  //   5th: mixolydian   T–T–S–T–T–S–T  0  2  4  5  7  9 10   one flat
  //   2nd: dorian       T–S–T–T–T–S–T  0  2  3  5  7  9 10   two flats
  //   6th: minor        T–S–T–T–S–T–T  0  2  3  5  7  8 10   three flats

  switch ((degree-1) % 7 + 1) {
  case 1:
    return 0;
  case 2:
    return 2;
  case 3:
    if (musical_mode == MODE_MAJOR ||
        musical_mode == MODE_MIXO) {
      return 4;
    } else {
      return 3;
    }
  case 4:
    return 5;
  case 5:
    return 7;
  case 6:
    return musical_mode == MODE_MINOR ? 8 : 9;
  case 7:
    return (musical_mode == MODE_MAJOR ? 11 : 10);
  }
  printf("bad degree %d\n", degree);
  return 0;
}

char scale_octave() {
  return ((degree-1) / 7) * 12;
}

char active_note() {
  printf("root: %d, degree: %d, scale_degree: %d, octave: %d, delta: %d\n",
         root_note, degree, scale_degree(), scale_octave(), root_note +
         scale_degree() + scale_octave());

  return root_note + scale_degree() + scale_octave();
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

void degree_changed() {
  if (jawharp_on) {
    if (current_note[ENDPOINT_JAWHARP] != -1) {
      send_midi(MIDI_OFF, current_note[ENDPOINT_JAWHARP], 0, ENDPOINT_JAWHARP);
    }
    int note_out = active_note();
    send_midi(MIDI_ON, note_out, MIDI_MAX, ENDPOINT_JAWHARP);
    current_note[ENDPOINT_JAWHARP] = note_out;
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

      printf("got packet %u %u %u\n", mode, note_in, val);

      if (mode == MIDI_ON && val == 0) {
        mode = MIDI_OFF;
      }

      if (mode == MIDI_ON) {
        switch (note_in) {

        case ALL_NOTES_OFF:
          all_notes_off(N_ENDPOINTS);
          continue;

        case SELECT_ACCORDION:
        case SELECT_ACCORDION_R:
        case SELECT_SAX:
        case SELECT_TROMBONE:
        case SELECT_KEYBOARD:
        case SELECT_LEAD:
          all_notes_off(N_BUTTON_ENDPOINTS);

          radio_buttons = (note_in == SELECT_ACCORDION_R ||
                           note_in == SELECT_LEAD);

          if (note_in == SELECT_ACCORDION ||
              note_in == SELECT_ACCORDION_R) {
            button_endpoint = ENDPOINT_ACCORDION;
          } else if (note_in == SELECT_SAX) {
            button_endpoint = ENDPOINT_SAX;
          } else if (note_in == SELECT_TROMBONE) {
            button_endpoint = ENDPOINT_TROMBONE;
          } else if (note_in == SELECT_KEYBOARD) {
            button_endpoint = ENDPOINT_KEYBOARD;
          } else if (note_in == SELECT_LEAD) {
            button_endpoint = ENDPOINT_LEAD;
          }
          continue;

        case SELECT_ROOT:
          printf("selecting root\n");
          selecting_root = true;
          continue;

        case TOGGLE_JAWHARP:
          endpoint_notes_off(ENDPOINT_JAWHARP);
          jawharp_on = !jawharp_on;
          degree_changed();
          continue;

        case TOGGLE_FOOTBASS:
          endpoint_notes_off(ENDPOINT_FOOTBASS);
          footbass_on = !footbass_on;
          continue;

        case TOGGLE_DRUM_LOW:
          drum_low_on = !drum_low_on;
          continue;

        case TOGGLE_DRUM_HIGH:
          drum_high_on = !drum_high_on;
          continue;

        case TOGGLE_DRUM_SPECIAL:
          drum_special_on = !drum_special_on;
          continue;

        case SELECT_MAJOR:
        case SELECT_MIXOLYDIAN:
        case SELECT_DORIAN:
        case SELECT_MINOR:
          musical_mode = note_in - SELECT_MAJOR;
          printf("mode: %d\n", musical_mode);
          continue;
        }
      }

      if ((mode == MIDI_ON || mode == MIDI_OFF) && true /* and not pedals */) {
        if (note_in < N_CONTROLS) {
          continue;
        }

        unsigned char note_out = mapping(note_in) + 12 + 2;

        if (selecting_root) {
          selecting_root = false;
          root_note = note_out - (12*3);
          printf("selected %d", root_note);
          degree_changed();
          // There will also be a corresponding MIDI_OFF but we don't care.
          continue;
        }

        // At this point, the signal is telling us to play a button instrument.

        if (button_endpoint == ENDPOINT_SAX) {
          // sax sounds an octave lower than we want
          note_out += 12;
        } else if (button_endpoint == ENDPOINT_ACCORDION) {
          // accordion always uses full volume, and is adjusted via the breath
          // controller.
          val = MIDI_MAX;
        }

        if (radio_buttons) {
          if (mode == MIDI_ON) {
            if (current_note[button_endpoint] != -1) {
              send_midi(MIDI_OFF, current_note[button_endpoint], 0, button_endpoint);
            }
            current_note[button_endpoint] = note_out;
            send_midi(MIDI_ON, note_out, val, button_endpoint);
          } else if (mode == MIDI_OFF) {
            // ignore
          }
        } else {
          send_midi(mode, note_out, val, button_endpoint);
        }
      } else if (false /* pedals */) {
        if (mode == MIDI_OFF ||
            (drum_low_on && note_in == MIDI_DRUM_LOW) ||
            (drum_high_on && note_in == MIDI_DRUM_HIGH) ||
            (drum_special_on && note_in == MIDI_DRUM_SPECIAL)) {
          send_midi(mode, note_in, val, ENDPOINT_DRUM);
        }

        if (footbass_on) {
          if (current_note[ENDPOINT_FOOTBASS] != -1) {
            send_midi(MIDI_OFF, current_note[ENDPOINT_FOOTBASS], 0,
                      ENDPOINT_FOOTBASS);
            current_note[ENDPOINT_FOOTBASS] = -1;
          }

          int note_out = active_note();
          if (mode == MIDI_ON &&
              (note_in == MIDI_DRUM_LOW || note_in == MIDI_DRUM_HIGH)) {
            if (note_in == MIDI_DRUM_HIGH) {
              note_out += 12;
            }
            send_midi(MIDI_ON, note_out, val, ENDPOINT_FOOTBASS);
            current_note[ENDPOINT_FOOTBASS] = note_out;
          }
        }
      } else if (mode == MIDI_CC &&
                 (note_in == CC_TILT1 || note_in == CC_TILT2)) {
        if (note_in == CC_TILT1) {
          raw_tilt1 = val;
        } else {
          raw_tilt2 = val;
        }
        int chosen_degree = choose_degree();
        if (chosen_degree != degree) {
          degree = chosen_degree;
          degree_changed();
        }
      } else if (mode == MIDI_CC) {
        // pass other control change to all synths that care about it:
        //  - accordion
        //  - sax
        //  - trombone
        //  - jawharp
        for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
          if (endpoint != ENDPOINT_ACCORDION &&
              endpoint != ENDPOINT_SAX &&
              endpoint != ENDPOINT_TROMBONE &&
              endpoint != ENDPOINT_JAWHARP) {
            continue;
          }

          if (note_in != CC_BREATH && note_in != CC_11) {
            printf("Unknown Control change %d\n", note_in);
            continue;
          }

          if (endpoint == ENDPOINT_ACCORDION && !radio_buttons) {
            val += 18;
          } else if (endpoint == ENDPOINT_JAWHARP) {
            val += 36;
          }

          if (val > MIDI_MAX) {
            val = MIDI_MAX;
          }

          send_midi(MIDI_CC, CC_11, val, endpoint);
        }
      }
    }
    packet = MIDIPacketNext(packet);
  }
}

void connect_source(MIDIEndpointRef endpoint_ref, MIDIPortRef port_ref) {
  attempt(
    MIDIInputPortCreate(
      midiclient,
      CFSTR("input port"),
      &read_midi,
      NULL /* refCon */,
      &port_ref),
   "creating input port");

  attempt(
    MIDIPortConnectSource(
      port_ref,
      endpoint_ref,
      NULL /* connRefCon */),
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
  attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &midiclient),
    "creating OS-X MIDI client object." );

  MIDIEndpointRef axis49, breath_controller, game_controller, tilt_controller, feet_controller;
  if (get_endpoint_ref(CFSTR("AXIS-49 2A"), &axis49)) {
    connect_source(axis49, midiport_axis_49);
  }
  if (get_endpoint_ref(CFSTR("Breath Controller 5.0-15260BA7"), &breath_controller)) {
    connect_source(breath_controller, midiport_breath_controller);
  }
  if (get_endpoint_ref(CFSTR("game controller"), &game_controller)) {
    connect_source(game_controller, midiport_game_controller);
  }
  if (get_endpoint_ref(CFSTR("yocto 3d v2"), &tilt_controller)) {
    connect_source(tilt_controller, midiport_tilt_controller);
  }
  if (get_endpoint_ref(CFSTR("VIEWCON.."), &feet_controller)) {
    connect_source(feet_controller, midiport_feet_controller);
  }

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }

  create_source(&endpoints[ENDPOINT_ACCORDION], CFSTR("jammer-accordion"));
  create_source(&endpoints[ENDPOINT_SAX],       CFSTR("jammer-sax"));
  create_source(&endpoints[ENDPOINT_TROMBONE],  CFSTR("jammer-trombone"));
  create_source(&endpoints[ENDPOINT_KEYBOARD],  CFSTR("jammer-keyboard"));
  create_source(&endpoints[ENDPOINT_LEAD],      CFSTR("jammer-lead"));
  create_source(&endpoints[ENDPOINT_FOOTBASS],  CFSTR("jammer-footbass"));
  create_source(&endpoints[ENDPOINT_DRUM],       CFSTR("jammer-drum"));
  create_source(&endpoints[ENDPOINT_JAWHARP],   CFSTR("jammer-jawharp"));
}

#endif
