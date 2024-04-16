#ifndef JAMMER_MIDI_LIB_H
#define JAMMER_MIDI_LIB_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#import <Foundation/Foundation.h>

/* endpoints */
#define ENDPOINT_TROMBONE 0
#define N_ENDPOINTS (ENDPOINT_TROMBONE+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0
#define MIDI_PITCH_BEND 0xe0

#define CC_MOD 0x01
#define CC_BREATH 0x02
#define CC_07 0x07
#define CC_11 0x0b

#define MIN_TROMBONE 20 // need to be blowing this hard to make the trombone make noise
#define MAX_TROMBONE 100  // cap midi breath to the trombone at this, or it gets blatty

#define MIDI_MAX 127

#define TICK_MS 10  // try to tick every N milliseconds

#define NS_PER_SEC 1000000000L

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

MIDIClientRef midiclient;
MIDIEndpointRef endpoints[N_ENDPOINTS];

MIDIPortRef midiport_teensy;
MIDIPortRef midiport_breath_controller;

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

unsigned char last_note = 60;
unsigned char last_vel = 30;
bool is_on = false;

void handle_button(unsigned int mode, unsigned int note_in, unsigned int val) {
  if (is_on) {
    send_midi(MIDI_OFF, last_note, 0, ENDPOINT_TROMBONE);
  }
  last_note = note_in;
  last_vel = val;
  send_midi(mode, last_note, val, ENDPOINT_TROMBONE);
  is_on = true;
}

void handle_cc(unsigned int cc, unsigned int val) {
  if (cc != CC_BREATH && cc != CC_11) {
    printf("Unknown Control change %d\n", cc);
    return;
  }

  int use_val = val;
  if (use_val > MIDI_MAX) {
    use_val = MIDI_MAX;
  }
  use_val -= MIN_TROMBONE;
  if (use_val < 0) {
    use_val = 0;
  }
  if (use_val > MAX_TROMBONE) {
    use_val = MAX_TROMBONE;
  }

  if (use_val == 0 && is_on) {
    send_midi(MIDI_OFF, last_note, 0, ENDPOINT_TROMBONE);
    is_on = false;
  }
  if (use_val > 10 && !is_on) {
    send_midi(MIDI_ON, last_note, last_vel, ENDPOINT_TROMBONE);
    is_on = true;
  }

  send_midi(MIDI_CC, CC_11, use_val, ENDPOINT_TROMBONE);  
}

void read_midi(const MIDIPacketList *pktlist,
               void *readProcRefCon,
               void *srcConnRefCon) {
  const MIDIPacket* packet = pktlist->packet;

  for (int i = 0; i < pktlist->numPackets; i++) {
    if (packet->length == 3) {
      unsigned int mode = packet->data[0];
      unsigned int note_in = packet->data[1];
      unsigned int val = packet->data[2];

      printf("got packet %u %u %u\n", mode, note_in, val);
      if (mode == MIDI_CC)  {
        handle_cc(note_in, val);
      } else if (val > 0) {
        handle_button(MIDI_ON, note_in, val);
      }
    } else if (packet->length == 6) {
      unsigned int mode = packet->data[0];
      unsigned int note_in = packet->data[1];
      unsigned int val = packet->data[5];
      printf("got packet %u %u %u\n", mode, note_in, val);
      if (val > 0) {
        handle_button(MIDI_ON, note_in, val);
      }
    } else {
      printf("bad packet!\n");
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

  MIDIEndpointRef teensy_controller, breath_controller;
  if (get_endpoint_ref(CFSTR("Teensy MIDI"), &teensy_controller)) {
    connect_source(teensy_controller, &midiport_teensy);
  }
  if (get_endpoint_ref(CFSTR("Breath Controller 5.0-15260BA7"), &breath_controller)) {
    connect_source(breath_controller, &midiport_breath_controller);
  }
  create_source(&endpoints[ENDPOINT_TROMBONE], CFSTR("jammer-trombone"));
}

void jml_tick() {
  // Called every TICK_MS
}

#endif
