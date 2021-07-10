#ifndef JML_LINUX_API_H
#define JML_LINUX_API_H

#include <stdbool.h>
#include <time.h>
#include <math.h>

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

uint64_t now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000000000) + ts.tv_nsec;
}

snd_seq_t* seq;
snd_seq_event_t ev;

void reset_event() {
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, 0);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  ev.source.port = 0;
  ev.flags = SND_SEQ_TIME_STAMP_REAL;
}

void send_midi(int type, int note, int velocity, int endpoint) {
  int channel = endpoint % 16; // TODO
  
  reset_event();
  ev.type = type;
  ev.data.note.channel = channel;
  ev.data.note.note = note;
  ev.data.note.velocity = velocity;
  printf("sending %d %d %d %d\n", type, channel, note, velocity);
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}

void choose_voice(int channel, int voice) {
  reset_event();
  snd_seq_ev_set_pgmchange(&ev, channel, voice);
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}



#endif
