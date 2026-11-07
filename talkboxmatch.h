#ifndef JML_TALKBOXMATCH_H
#define JML_TALKBOXMATCH_H

// Matching the mandolin through the talkbox to the mandolin without it.
//
// Audio Output > Match Talkbox to Mandolin: with the mandolin's TB on, the
// tube in your mouth and your mouth open, play for a few seconds.  While
// it's playing -- the mandolin over its gate -- the audio thread adds up
// two things at the point both land on the right: the mandolin as it would
// have played dry, and the microphone as it comes back at the return's
// level.  Their ratio says how far off the talkbox's mandolin is, and the
// "Talkbox send from the right" slider moves by that much, since what comes
// back scales with what's sent.  That send is the right side's alone, so
// the left's endpoints' talkbox level stays as it was.
//
// Some of what can be wrong is outside jammer, and then this says which
// knob to turn instead of setting anything: the microphone clipping, the
// talkbox too quiet to hear over the room, or a send so high the talkbox's
// device would clip.  This file is the deciding, apart from the audio so
// it can be tested; talkbox.h does the measuring.

#include <math.h>
#include <stdbool.h>

// Seconds of playing to go by, and how long to wait for them.
#define TBMATCH_PLAYING_S 4.0
#define TBMATCH_TIMEOUT_S 30.0
// The microphone's peak that counts as clipping, and the most the talkbox's
// device may be sent, a little under full scale.
#define TBMATCH_MIC_CLIP 0.98
#define TBMATCH_SEND_CEILING 0.9
// What comes back has to be this many times the room's power, between
// strums, to be worth matching: 6dB.
#define TBMATCH_MIN_OVER_ROOM 4.0
// And there has to be this much between strums to judge the room by.
#define TBMATCH_MIN_QUIET_S 0.5

typedef struct {
  double dry_energy;     // the mandolin as it'd play dry, while playing
  double mic_energy;     // the microphone, raw, while playing
  double quiet_energy;   // the microphone, raw, while not
  double playing_frames;
  double quiet_frames;
  float mic_peak;        // the microphone's, while playing
  float send_peak;       // what was sent to the talkbox's device
} TalkboxMatchStats;

enum {
  TBMATCH_OK = 0,
  TBMATCH_NOT_PLAYED,    // not enough mandolin to go by: nothing set
  TBMATCH_NOTHING_BACK,  // the microphone heard nothing: nothing set
  TBMATCH_MIC_CLIPS,     // turn input 1 down: nothing set
  TBMATCH_BURIED,        // talkbox hardly over the room: nothing set
  TBMATCH_DEVICE_CLIPS,  // set as far as the device allows: turn the amp up
  TBMATCH_SEND_MAXED,    // set to the slider's top: turn the amp up
};

typedef struct {
  int outcome;
  double send_volume;    // the slider's new value (the old one if not set)
  double off_db;         // how far the talkbox's mandolin was, + is louder
  double short_db;       // how far it still is, after setting, + is quieter
} TalkboxMatchResult;

// `send_volume` is the right send's slider as it was during the measuring,
// up to `max_send`; `return_gain` the return's gain as applied; `rate` the
// rig's.
static TalkboxMatchResult talkbox_match_decide(const TalkboxMatchStats* s,
                                               double send_volume,
                                               double max_send,
                                               double return_gain,
                                               double rate) {
  TalkboxMatchResult r = { TBMATCH_OK, send_volume, 0, 0 };
  if (s->playing_frames < TBMATCH_PLAYING_S * rate * 0.5 ||
      s->dry_energy <= 0) {
    r.outcome = TBMATCH_NOT_PLAYED;
    return r;
  }
  double back = s->mic_energy * return_gain * return_gain;
  if (s->mic_energy / s->playing_frames < 1e-10 || back <= 0 ||
      send_volume <= 0) {
    r.outcome = TBMATCH_NOTHING_BACK;
    return r;
  }
  r.off_db = 10 * log10(back / s->dry_energy);
  if (s->mic_peak >= TBMATCH_MIC_CLIP) {
    r.outcome = TBMATCH_MIC_CLIPS;
    return r;
  }
  if (s->quiet_frames >= TBMATCH_MIN_QUIET_S * rate &&
      s->mic_energy / s->playing_frames <
        TBMATCH_MIN_OVER_ROOM * s->quiet_energy / s->quiet_frames) {
    r.outcome = TBMATCH_BURIED;
    return r;
  }
  double want = sqrt(s->dry_energy / back);  // the send's change
  double k = want;
  if (s->send_peak > 0 && s->send_peak * k > TBMATCH_SEND_CEILING) {
    k = TBMATCH_SEND_CEILING / s->send_peak;
    r.outcome = TBMATCH_DEVICE_CLIPS;
  }
  if (send_volume * k > max_send) {
    k = max_send / send_volume;
    r.outcome = TBMATCH_SEND_MAXED;
  }
  r.send_volume = send_volume * k;
  r.short_db = 20 * log10(want / k);
  return r;
}

#endif
