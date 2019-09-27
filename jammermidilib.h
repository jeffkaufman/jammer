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

#define ENDPOINT_JAMMER 0
#define N_ENDPOINTS (ENDPOINT_JAMMER+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_axis_49;

#define PACKET_BUF_SIZE (3+64) /* 3 for message, 32 for structure vars */
void send_midi(char actionType, int noteNo, int v, int endpoint) {
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

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  unsigned char note_out = mapping(note_in) + 12 + 2;
  send_midi(mode, note_out, val, ENDPOINT_JAMMER);
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

      if (srcConnRefCon == &midiport_axis_49) {
        handle_button(mode, note_in, val);
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

void jml_setup() {
  attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &midiclient),
    "creating OS-X MIDI client object." );

  print_endpoints();

  MIDIEndpointRef axis49;

  if (get_endpoint_ref(CFSTR("AXIS-49 2A"), &axis49)) {
    connect_source(axis49, &midiport_axis_49);
  } else {
    die("Couldn't find an AXIS-49\n");
  }

  create_source(&endpoints[ENDPOINT_JAMMER], CFSTR("jammer"));
}

#endif
