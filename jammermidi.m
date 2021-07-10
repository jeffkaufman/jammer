#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreAudio/HostTime.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>
#import <Foundation/Foundation.h>
#include "macapi.h"
#include "jammermidilib.h"

// #define PIANO_MIDI_NAME "Roland Digital Piano"
#define PIANO_MIDI_NAME "USB MIDI Interface"

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

void mac_setup() {
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
  create_source(&endpoints[ENDPOINT_DRUM_A],             CFSTR("jammer-drum-a"));
  create_source(&endpoints[ENDPOINT_DRUM_B],             CFSTR("jammer-drum-b"));
  create_source(&endpoints[ENDPOINT_DRUM_C],             CFSTR("jammer-drum-c"));
  create_source(&endpoints[ENDPOINT_DRUM_D],             CFSTR("jammer-drum-d"));
  create_source(&endpoints[ENDPOINT_FOOT_1],             CFSTR("jammer-foot-1"));
  create_source(&endpoints[ENDPOINT_FOOT_3],             CFSTR("jammer-foot-3"));
  create_source(&endpoints[ENDPOINT_FOOT_4],             CFSTR("jammer-foot-4"));
  create_source(&endpoints[ENDPOINT_TAMBOURINE_FREE],    CFSTR("jammer-tambourine-free"));
  create_source(&endpoints[ENDPOINT_TAMBOURINE_STOPPED], CFSTR("jammer-tambourine-stopped"));
  create_source(&endpoints[ENDPOINT_AUTO_RIGHTHAND],     CFSTR("jammer-auto-righthand"));
  create_source(&endpoints[ENDPOINT_GROOVE_BASS],        CFSTR("jammer-groove-bass"));
}

int main(int argc, char** argv) {
  jml_setup();
  mac_setup();
  while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE)) {
    usleep(TICK_MS * 1000);
    jml_tick();
  }
  return 0;
}
