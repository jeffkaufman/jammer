#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#define FLUIDSYNTH_PORT_NAME "Synth input port (2132:0)"
#define AXIS49_PORT_NAME "AXIS-49 2A MIDI 1"
#define KEYBOARD_PORT_NAME "USB MIDI Interface MIDI 1"
#define BREATH_CONTROLLER_PORT_NAME "Breath Controller 5.0-15260BA7 "
#define FEET_PORT_NAME "mio MIDI 1"

// sudo fluidsynth --server --audio-driver=alsa -o audio.alsa.device=hw:1,0 /usr/share/sounds/sf2/FluidR3_GM.sf2

snd_seq_t* seq;
snd_seq_event_t ev;
int fluidsynth_port;
int axis49_port;
int keyboard_port;
int breath_controller_port;
int feet_port;

int fluidsynth_client;
int axis49_client;
int keyboard_client;
int breath_controller_client;
int feet_client;

int fluidsynth_index;
int axis49_index;
int keyboard_index;
int breath_controller_index;
int feet_index;

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

int attempt(int result, char* errmsg) {
  if (result < 0) {
    die(errmsg);
  }
  return result;
}

void reset_event() {
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, 0);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  ev.source.port = 0;
  ev.flags = SND_SEQ_TIME_STAMP_REAL;
}

void play(int type, int channel, int note, int velocity) {
  reset_event();
  ev.type = type;
  ev.data.note.channel = channel;
  ev.data.note.note = note;
  ev.data.note.velocity = velocity;
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}

void choose_voice(int channel, int voice) {
  reset_event();
  snd_seq_ev_set_pgmchange(&ev, channel, voice);
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}


void setup_ports() {
  fluidsynth_port =
    axis49_port =
    keyboard_port =
    breath_controller_port =
    feet_port =
    fluidsynth_client =
    axis49_client =
    keyboard_client =
    breath_controller_client =
    feet_client =
    fluidsynth_index =
    axis49_index =
    keyboard_index =
    breath_controller_index =
    feet_index =
    -1;

  snd_seq_client_info_t *client_info;
  snd_seq_port_info_t *port_info;

  snd_seq_client_info_malloc(&client_info);
  snd_seq_port_info_malloc(&port_info);

  snd_seq_client_info_set_client(client_info, -1);
  while (snd_seq_query_next_client(seq, client_info) >= 0) {
    int client = snd_seq_client_info_get_client(client_info);

    if (client == SND_SEQ_CLIENT_SYSTEM) {
      continue;  // ignore system timer
    }

    snd_seq_port_info_set_client(port_info, client);
    snd_seq_port_info_set_port(port_info, -1);
    while (snd_seq_query_next_port(seq, port_info) >= 0) {
      printf("Device: %s\n", snd_seq_port_info_get_name(port_info));

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
        }
      }

      // Output port: we need writing.
      if ((snd_seq_port_info_get_capability(port_info)
           & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
          == (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) {
        if (strcmp(snd_seq_port_info_get_name(port_info),
                   FLUIDSYNTH_PORT_NAME) == 0) {
          fluidsynth_client = snd_seq_port_info_get_client(port_info);
          fluidsynth_port = snd_seq_port_info_get_port(port_info);
        }
      }
    }
  }

  if (fluidsynth_port == -1) {
    die("can't run without output device; need fluidsynth");
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
    snd_seq_port_info_set_port_specified(port_info, 1);
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
}

int main() {
  attempt(snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0),
          "open seq");
  attempt(snd_seq_set_client_name(seq, "jammer"),
          "set client name");

  setup_ports();

  printf("listening...\n");
  while (1) {
    snd_seq_event_t* event;
    int val = snd_seq_event_input(seq, &event);
    printf("got something! %d\n", val);
    snd_seq_free_event(event);
  }
  for (int i = 0; i < 1 /*128*/; i++) {
    choose_voice(0, i);
    play(SND_SEQ_EVENT_NOTEON, 0, 64, 100);
    sleep(1);
    play(SND_SEQ_EVENT_NOTEOFF, 0, 64, 100);
  }
}
