#ifndef JML_SPEECH_TIMING_H
#define JML_SPEECH_TIMING_H

// When a spoken number lands, from when it was said: see "Landing" in
// speech.h.  Plain C, apart from that file, so test-keypad.c can check it.

#include <stdbool.h>
#include <stdint.h>

// Beats after the word's onset.  Two rather than one: a number and the
// moment it takes to be sure of it run from about 450ms to a second, which
// at dance tempos is more than a beat as often as not, and a chord that
// came a beat after some words and the moment it was heard after others
// wasn't on anything.  Two covers every recording up to about 125 BPM.
#define SPEECH_BEATS_AFTER_ONSET 2
// A pedal beat this far ahead of the landing's time makes it, inside the
// beat before its notes, so a pedal a hair early plays the new chord.
#define SPEECH_EARLY_FRACTION 0.25

typedef struct {
  uint64_t due;       // when it lands, on now()'s clock
  uint64_t earliest;  // or from here on, the next pedal beat makes it
  int beats;          // after the onset: SPEECH_BEATS_AFTER_ONSET, or more
} SpeechLandingTime;

// `onset_ns` when the word began, `beat_ns` the beat -- the pedals' if
// they're going (`pedals`), else the default tempo's -- and `now_ns` the
// time.  Two beats after the onset, or if that's gone by, the next whole
// beat after it: never "now", which would be a different time for every
// word.
static SpeechLandingTime speech_landing_time(uint64_t onset_ns,
                                             uint64_t beat_ns, bool pedals,
                                             uint64_t now_ns) {
  SpeechLandingTime l;
  l.beats = SPEECH_BEATS_AFTER_ONSET;
  l.due = onset_ns + (uint64_t)l.beats * beat_ns;
  while (l.due <= now_ns) {
    l.due += beat_ns;
    l.beats++;
  }
  l.earliest = pedals ? l.due - (uint64_t)(SPEECH_EARLY_FRACTION * beat_ns)
                      : l.due;
  return l;
}

#endif
