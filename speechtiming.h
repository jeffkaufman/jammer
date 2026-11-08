#ifndef JML_SPEECH_TIMING_H
#define JML_SPEECH_TIMING_H

// When a spoken number lands, from when it was said: see "Landing" in
// speech.h.  Plain C, apart from that file, so test-keypad.c can check it.

#include <stdbool.h>
#include <stdint.h>

// Beats after the word's onset, give or take half a beat to the nearest
// pedal: a number started anywhere from 2.2 to 1.2 beats before a beat lands
// on it, borderline calls aside (speech_landing_time).  Not one beat: a
// number and the moment it takes to be sure of it run about 450-650ms with
// the fast recognizer, which at dance tempos is the better part of a beat.
// And not two: called "just before the change", words started a median 1.6
// beats ahead.  Tried from 1.6 to 1.95 over five recordings (liveval.c),
// with the bar deciding the borderline calls, 1.6 to 1.8 did as well.
#define SPEECH_BEATS_AFTER_ONSET_DEFAULT 1.7
#ifndef SPEECH_BEATS_AFTER_ONSET  // liveval.c tries others
#define SPEECH_BEATS_AFTER_ONSET SPEECH_BEATS_AFTER_ONSET_DEFAULT
#endif
// A pedal beat this far ahead of the landing's time makes it, inside the
// beat before its notes, so a pedal a hair early plays the new chord.
#define SPEECH_EARLY_FRACTION 0.25

typedef struct {
  uint64_t due;       // when it lands, on now()'s clock
  uint64_t earliest;  // or from here on, the next pedal beat makes it
  int beats;          // after the onset, to the nearest beat
  bool borderline;    // not decided by its time alone: by the changes
                      // before, or heard too late to choose
  long beat;          // which pedal beat, counting as pedal_beat does
} SpeechLandingTime;

// Where changes have landed since the pedals started, by pedal beat --
// counted, not timed, since 128 beats of a tempo that wanders by a percent
// or two is more than a beat out.  For telling apart the beats a call could
// mean (speech_landing_time).  Oldest first.
#define SPEECH_RECENT 64
typedef struct {
  long beat[SPEECH_RECENT];
  bool borderline[SPEECH_RECENT];  // decided by the ones before, not its time
  int n;
} SpeechRecent;

static void speech_recent_add(SpeechRecent* r, long beat, bool borderline) {
  if (r->n == SPEECH_RECENT) {
    for (int i = 1; i < SPEECH_RECENT; i++) {
      r->beat[i - 1] = r->beat[i];
      r->borderline[i - 1] = r->borderline[i];
    }
    r->n--;
  }
  r->beat[r->n] = beat;
  r->borderline[r->n] = borderline;
  r->n++;
}

static void speech_recent_clear(SpeechRecent* r) { r->n = 0; }

// A call can mean a beat as little as this far after it began, if it was
// heard in time -- or as much as this.
#define SPEECH_MIN_LEAD 1.0
#define SPEECH_MAX_LEAD 2.9
// Chords last a multiple of this many beats, almost always, and a tune
// doesn't gain or lose beats.  Two rather than four: most chords are four
// or eight beats, but runs of two-beat chords -- 6-5, 6-5-4-1 -- are common
// enough, and with four they were pulled off their beats (five recordings:
// 145 of 163 calls on time with four, 150 with two).
#define SPEECH_CHORD_BEATS_DEFAULT 2
#ifndef SPEECH_CHORD_BEATS  // liveval.c tries others
#define SPEECH_CHORD_BEATS SPEECH_CHORD_BEATS_DEFAULT
#endif
// How far back changes count, in beats.
#define SPEECH_MEMORY_BEATS_DEFAULT 128
#ifndef SPEECH_MEMORY_BEATS
#define SPEECH_MEMORY_BEATS SPEECH_MEMORY_BEATS_DEFAULT
#endif
// Whether a change that was itself borderline counts toward the next.
#ifndef SPEECH_BORDERLINE_VOTES
#define SPEECH_BORDERLINE_VOTES 0
#endif
// Once the bar's trusted, a call is borderline when its time is at least
// this far from the nearest beat: all but the ones right on a beat, which
// go by their time and so can tell it a beat was dropped.
#define SPEECH_BORDERLINE_DEFAULT 0.1
#ifndef SPEECH_BORDERLINE  // liveval.c tries others
#define SPEECH_BORDERLINE SPEECH_BORDERLINE_DEFAULT
#endif

// How many changes in a row, out of step with where they've been falling
// but in step with each other, it takes to move it: a beat dropped or added.
// Fewer than that are taken as mistakes.  Not two: a run of two-beat chords
// called late is two out of step in a row, and moving for those pulled the
// rest of the tune off.
#define SPEECH_SHIFT_AFTER_DEFAULT 3
#ifndef SPEECH_SHIFT_AFTER  // liveval.c tries others
#define SPEECH_SHIFT_AFTER SPEECH_SHIFT_AFTER_DEFAULT
#endif

// Where in the bar changes have been falling, as of beat `at`: a beat
// number modulo SPEECH_CHORD_BEATS, or -1 if there's nothing to go on.  Set
// by the first change in memory, and moved only by SPEECH_SHIFT_AFTER in a
// row agreeing on somewhere else.
// `support` is how many changes since it was last set have been in step
// with it, counting the ones that set it.
static int speech_bar_phase(const SpeechRecent* r, long at, int* support) {
  int phase = -1, pending = -1, n_pending = 0, n_support = 0;
  for (int i = 0; i < r->n; i++) {
    long d = at - r->beat[i];
    if (d <= 0 || d > SPEECH_MEMORY_BEATS) continue;
    if (r->borderline[i] && !SPEECH_BORDERLINE_VOTES) continue;
    int p = (int)(r->beat[i] % SPEECH_CHORD_BEATS);
    if (phase < 0 || p == phase) {
      phase = p;
      n_pending = 0;
      n_support++;
    } else if (p == pending && n_pending > 0) {
      if (++n_pending >= SPEECH_SHIFT_AFTER) {
        phase = p;
        n_support = n_pending;
        n_pending = 0;
      }
    } else {
      pending = p;
      n_pending = 1;
      if (n_pending >= SPEECH_SHIFT_AFTER) {
        phase = p;
        n_support = n_pending;
        n_pending = 0;
      }
    }
  }
  if (support) *support = phase < 0 ? 0 : n_support;
  return phase;
}

// Changes in step with where they've been falling that it takes to trust
// the bar fully.  With fewer, a call is borderline only nearer the halfway
// point between two beats -- with none, never -- and with this many, from
// SPEECH_BORDERLINE out.  At 8 every setting tried did about as well; at 4
// some did a little better and some much worse.
#define SPEECH_CONFIDENT_DEFAULT 8
#ifndef SPEECH_CONFIDENT  // liveval.c tries others
#define SPEECH_CONFIDENT SPEECH_CONFIDENT_DEFAULT
#endif

// How far from the nearest beat a call's time must be to be borderline,
// given `support` changes in step with the bar.
static double speech_borderline_at(int support) {
  double trust = SPEECH_CONFIDENT <= 0 ? 1
               : support >= SPEECH_CONFIDENT ? 1
               : (double)support / SPEECH_CONFIDENT;
  return 0.5 - (0.5 - SPEECH_BORDERLINE) * trust;
}

// `onset_ns` when the word began, `beat_ns` the beat -- the pedals' if
// they're going, else the default tempo's -- `pedal_ns` the last pedal hit,
// or 0 if they aren't going, `pedal_beat` which beat that was, counting
// pedal hits, `now_ns` the time, and `recent` where changes have landed
// since the pedals started, or NULL.
//
// With pedals, it's the pedal beat nearest SPEECH_BEATS_AFTER_ONSET beats
// after the onset.  People don't start a word to within a few tens of
// milliseconds -- calls in one recording ran over a spread of a beat -- and
// two beats after the onset exactly, off the grid, only fell on a beat for
// a word started in a window a quarter of a beat wide; the rest changed the
// chord between beats.
//
// Even the nearest beat isn't enough, though: calls for a change spread over
// a whole beat, from about 1.4 to 2.5 beats ahead, so a call whose time is
// near the halfway point between two beats could mean either.  There the
// music decides.  Chords last a multiple of two beats and a tune doesn't
// gain or lose any, so of the two it's the one where changes have been
// falling (speech_bar_phase): learned from calls that went by their time
// alone, over the last 128 beats, since the pedals started.  One or two
// changes out of step are taken as mistakes; SPEECH_SHIFT_AFTER in a row, in
// step with each other, as a beat dropped or added.  And the more changes
// have agreed, the more of the calls it decides (speech_borderline_at): with
// none, only its time; with SPEECH_CONFIDENT, any call more than
// SPEECH_BORDERLINE from a beat.  Only for those two beats, both from 1.0
// to 2.9 beats after the onset and not yet gone by.
//
// Over five recordings (liveval.c), 163 calls, that took the calls on time
// from 125 by their time alone to 148.  Most of the rest were started less
// than a beat ahead -- in runs of two-beat chords, mostly -- which nothing
// can land on time, or came just after a dropped beat.  Trusting the bar
// fully at once, or deciding every call by it, did better on some settings
// and far worse on others: a first call in the wrong place, or two late
// calls in a run, and it held the whole tune a beat off.
//
// Without pedals there's no grid: that many beats after the onset.  Either
// way, if that's gone by, the next beat after now -- never "now", which
// would be a different time for every word.
static SpeechLandingTime speech_landing_time(uint64_t onset_ns,
                                             uint64_t beat_ns,
                                             uint64_t pedal_ns,
                                             long pedal_beat,
                                             uint64_t now_ns,
                                             const SpeechRecent* recent) {
  SpeechLandingTime l;
  l.borderline = false;
  int64_t beat = (int64_t)beat_ns;
  int64_t target =
    (int64_t)onset_ns + (int64_t)(SPEECH_BEATS_AFTER_ONSET * (double)beat);
  int64_t due = target;
  long k = 0;
  if (pedal_ns) {
    int64_t from = target - (int64_t)pedal_ns;
    k = from >= 0 ? (from + beat / 2) / beat
                  : -((-from + beat / 2 - 1) / beat);
    due = (int64_t)pedal_ns + k * beat;
    // The beat on the other side of its time, and whether the music
    // prefers it.
    long dk = target > due ? 1 : -1;
    int64_t other = due + dk * beat;
    double lead = (double)(other - (int64_t)onset_ns) / (double)beat;
    double off = (double)(target > due ? target - due : due - target) /
                 (double)beat;
    // A call that could have meant a beat already gone by when it was
    // heard was too late to say which it meant: where it lands is no
    // evidence of where changes fall, and two late in a row would look like
    // a dropped beat.
    if (lead >= SPEECH_MIN_LEAD && lead <= SPEECH_MAX_LEAD &&
        (other <= (int64_t)now_ns || due <= (int64_t)now_ns)) {
      l.borderline = true;
    }
    int support = 0;
    int phase = recent ? speech_bar_phase(recent, pedal_beat + k, &support)
                       : -1;
    if (phase >= 0 && off >= speech_borderline_at(support) &&
        lead >= SPEECH_MIN_LEAD && lead <= SPEECH_MAX_LEAD &&
        other > (int64_t)now_ns) {
      l.borderline = true;
      if ((pedal_beat + k + dk) % SPEECH_CHORD_BEATS == phase) {
        due = other;
        k += dk;
      }
    }
  }
  while (due <= (int64_t)now_ns) {
    due += beat;
    k++;
  }
  l.due = (uint64_t)due;
  l.beat = pedal_beat + k;
  l.beats = (int)((due - (int64_t)onset_ns + beat / 2) / beat);
  l.earliest = pedal_ns ? l.due - (uint64_t)(SPEECH_EARLY_FRACTION * beat_ns)
                        : l.due;
  return l;
}

#endif
