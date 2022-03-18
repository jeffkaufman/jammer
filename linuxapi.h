#ifndef JML_LINUX_API_H
#define JML_LINUX_API_H

#include <stdbool.h>
#include <time.h>
#include <math.h>
#include "common.h"

int attempt(int result, char* errmsg) {
  if (result < 0) {
    perror("");
    die(errmsg);
  }
  return result;
}

uint64_t now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
  return (ts.tv_sec * 1000000000LL) + ts.tv_nsec;
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
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;


  int channel = endpoint;
  //printf("sending %d %d %d %d\n", action, channel, note, velocity);

  snd_seq_event_t ev;
  reset_event(&ev);

  const char* friendly_action;
  if (action == MIDI_CC) {
    snd_seq_ev_set_controller(&ev, channel, note, velocity);
    friendly_action = "cc";
  } else if (action == MIDI_ON) {
    snd_seq_ev_set_noteon(&ev, channel, note, velocity);
    friendly_action = "on";
  } else if (action == MIDI_OFF) {
    snd_seq_ev_set_noteoff(&ev, channel, note, velocity);
    friendly_action = "off";
  } else {
    printf("unknown action %d\n", action);
    return;
  }

  int result = snd_seq_event_output_direct(seq, &ev);
  if (result < 0) {
    printf("dropped %s %d %d %d (err=%d)\n",
           friendly_action, channel, note, velocity, result);
  }
}

void choose_voice(int channel, int bank, int voice) {
  if (bank < 0) bank = 0;
  if (bank > 127) bank = 127;
  if (voice < 0) voice = 0;
  if (voice > 127) voice = 127;

  // bank select doesn't seem to work, though
  printf("selecting voice %d-%d for channel %d\n", bank, voice, channel);
  send_midi(MIDI_CC, CC_BANK_SELECT, bank, channel);

  snd_seq_event_t ev;
  reset_event(&ev);
  snd_seq_ev_set_pgmchange(&ev, channel, voice);
  attempt(snd_seq_event_output_direct(seq, &ev), "send event");
  printf("set endpoint #%d to voice %d\n", channel, voice);
}



#endif
