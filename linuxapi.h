#ifndef JML_LINUX_API_H
#define JML_LINUX_API_H

#include <stdbool.h>
#include <time.h>
#include <math.h>
#include "common.h"

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

void reset_event(snd_seq_event_t* ev) {
  snd_seq_ev_clear(ev);
  snd_seq_ev_set_source(ev, 0);
  snd_seq_ev_set_subs(ev);
  snd_seq_ev_set_direct(ev);
  ev->source.port = 0;
  ev->flags = SND_SEQ_TIME_STAMP_REAL;
}

void send_midi(int action, int note, int velocity, int endpoint) {
  int channel = endpoint % 16; // TODO
  printf("sending %d %d %d %d\n", action, channel, note, velocity);

  snd_seq_event_t ev;
  reset_event(&ev);

  if (action == MIDI_CC) {
    snd_seq_ev_set_controller(&ev, channel, note, velocity);
  } else if (action == MIDI_ON) {
    snd_seq_ev_set_noteon(&ev, channel, note, velocity);
  } else if (action == MIDI_OFF) {
    snd_seq_ev_set_noteoff(&ev, channel, note, velocity);
  } else {
    printf("unknown action %d\n", action);
    return;
  }

  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}

void choose_voice(int channel, int voice) {
  snd_seq_event_t ev;
  reset_event(&ev);
  snd_seq_ev_set_pgmchange(&ev, channel, voice);
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
}



#endif
