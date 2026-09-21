#ifndef JML_WHISTLE_NOTE_H
#define JML_WHISTLE_NOTE_H

// Turning the pitch detector's running output into single whistled notes, for
// the whistle choosing the chord (whistle_chooses_notes).
//
// A note counts once it has sounded for at least WN_MIN_NOTE_S and then
// stopped, and it is reported only when it stops.  Waiting for the end is the
// point: a whistle often scoops into its note, and deciding at the start would
// pick the chord the scoop passed through.
//
// What it reports is the note's average pitch, weighted to the middle (a Hann
// window over its length), so the scoop in and the fall-off at the end count
// for little and the held part for most.
//
// No allocation and no locks: it runs on the audio thread, once per block.

#include <math.h>
#include <stdbool.h>

// Kept as 10ms bins rather than one entry per audio block, so the history is
// the same size whatever the block size is.
#define WN_BIN_S 0.010
// Ten seconds of note.  A note held longer is weighted over its first ten
// seconds, which is still the part in the middle of any reasonable note.
#define WN_MAX_BINS 1000
#define WN_MIN_NOTE_S 0.150
// How long the pitch has to be gone before the note counts as over, so that
// the detector briefly losing a note mid-whistle doesn't cut it in two.
#define WN_GAP_S 0.050

typedef struct {
  bool in_note;
  double voiced_s;   // how long the note has sounded so far
  double gap_s;      // how long it's been silent, while in a note
  double bin_sum;    // semitones x seconds in the bin being filled
  double bin_s;      // seconds in the bin being filled
  int n_bins;
  float bins[WN_MAX_BINS];  // each bin's mean pitch, in MIDI semitones
} WhistleNoteTracker;

static void wn_reset(WhistleNoteTracker* t) {
  t->in_note = false;
  t->voiced_s = 0;
  t->gap_s = 0;
  t->bin_sum = 0;
  t->bin_s = 0;
  t->n_bins = 0;
}

static void wn_close_bin(WhistleNoteTracker* t) {
  if (t->bin_s <= 0) return;
  if (t->n_bins < WN_MAX_BINS) {
    t->bins[t->n_bins++] = (float)(t->bin_sum / t->bin_s);
  }
  t->bin_sum = 0;
  t->bin_s = 0;
}

// The Hann-weighted mean of the bins.
static double wn_weighted_pitch(const WhistleNoteTracker* t) {
  double sum = 0, weights = 0;
  for (int i = 0; i < t->n_bins; i++) {
    double s = sin(M_PI * (i + 0.5) / t->n_bins);
    double w = s * s;
    sum += w * t->bins[i];
    weights += w;
  }
  return sum / weights;
}

// Feed one block: whether the detector heard a note, at what frequency, and
// how long the block was.  Returns true when a note has just finished, with
// its pitch in MIDI semitones (fractional) in *pitch_out.
static bool wn_feed(WhistleNoteTracker* t, bool voiced, double hz,
                    double block_s, double* pitch_out) {
  if (voiced && hz > 0) {
    if (!t->in_note) {
      wn_reset(t);
      t->in_note = true;
    }
    t->gap_s = 0;
    t->voiced_s += block_s;
    t->bin_sum += (69.0 + 12.0 * log2(hz / 440.0)) * block_s;
    t->bin_s += block_s;
    if (t->bin_s >= WN_BIN_S) wn_close_bin(t);
    return false;
  }

  if (!t->in_note) return false;
  t->gap_s += block_s;
  if (t->gap_s < WN_GAP_S) return false;

  wn_close_bin(t);
  bool counts = t->voiced_s >= WN_MIN_NOTE_S && t->n_bins > 0;
  if (counts) *pitch_out = wn_weighted_pitch(t);
  wn_reset(t);
  return counts;
}

#endif
