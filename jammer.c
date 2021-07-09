#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

#define OUTPUT_PORT "128:0"

// sudo fluidsynth --server --audio-driver=alsa -o audio.alsa.device=hw:1,0 /usr/share/sounds/sf2/FluidR3_GM.sf2

snd_seq_t* seq;
snd_seq_addr_t output_port;
snd_seq_event_t ev;

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

void send_event() {
  attempt(snd_seq_event_output(seq, &ev), "send event");
  attempt(snd_seq_drain_output(seq), "drain");
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
  send_event();
}

void choose_voice(int channel, int voice) {
  reset_event();
  snd_seq_ev_set_pgmchange(&ev, channel, voice);
  send_event();
}

int main() {
  attempt(snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0),
          "open seq");
  attempt(snd_seq_set_client_name(seq, "jammer"),
          "set client name");
  
  snd_seq_port_info_t* port_info;
  snd_seq_port_info_malloc(&port_info);
  snd_seq_port_info_set_port(port_info, 0);
  snd_seq_port_info_set_port_specified(port_info, 1);
  snd_seq_port_info_set_name(port_info, "jammer");
  snd_seq_port_info_set_capability(port_info, SND_SEQ_PORT_CAP_WRITE);
  snd_seq_port_info_set_type(port_info,
                             SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                             SND_SEQ_PORT_TYPE_APPLICATION);
  attempt(snd_seq_create_port(seq, port_info), "create port");
  attempt(snd_seq_parse_address(seq, &output_port, OUTPUT_PORT),
          "parse output port");
  attempt(snd_seq_connect_to(seq, 0, output_port.client,
                             output_port.port),
          "connect to port");
  
  for (int i = 0; i < 128; i++) {
    choose_voice(0, i);
    play(SND_SEQ_EVENT_NOTEON, 0, 64, 100);
    sleep(1);
    play(SND_SEQ_EVENT_NOTEOFF, 0, 64, 100);
  }
}


