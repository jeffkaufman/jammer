#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

// Spec:
// https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2

void jml_die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void jml_attempt(OSStatus result, char* errmsg) {
  if (result != noErr) {
    jml_die(errmsg);
  }
}

#define JML_CHANNEL 3

MIDIClientRef jml_midiclient;

bool polyphonic;

#define JML_N_ENDPOINTS 4

// TODO: at some point figure out how to allocate notes in chords to best fit
// ranges of instruments.
//
// TODO: when playing monophonically, consider automatically switching
// instruments at the edge of instrument's range if there's a wider range
// available.  Maybe just:
//  note > curMax: switch to alto
//  note < curMin: switch to baritone

int selected_endpoint;
MIDIEndpointRef jml_midiendpoints[JML_N_ENDPOINTS];
int jml_current_note_values[JML_N_ENDPOINTS];

#define ALTO 0     // 47 - 91
#define TENOR 1    // 42 - 88
#define BARITONE 2 // 35 - 86
#define SOPRANO 3  // 54 - 93

int min_range[JML_N_ENDPOINTS];
int max_range[JML_N_ENDPOINTS];

MIDIPortRef jml_midiport_axis_49;
MIDIPortRef jml_midiport_breath_controller;
bool newModel;

void jml_compose_midi(char actionType, int noteNo, int v, Byte* msg) {
  msg[0] = actionType + ((JML_CHANNEL-1) & 0xFF);
  msg[1] = noteNo;
  msg[2] = v;
}

#define JML_PACKET_BUF_SIZE (3+64) /* 3 for message, 32 for structure vars */
void jml_send_midi(char actionType, int noteNo, int v, MIDIEndpointRef* endpoint) {
  Byte buffer[JML_PACKET_BUF_SIZE];
  Byte msg[3];
  jml_compose_midi(actionType, noteNo, v, msg);

  MIDIPacketList *packetList = (MIDIPacketList*) buffer;
  MIDIPacket *curPacket = MIDIPacketListInit(packetList);

  curPacket = MIDIPacketListAdd(packetList,
				JML_PACKET_BUF_SIZE,
				curPacket,
				AudioGetCurrentHostTime(),
				3,
				msg);
  if (!curPacket) {
      jml_die("packet list allocation failed");
  }

  jml_attempt(MIDIReceived(*endpoint, packetList), "error sending midi");
}

bool getEndpointRef(CFStringRef targetName, MIDIEndpointRef* endpointRef) {
  int n_sources = MIDIGetNumberOfSources();
  if (!n_sources) {
    jml_die("no midi sources found");
  }
  for (int i = 0; i < n_sources ; i++) {
    MIDIEndpointRef src = MIDIGetSource(i);
    if (!src) continue;

    MIDIEntityRef ent;
    MIDIEndpointGetEntity(src, &ent);
    if (!ent) continue;

    MIDIDeviceRef dev;
    MIDIEntityGetDevice(ent, &dev);
    if (!dev) continue;

    CFStringRef name;
    MIDIObjectGetStringProperty (dev, kMIDIPropertyName, &name);

    printf("Saw %s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8));

    if (CFStringCompare(name, targetName, 0) == kCFCompareEqualTo) {
      *endpointRef = src;
      return true;
    }
  }
  return false;  // failed to find one
}

char mapping(unsigned char note_in) {
  if (newModel) {
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

void allNotesOff() {
  for (int i = 0; i < JML_N_ENDPOINTS; i++) {
    jml_current_note_values[i] = -1;
    for (int j = 0 ; j < 128; j++) {
      jml_send_midi(0x80, i, 0, &jml_midiendpoints[i]);
    }
    // send an explicit all notes off as well
    jml_send_midi(0xb0, 123, 0, &jml_midiendpoints[i]);
  }
}

void play_note(int mode, int note, int velocity, int endpoint_index) {
  printf("sending %d to index %d\n", note_out, endpoint_index);
  if (endpoint_index == TENOR || endpoint_index == BARITONE) {
    // these two sound an ocatve lower than their midi value
    note += 12;
  }
  jml_send_midi(mode, note, velocity, &jml_midiendpoints[endpoint_index]);
}

bool in_range(note, endpoint) {
  return note >= min_range[endpoint] && note <= max_range[endpoint];
}

int breathVal = -1;
void read_midi(const MIDIPacketList *pktlist,
               void *readProcRefCon,
               void *srcConnRefCon) {
  const MIDIPacket* packet = pktlist->packet;
  for (int i = 0; i < pktlist->numPackets; i++) {
    if (packet->length != 3) {
      printf("bad packet len: %d\n", packet->length);
      continue;
    }

    unsigned int mode = packet->data[0];
    unsigned int note_in = packet->data[1];
    unsigned int val = packet->data[2];

    printf("got packet %x %x %x\n", mode, note_in, val);

    if (mode == 0x80 || mode == 0x90) {
      // note on or off

      if (note_in == 1 || note_in == 2) {
        // switch polyphonic mode
        polyphonic = (note_in == 2);
        printf("setting mode polyphonic=%d\n", polyphonic);
        allNotesOff();
        continue;
      }

      if (note_in >= 3 && note_in < 3 + JML_N_ENDPOINTS) {
        // switch default endpoint for switching instruments
        selected_endpoint = note_in - 3;
        printf("switched to endpoint %d\n", selected_endpoint);
        allNotesOff();
        continue;
      }

      unsigned char note_out = mapping(note_in) + 12;

      if (polyphonic) {
        // Figure out which endpoint to use to simulate polyphony,
        // moving notes between endpoints as needed.
        int j;
        for (j = 0; j < JML_N_ENDPOINTS; j++) {
          if (mode == 0x80 || val == 0) {
            if (jml_current_note_values[j] == note_out) {
              jml_current_note_values[j] = -1;
              break;
            }
          } else {
            if (jml_current_note_values[j] == -1 && in_range(note_out, j) {
              jml_current_note_values[j] = note_out;
              break;
            }
          }
        }
        if (j < JML_N_ENDPOINTS) {
          // found available endpoint, all set.
          play_note(mode, note, val, j);
          continue;
        }
        if (mode == 0x80) {
          printf("this shouldn't happen");
          continue;
        }
        
        // All endpoints that could take this note are busy. Try to
        // swap things around.

        // first make sure we have a space to swap to, and put it there
        for (j = 0; j < JML_N_ENDPOINTS; j++) {
          if (jml_current_note_values[j] == -1):
            jml_current_note_values[j] = note_out;
          }
        }
        if (j == JML_N_ENDPOINTS) {
          // no place to put it; no solution
          continue;
        }
        // Now try to swap them around to make the range work out.

        int new_notes[JML_N_ENDPOINTS];
        for (j = 0; j < JML_N_ENDPOINTS; j++) {
          new_notes[j] = jml_current_note_values[j];
        }

        // this is a terrible algorithm, but JML_N_ENDPOINTS is very
        // small and it's hard to write this properly in C
        bool progress = false;
        while (progress) {
          progress = false;
          for (j = 0; j < JML_N_ENDPOINTS; j++) {
            int noteJ = new_notes[j]
            if (!in_range(noteJ, j) {
              for (k = 0, k < JML_N_ENDPOINTS; k++) {
                if (k != j) {
                  if (in_range(noteJ, k) && in_range(new_notes[k], j)) {
                    new_notes[j] = new_notes[k];
                    new_notes[k] = noteJ;
                    progress = true;
                  }
                }
              }
            }
          }
        }

        bool success = true;
        for (j = 0; j < JML_N_ENDPOINTS; j++) {
          if (!in_range(new_notes[j])) {
            success = false;
          }
        }
        if (!success) {
          printf("couldn't reallocate notes\n");
          continue;
        }

        // Now we just need to transfer new_notes to
        // jml_current_note_values and the synth.
        for (j = 0; j < JML_N_ENDPOINTS; j++) {
          if (jml_current_note_values[j] != -1) {
            // turn off current note
            play_note(0x80, jml_current_note_values[j], 0, j);
          }
          jml_current_note_values[j] = new_notes[j];
          play_note(0x90, jml_current_note_values[j], val, j);
        }
        // this was probably too much work
      } else {
        // switch instruments if out of range, but only to the bottom
        // since all of them go higher than I want to play
        if (note_out < min_range[selected_endpoint] &&
            note_out >= min_range[BARITONE]) {
          selected_endpoint = BARITONE;
        }
        play_note(mode, note, val, selected_endpoint);
      }
    } else if (mode == 0xb0 && note_in == 0x02) {
      // breath controller

      for (int j = 0; j < JML_N_ENDPOINTS; j++) {
        // Send CC-11 instead of CC-2
        jml_send_midi(0xb0, 0x0b, val, &jml_midiendpoints[j]);
      }
    }
    packet = MIDIPacketNext(packet);
  }
}

void jml_setup() {
  MIDIEndpointRef axis49;
  if (getEndpointRef(CFSTR("AXIS-49 USB Keyboard"), &axis49)) {
    newModel = false;
  } else if (getEndpointRef(CFSTR("AXIS-49 2A"), &axis49)) {
    newModel = true;
  } else {
    jml_die("Couldn't find AXIS-49");
  }

  polyphonic = false;
  selected_endpoint = 0;

  MIDIEndpointRef breathController;
  bool haveBreathController = getEndpointRef(CFSTR("Breath Controller 5.0-15260BA7"),
                                             &breathController);
  jml_attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &jml_midiclient),
    "creating OS-X MIDI client object." );

  jml_attempt(
    MIDIInputPortCreate(
     jml_midiclient,
     CFSTR("axis 49 input port"),
     &read_midi,
     NULL /* refCon */,
     &jml_midiport_axis_49),
    "creating input port for Axis 49");

  jml_attempt(
    MIDIPortConnectSource(
     jml_midiport_axis_49,
     axis49,
     NULL /* connRefCon */),
    "connecting to Axis 49");

  if (haveBreathController) {
    jml_attempt(
      MIDIInputPortCreate(
       jml_midiclient,
       CFSTR("breath controller input port"),
       &read_midi,
       NULL /* refCon */,
       &jml_midiport_breath_controller),
      "creating input port for breath controller");

    jml_attempt(
      MIDIPortConnectSource(
       jml_midiport_breath_controller,
       breathController,
       NULL /* connRefCon */),
      "connecting to Breath Controller");
  }

  for (int i = 0; i < JML_N_ENDPOINTS; i++) {
    min_range[i] = 0;
    max_range[i] = 128;

    jml_current_note_values[i] = -1;

    jml_attempt(
      MIDISourceCreate(
       jml_midiclient,
       (__bridge CFStringRef)[NSString stringWithFormat:@"jammer-%i", i],
       &jml_midiendpoints[i]),
      "creating OS-X virtual MIDI source." );
  }

  min_range[ALTO] = 47;
  max_range[ALTO] = 91;

  min_range[TENOR] = 42;
  max_range[TENOR] = 88;

  min_range[BARITONE] = 35;
  max_range[BARITONE] = 86;

  min_range[SOPRANO] = 54;
  max_range[SOPRANO] = 93;
}

#endif
