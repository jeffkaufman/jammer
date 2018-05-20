#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>

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
MIDIEndpointRef jml_midiendpoint;
MIDIPortRef jml_midiport;

void jml_compose_midi(char actionType, int noteNo, int v, Byte* msg) {
  msg[0] = actionType + ((JML_CHANNEL-1) & 0xFF);
  msg[1] = noteNo;
  msg[2] = v;
}

#define JML_PACKET_BUF_SIZE (3+64) /* 3 for message, 32 for structure vars */
void jml_send_midi(char actionType, int noteNo, int v) {
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

  jml_attempt(MIDIReceived(jml_midiendpoint, packetList), "error sending midi");
}

MIDIEndpointRef getEndpointRef() {
  int n_sources = MIDIGetNumberOfSources();
  if (!n_sources) {
    jml_die("no sources found");
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

    if (CFStringCompare(name, CFSTR("AXIS-49 USB Keyboard"), 0) !=
        kCFCompareEqualTo) continue;

    return src;
  }
  jml_die("Failed to locate an AXIS-49");
  return 0;  // not reached
}

char mapping(unsigned char note_in) {
  //TODO: map the rest of the notes
  //TODO: figure out how to get it into selfless mode
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

void read_midi(const MIDIPacketList *pktlist,
               void *readProcRefCon,
               void *srcConnRefCon) {
  const MIDIPacket* packet = pktlist->packet;
  for (int i = 0; i < pktlist->numPackets; i++) {
    if (packet->length != 3) {
      printf("bad packet len: %d\n", packet->length);
    } else {
      unsigned char mode = packet->data[0];
      unsigned char note_in = packet->data[1];
      unsigned char val = packet->data[2];

      unsigned char note_out = mapping(note_in);

      jml_send_midi(mode, note_out, val);

      if (mode == 0x90) {
        printf("%d\n", note_in);
      }
    }
    packet = MIDIPacketNext(packet);
  }
}

void jml_setup() {
  MIDIEndpointRef src = getEndpointRef();  // exits on failure

  jml_attempt(
    MIDIClientCreate(
     CFSTR("jammer"),
     NULL, NULL, &jml_midiclient),
    "creating OS-X MIDI client object." );

  jml_attempt(
    MIDIInputPortCreate(
     jml_midiclient,
     CFSTR("jammer"),
     &read_midi,
     NULL /* refCon */,
     &jml_midiport),
    "creating input port for Axis 49");

  jml_attempt(
    MIDIPortConnectSource(
     jml_midiport,
     src,
     NULL /* connRefCon */),
    "connecting to Axis 49");

  jml_attempt(
    MIDISourceCreate(
     jml_midiclient,
     CFSTR("jammer"),
     &jml_midiendpoint),
    "creating OS-X virtual MIDI source." );
}

#endif
