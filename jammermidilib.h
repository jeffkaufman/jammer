#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void attempt(OSStatus result, char* errmsg) {
  if (result != noErr) {
    die(errmsg);
  }
}

#define KEYBOARD 0
#define JAWHARP 1
#define SAX 2
#define TROMBONE 3
#define ACCORDION 4
#define N_ENDPOINTS 5

#define MODE_MAJOR 0
#define MODE_MIXO 1
#define MODE_MINOR 2
#define MODE_DORIAN 3
#define N_MODES 4

#define SUSTAIN_STANDARD 1
#define SUSTAIN_RADIO 2
#define BEGIN_ENDPOINT_RANGE (SUSTAIN_RADIO + 1)
#define END_ENDPONT_RANGE (BEGIN_ENDPOINT_RANGE + N_ENDPOINTS)
#define BEGIN_MODE_RANGE (END_ENDPONT_RANGE)
#define END_MODE_RANGE (BEGIN_MODE_RANGE + N_MODES)

#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;
MIDIPortRef midiport_breath_controller;
MIDIPortRef midiport_game_controller;

int instrument = 0;
int musical_mode = 0;
int degree = 1;

bool new_model;

int current_note[N_ENDPOINTS];

bool radio_buttons = false;

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

  switch (degree) {
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
    return (musical_mode == MODE_MAJOR ? 11 : 10) - 12; // octave down
  }
  printf("bad degree %d\n", degree);
  return 0;
}

char mapping(unsigned char note_in) {
  if (new_model) {
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
  } else {
    switch (note_in) {
    case 25: return 37;  // Db / C#
    case 32: return 39;  // D# / Eb
    case 39: return 41;  // F
    case 46: return 43;  // G
    case 28: return 44;  // G# / Ab
    case 53: return 45;  // A
    case 35: return 46;  // Bb
    case 60: return 47;  // B
    case 42: return 48;  // C
    case 24: return 49;  // C# / Db
    case 67: return 49;  // C# / Db
    case 49: return 50;  // D
    case 31: return 51;  // D# / Eb
    case 56: return 52;  // E
    case 38: return 53;  // F
    case 63: return 54;  // F#
    case 45: return 55;  // G
    case 70: return 56;  // G# / Ab
    case 27: return 56;  // G# / Ab
    case 52: return 57;  // A
    case 34: return 58;  // Bb
    case 59: return 59;  // B
    case 41: return 60;  // C
    case 66: return 61;  // C# / Db
    case 48: return 62;  // D
    case 30: return 63;  // D# / Eb
    case 55: return 64;  // E
    case 37: return 65;  // F
    case 62: return 66;  // F#
    case 44: return 67;  // G
    case 69: return 68;  // G# / Ab
    case 26: return 68;  // G# / Ab
    case 51: return 69;  // A
    case 33: return 70;  // Bb
    case 58: return 71;  // B
    case 40: return 72;  // C
    case 65: return 73;  // C# / Db
    case 47: return 74;  // D
    case 72: return 75;  // D# / Eb
    case 29: return 75;  // D# / Eb
    case 54: return 76;  // E
    case 36: return 77;  // F
    case 61: return 78;  // F#
    case 43: return 79;  // G
    case 68: return 80;  // G# / Ab
    case 50: return 81;  // A
    case 57: return 83;  // B
    case 64: return 85;  // C# / Db
    case 71: return 87;  // D# / Eb
    default:
      return 0;
    }
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

void all_notes_off() {
  for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
    for (int note = 0 ; note < 128; note++) {
      send_midi(MIDI_OFF, note, 0, endpoint);
    }
    // send an explicit all notes off as well
    send_midi(MIDI_CC, 123, 0, endpoint);
  }
}

void read_midi(const MIDIPacketList *pktlist,
               void *readProcRefCon,
               void *srcConnRefCon) {
  const MIDIPacket* packet = pktlist->packet;
  for (int i = 0; i < pktlist->numPackets; i++) {
    if (packet->length != 3) {
      printf("bad packet len: %d\n", packet->length);
    } else {
      unsigned int mode = packet->data[0];
      unsigned int note_in = packet->data[1];
      unsigned int val = packet->data[2];

      //printf("got packet %x %d %x\n", mode, note_in, val);

      if (mode == MIDI_ON && val == 0) {
        mode = MIDI_OFF;
      }

      if (mode == MIDI_OFF || mode == MIDI_ON) {
        // note on or off

        if (note_in == SUSTAIN_STANDARD || note_in == SUSTAIN_RADIO) {
          radio_buttons = (note_in == SUSTAIN_RADIO);
          printf("selected sustain mode %d\n", radio_buttons);
          all_notes_off();
        } else if (note_in >= BEGIN_ENDPOINT_RANGE && note_in < END_ENDPONT_RANGE) {
          // switch default endpoint for switching instruments
          instrument = note_in - BEGIN_ENDPOINT_RANGE;
          printf("switched to endpoint %d\n", instrument);
          all_notes_off();
        } else if (instrument == JAWHARP &&
                   note_in >= BEGIN_MODE_RANGE &&
                   note_in < END_MODE_RANGE) {
          musical_mode = note_in - BEGIN_MODE_RANGE;
          printf("seleced mode %d\n", musical_mode);
        } else {
          unsigned char note_out = mapping(note_in) + 12 + 2;

          if (instrument == SAX) {
            note_out += 12;
          } else if (instrument == JAWHARP) {

            note_out += scale_degree();
            note_out -= 12;
          }

          if (instrument == JAWHARP || instrument == ACCORDION) {
            val = 127;
          }

          if (radio_buttons) {
            if (mode == MIDI_ON) {
              if (current_note[instrument] != -1) {
                send_midi(MIDI_OFF, current_note[instrument], 0, instrument);
              }
              current_note[instrument] = note_out;
              send_midi(MIDI_ON, note_out, val, instrument);
            } else if (mode == MIDI_OFF) {
              // ignore
            }
          } else {
            send_midi(mode, note_out, val, instrument);
          }
        }
      } else if (mode == MIDI_CC) {
        // pass control change to all synths
        for (int endpoint = 0; endpoint < N_ENDPOINTS; endpoint++) {
          if (note_in == 0x02) { // breath controller
            note_in = 0x0b; // send CC-11 instead of CC-2
          }

          if (endpoint == ACCORDION && !radio_buttons) {
            val += 18;
          } else if (endpoint == JAWHARP) {
            val += 36;
          }

          if (val > 127) {
            val = 127;
          }

          if (radio_buttons && endpoint != JAWHARP && val == 0 && current_note[endpoint] != -1) {
            send_midi(MIDI_OFF, current_note[endpoint], 0, endpoint);
            current_note[endpoint] = -1;
          }

          if (endpoint == KEYBOARD) {
            // skip breath
          } else {
            send_midi(MIDI_CC, note_in, val, endpoint);
          }
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
  MIDIEndpointRef axis49;
  if (get_endpoint_ref(CFSTR("AXIS-49 USB Keyboard"), &axis49)) {
    new_model = false;
  } else if (get_endpoint_ref(CFSTR("AXIS-49 2A"), &axis49)) {
    new_model = true;
  } else {
    die("Couldn't find AXIS-49");
  }

  MIDIEndpointRef breath_controller;
  bool have_breath_controller = get_endpoint_ref(CFSTR("Breath Controller 5.0-15260BA7"),
                                                 &breath_controller);
  MIDIEndpointRef game_controller;
  bool have_game_controller = get_endpoint_ref(CFSTR("game controller"),
                                               &game_controller);
  attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &midiclient),
    "creating OS-X MIDI client object." );

  connect_source(axis49, midiport_axis_49);

  if (have_breath_controller) {
    connect_source(breath_controller, midiport_breath_controller);
  }

  if (have_game_controller) {
    connect_source(game_controller, midiport_game_controller);
  }

  create_source(&endpoints[TROMBONE], CFSTR("jammer-trombone"));
  create_source(&endpoints[SAX], CFSTR("jammer-sax"));
  create_source(&endpoints[JAWHARP], CFSTR("jammer-jawharp"));
  create_source(&endpoints[KEYBOARD], CFSTR("jammer-keyboard"));
  create_source(&endpoints[ACCORDION], CFSTR("jammer-accordion"));

  for (int i = 0; i < N_ENDPOINTS; i++) {
    current_note[i] = -1;
  }
}

#endif
