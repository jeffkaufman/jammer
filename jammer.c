#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include "linuxapi.h"
#include "jammermidilib.h"

#define TICK_MS 1  // try to tick every N milliseconds

#define FLUIDSYNTH_PORT_PREFIX "Synth input port"
#define AXIS49_PORT_NAME "AXIS-49 2A MIDI 1"
#define KEYBOARD_PORT_NAME "USB MIDI Interface MIDI 1"
#define BREATH_CONTROLLER_PORT_NAME "Breath Controller 5.0-15260BA7 "
#define FEET_PORT_NAME "mio MIDI 1"
#define KEYPAD_PORT_NAME "mido-keypad"  // pitch-detect:kbd.py

int fluidsynth_port;
int axis49_port;
int keyboard_port;
int breath_controller_port;
int feet_port;
int keypad_port;

int fluidsynth_client;
int axis49_client;
int keyboard_client;
int breath_controller_client;
int feet_client;
int keypad_client;

int fluidsynth_index;
int axis49_index;
int keyboard_index;
int breath_controller_index;
int feet_index;
int keypad_index;

void setup_ports() {
  fluidsynth_port =
    axis49_port =
    keyboard_port =
    breath_controller_port =
    feet_port =
    keypad_port =
    fluidsynth_client =
    axis49_client =
    keyboard_client =
    breath_controller_client =
    feet_client =
    keypad_client =
    fluidsynth_index =
    axis49_index =
    keyboard_index =
    breath_controller_index =
    feet_index =
    keypad_index =
    -1;

  snd_seq_client_info_t *client_info;
  snd_seq_port_info_t *port_info;

  snd_seq_client_info_malloc(&client_info);
  snd_seq_port_info_malloc(&port_info);

  for (int iterations = 0; true; iterations++) {
    snd_seq_client_info_set_client(client_info, -1);
    while (snd_seq_query_next_client(seq, client_info) >= 0) {
      int client = snd_seq_client_info_get_client(client_info);

      if (client == SND_SEQ_CLIENT_SYSTEM) {
        continue;  // ignore system timer
      }

      snd_seq_port_info_set_client(port_info, client);
      snd_seq_port_info_set_port(port_info, -1);
      while (snd_seq_query_next_port(seq, port_info) >= 0) {
        if (iterations == 0) {
          printf("Device: %s\n", snd_seq_port_info_get_name(port_info));
        }

        // Input ports: we need reading.
        if ((snd_seq_port_info_get_capability(port_info)
             & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
            == (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) {
          if (strcmp(snd_seq_port_info_get_name(port_info),
                     AXIS49_PORT_NAME) == 0) {
            axis49_client = snd_seq_port_info_get_client(port_info);
            axis49_port = snd_seq_port_info_get_port(port_info);
          } else if (strcmp(snd_seq_port_info_get_name(port_info),
                            KEYBOARD_PORT_NAME) == 0) {
            keyboard_client = snd_seq_port_info_get_client(port_info);
            keyboard_port = snd_seq_port_info_get_port(port_info);
          } else if (strcmp(snd_seq_port_info_get_name(port_info),
                            BREATH_CONTROLLER_PORT_NAME) == 0) {
            breath_controller_client = snd_seq_port_info_get_client(port_info);
            breath_controller_port = snd_seq_port_info_get_port(port_info);
          } else if (strcmp(snd_seq_port_info_get_name(port_info),
                            FEET_PORT_NAME) == 0) {
            feet_client = snd_seq_port_info_get_client(port_info);
            feet_port = snd_seq_port_info_get_port(port_info);
          } else if (strcmp(snd_seq_port_info_get_name(port_info),
                            KEYPAD_PORT_NAME) == 0) {
            keypad_client = snd_seq_port_info_get_client(port_info);
            keypad_port = snd_seq_port_info_get_port(port_info);
          }
        }

        // Output port: we need writing.
        if ((snd_seq_port_info_get_capability(port_info)
             & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
            == (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) {
          if (strncmp(FLUIDSYNTH_PORT_PREFIX,
                      snd_seq_port_info_get_name(port_info),
                      strlen(FLUIDSYNTH_PORT_PREFIX)) == 0) {
            fluidsynth_client = snd_seq_port_info_get_client(port_info);
            fluidsynth_port = snd_seq_port_info_get_port(port_info);
          }
        }
      }
    }

    if (fluidsynth_port == -1 || (keypad_port == -1 && axis49_port == -1)) {
      printf("waiting for %s%s%s...\n",
             fluidsynth_port == -1 ? "fluidsynth" : "",
             fluidsynth_port == -1 && (
                 keypad_port == -1 && axis49_port == -1)? " and " : "",
             (keypad_port == -1 && axis49_port == -1) ?
                 "keypad or axis49" : "");
      if (iterations < 50) {
        usleep(100000 /* 100ms */);
      } else {
        sleep(5 /* 5s */);
      }
    } else {
      break;
    }
  }

  int next_index = 0;
  fluidsynth_index = next_index++;
  snd_seq_port_info_set_port(port_info, fluidsynth_index);
  snd_seq_port_info_set_port_specified(port_info, 1);
  snd_seq_port_info_set_name(port_info, "jammer-fluidsynth");
  snd_seq_port_info_set_capability(port_info,
                                   SND_SEQ_PORT_CAP_READ |
                                   SND_SEQ_PORT_CAP_SUBS_READ);
  snd_seq_port_info_set_type(port_info,
                             SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                             SND_SEQ_PORT_TYPE_APPLICATION);
  attempt(snd_seq_create_port(seq, port_info), "create port");
  attempt(snd_seq_connect_to(seq, fluidsynth_index,
                             fluidsynth_client, fluidsynth_port),
          "connect to fluidsynth");

  if (axis49_port != -1) {
    axis49_index = next_index++;
    snd_seq_port_info_set_port(port_info, axis49_index);
    snd_seq_port_info_set_port_specified(port_info, 1);
    snd_seq_port_info_set_name(port_info, "jammer-axis49");
    snd_seq_port_info_set_capability(port_info,
                                     SND_SEQ_PORT_CAP_WRITE |
                                     SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_port_info_set_type(port_info,
                               SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                               SND_SEQ_PORT_TYPE_APPLICATION);
    attempt(snd_seq_create_port(seq, port_info), "create port");
    attempt(snd_seq_connect_from(seq, axis49_index,
                                 axis49_client, axis49_port),
            "connect to axis49");
  }

  if (keyboard_port != -1) {
    keyboard_index = next_index++;
    snd_seq_port_info_set_port(port_info, keyboard_index);
    snd_seq_port_info_set_port_specified(port_info, 1);
    snd_seq_port_info_set_name(port_info, "jammer-keyboard");
    snd_seq_port_info_set_capability(port_info,
                                     SND_SEQ_PORT_CAP_WRITE |
                                     SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_port_info_set_type(port_info,
                               SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                               SND_SEQ_PORT_TYPE_APPLICATION);
    attempt(snd_seq_create_port(seq, port_info), "create port");
    attempt(snd_seq_connect_from(seq, keyboard_index,
                                 keyboard_client, keyboard_port),
            "connect to keyboard");
  }

  if (breath_controller_port != -1) {
    breath_controller_index = next_index++;
    snd_seq_port_info_set_port(port_info, breath_controller_index);
    snd_seq_port_info_set_port_specified(port_info, 1);
    snd_seq_port_info_set_name(port_info, "jammer-breath_controller");
    snd_seq_port_info_set_capability(port_info,
                                     SND_SEQ_PORT_CAP_WRITE |
                                     SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_port_info_set_type(port_info,
                               SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                               SND_SEQ_PORT_TYPE_APPLICATION);
    attempt(snd_seq_create_port(seq, port_info), "create port");
    attempt(snd_seq_connect_from(seq, breath_controller_index,
                                 breath_controller_client,
                                 breath_controller_port),
            "connect to breath_controller");
  }

  if (feet_port != -1) {
    feet_index = next_index++;
    snd_seq_port_info_set_port(port_info, feet_index);
    snd_seq_port_info_set_port_specified(port_info, true);
    snd_seq_port_info_set_name(port_info, "jammer-feet");
    snd_seq_port_info_set_capability(port_info,
                                     SND_SEQ_PORT_CAP_WRITE |
                                     SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_port_info_set_type(port_info,
                               SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                               SND_SEQ_PORT_TYPE_APPLICATION);
    attempt(snd_seq_create_port(seq, port_info), "create port");
    attempt(snd_seq_connect_from(seq, feet_index,
                                 feet_client, feet_port),
            "connect to feet");
  }

  if (keypad_port != -1) {
    keypad_index = next_index++;
    snd_seq_port_info_set_port(port_info, keypad_index);
    snd_seq_port_info_set_port_specified(port_info, true);
    snd_seq_port_info_set_name(port_info, "jammer-keypad");
    snd_seq_port_info_set_capability(port_info,
                                     SND_SEQ_PORT_CAP_WRITE |
                                     SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_port_info_set_type(port_info,
                               SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                               SND_SEQ_PORT_TYPE_APPLICATION);
    attempt(snd_seq_create_port(seq, port_info), "create port");
    attempt(snd_seq_connect_from(seq, keypad_index,
                                 keypad_client, keypad_port),
            "connect to keypad");
  }
}

void tick() {
  jml_tick();
}

int tmp_jawharp_voice = 1;
void handle_event(snd_seq_event_t* event) {
  if (event->source.client == breath_controller_client) {
    handle_cc(event->data.control.param, event->data.control.value);
    return;
  }

  unsigned int action;
  if (event->type == SND_SEQ_EVENT_NOTEON) {
    action = MIDI_ON;
  } else if (event->type == SND_SEQ_EVENT_NOTEOFF) {
    action = MIDI_OFF;
  } else if (event->type == SND_SEQ_EVENT_CLOCK ||
             event->type == SND_SEQ_EVENT_SENSING) {
    return;
  } else {
    printf("unknown input type %d\n", event->type);
    return;
  }
  unsigned int note_in = event->data.note.note;
  unsigned int val = event->data.note.velocity;

  if (action == MIDI_ON && val == 0) {
    action = MIDI_OFF;
  }

  if (event->source.client == keyboard_client) {
    handle_piano(action, note_in, val);
  } else if (event->source.client == axis49_client) {
    /// pass
  } else if (event->source.client == feet_client) {
    handle_feet(action, note_in, val);
  } else if (event->source.client == keypad_client) {
    handle_keypad(action, note_in, val);
  } else {
    printf("ignored\n");
  }
}

void select_endpoint_voice(int endpoint, int voice, int volume_delta, int manual_volume) {
  send_midi(MIDI_CC, CC_07, 0, endpoint);

  int volume = 0;
  switch (voice) {

  case 80:
    volume = 57;
    break;
  case 81:
    volume = 60;
    break;
  case 84:
    volume = 68;
    break;
  case 38:
    volume = 82;
    break;
  case 85:
    volume = 84;
    break;
  case 75:
    volume = 87;
    break;
  case 39:
    volume = 96;
    break;
  case 7:
    volume = 98;
    break;
  case 24:
  case 26:
  case 28:
  case 35:
  case 64:
  case 66:
  case 67:
    volume = 112;
    break;
  case 4:
    volume = 127;
    break;
  }

  if (manual_volume != -1) {
    volume = manual_volume;
  }

  volume += volume_delta;

  if (endpoint == ENDPOINT_FLEX) {
    volume = volume / 1.5;
  }

  send_midi(MIDI_CC, CC_07, volume, endpoint);
  choose_voice(endpoint, 0, voice);
}

int main(int argc, char** argv) {
  attempt(snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0),
          "open seq");
  attempt(snd_seq_set_client_name(seq, "jammer"),
          "set client name");

  setup_ports();

  if (false) {
    int demo_note = 60;
    int demo_voice = 97;
    send_midi(MIDI_CC, CC_07, 127, 1);
    send_midi(MIDI_CC, CC_11, 127, 1);
    choose_voice(0, 1, demo_voice);
    send_midi(MIDI_ON, demo_note, 100, 1);
    sleep(2);
    send_midi(MIDI_OFF, demo_note, 100, 1);
    exit(0);
  }
  if (false) { // demo voices
    int demo_note = 32;  // 60
    for (int i = 0; i < 128; i++) {
      choose_voice(0, 1, i);
      printf("voice %d\n", i);
      send_midi(MIDI_CC, CC_07, 127, 1);
      send_midi(MIDI_CC, CC_11, 127, 1);
      send_midi(MIDI_ON, demo_note, 100, 1);
      sleep(2);
      send_midi(MIDI_OFF, demo_note, 100, 1);
    }
  }

  sleep(1);

  jml_setup();
  printf("listening...\n");

  int n_poll_file_descriptors = snd_seq_poll_descriptors_count(seq, POLLIN);
  struct pollfd* poll_file_descriptors =
    malloc(sizeof(struct pollfd) * n_poll_file_descriptors);
  snd_seq_poll_descriptors(seq, poll_file_descriptors, n_poll_file_descriptors,
                           POLLIN);

  while (true) {
    if (poll(poll_file_descriptors, n_poll_file_descriptors, TICK_MS) > 0) {
      do {
        snd_seq_event_t* event;
        if (snd_seq_event_input(seq, &event) > 0) {
          handle_event(event);
        }
      } while (snd_seq_event_input_pending(seq, 0) > 0);
    }

    tick();
  }
}
