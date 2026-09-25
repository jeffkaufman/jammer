#ifndef JML_BREATH_MIC_H
#define JML_BREATH_MIC_H

// The microphone as a breath controller: the whistle's breath voices, which
// make no sound of their own and instead drive everything the breath
// controller drives -- Flex, the jawharp, the sweeps, the Breath Gate.
//
//   Whistle Breath  how loud you're whistling, counted only while the
//                   whistle's pitch detector says it's a whistle: a note in
//                   the whistle's range, above its gate.  Talking is below
//                   that range and isn't periodic enough anyway, so calling
//                   chord numbers doesn't breathe.
//
//   Blow Noise      how hard you're blowing into the microphone, counted only
//                   as far as what's coming in is broad noise: how evenly its
//                   energy spreads over six octave bands, 250Hz to 8kHz.  A
//                   vowel piles up in the bottom two or three and a whistle
//                   in one, while blowing fills them all.  An "f" or an "s"
//                   is a short blow, though, so calling a chord can breathe a
//                   little.
//
// Blow Noise answers at once: the level is followed continuously rather than
// judged a window at a time, and nothing waits to be sure.  All three are a
// 0-1 level from a gate up to a full-blow knob -- the Blows' gate their own
// knob, Whistle Breath's BM_RANGE_DB under full, since the pitch detector's
// gate is what it starts from -- rising quickly and falling off like a
// breath.
//
// Plain C, so the tests and ./breathmic-eval can reach it.  Realtime safe: it
// runs on the audio thread, once a sample, and never locks or allocates.

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"

#define BM_BANDS 6
#define BM_LOW_HZ 250       // octaves up from here: 250, 500 ... 8000
#define BM_RANGE_DB 30      // Whistle Breath's: under full, down to nothing
#define BM_LEVEL_S 0.005    // how long the level is averaged over
#define BM_BANDS_S 0.010    // and the bands', which the lowest needs
#define BM_HOP 48           // samples between looks at the bands
#define BM_ATTACK_S 0.003
#define BM_RELEASE_S 0.080

// How even the bands have to be, as geometric over arithmetic mean of their
// energies: 1 for noise with the same energy in every octave (pink), about
// 0.6 for white noise or noise falling 3dB an octave, and mostly 0.2-0.5 for
// a whistle or a vowel.  Nothing counts below BM_NOISY_FROM, and everything
// counts from BM_NOISY_FULL.  Starting at 0.30 let a whistle breathe half
// the time it sounded; starting higher than this mostly takes away from
// blowing, since what speech gets through is its fricatives, which are noise.
#define BM_NOISY_FROM 0.45
#define BM_NOISY_FULL 0.60

typedef struct {
  double ic1, ic2, g, k, a1, a2;
} BmBand;

typedef struct {
  BmBand bands[BM_BANDS];
  double band_power[BM_BANDS];  // each band's, followed
  double power;                 // and the whole input's
  double level_k, bands_k;
  int hop;
  double level;     // RMS, for the meter and the full-blow knob
  double noisy;     // how broad the noise is, 0-1
  double out;       // what the breath follows
  double attack, release;
} BreathMic;

static void bm_band_set(BmBand* f, double hz, double q, double rate) {
  memset(f, 0, sizeof(*f));
  f->g = tan(M_PI * fmin(hz, 0.45 * rate) / rate);
  f->k = 1 / q;
  f->a1 = 1 / (1 + f->g * (f->g + f->k));
  f->a2 = f->g * f->a1;
}

static double bm_band_run(BmBand* f, double in) {
  double v1 = f->a1 * f->ic1 + f->a2 * (in - f->ic2);
  double v2 = f->ic2 + f->g * v1;
  f->ic1 = 2 * v1 - f->ic1;
  f->ic2 = 2 * v2 - f->ic2;
  return f->k * v1;
}

static void bm_init(BreathMic* m, double rate) {
  memset(m, 0, sizeof(*m));
  for (int b = 0; b < BM_BANDS; b++) {
    bm_band_set(&m->bands[b], BM_LOW_HZ * pow(2, b), M_SQRT2, rate);
  }
  m->level_k = 1 - exp(-1 / (rate * BM_LEVEL_S));
  m->bands_k = 1 - exp(-1 / (rate * BM_BANDS_S));
  m->attack = 1 - exp(-1 / (rate * BM_ATTACK_S));
  m->release = 1 - exp(-1 / (rate * BM_RELEASE_S));
}

// An RMS level from `gate` to `full`, 0-1, evenly in dB.
static double bm_loudness(double rms, double gate, double full) {
  if (rms <= gate || full <= 0) return 0;
  if (gate >= full * 0.9) gate = full * 0.9;
  double x = log(rms / gate) / log(full / gate);
  return x > 1 ? 1 : x;
}

// Where the Blows' gate knob starts breathing, for its 0-9: 1.4x a step,
// from 0.001, about the quietest room a vocal mic sees, up to 0.022.
static double bm_blow_gate_for_step(int step) {
  return 0.001 * pow(10, 0.15 * step);
}

// How even the bands' energies are, 0-1.  See BM_NOISY_FROM.
static double bm_flatness(const double* energy) {
  double log_sum = 0, sum = 0;
  for (int b = 0; b < BM_BANDS; b++) {
    double e = energy[b] + 1e-20;
    log_sum += log(e);
    sum += e;
  }
  return exp(log_sum / BM_BANDS) / (sum / BM_BANDS);
}

static double bm_follow(BreathMic* m, double target) {
  m->out += (target - m->out) * (target > m->out ? m->attack : m->release);
  return m->out;
}

static void bm_hear_level(BreathMic* m, float in) {
  m->power += ((double)in * in - m->power) * m->level_k;
  m->level = sqrt(m->power);
}

// Blow Noise: one sample in, the breath out.
static double bm_blow_noise(BreathMic* m, float in, double gate,
                            double full) {
  bm_hear_level(m, in);
  for (int b = 0; b < BM_BANDS; b++) {
    double y = bm_band_run(&m->bands[b], in);
    m->band_power[b] += (y * y - m->band_power[b]) * m->bands_k;
  }
  if (++m->hop >= BM_HOP) {
    m->hop = 0;
    double x = (bm_flatness(m->band_power) - BM_NOISY_FROM) /
               (BM_NOISY_FULL - BM_NOISY_FROM);
    m->noisy = x < 0 ? 0 : x > 1 ? 1 : x;
  }
  return bm_follow(m, m->noisy * bm_loudness(m->level, gate, full));
}

// Whistle Breath: the pitch detector's verdict and level in, the breath out.
static double bm_whistle(BreathMic* m, bool voiced, float level, double full) {
  m->level = level;
  double gate = full * pow(10, -BM_RANGE_DB / 20.0);
  return bm_follow(m, voiced ? bm_loudness(level, gate, full) : 0);
}

// A breath, 0-1, as the breath controller would send it: nothing at rest,
// and from just past its resting level up to as far as it goes.
static int bm_cc(double x) {
  if (x <= 0.002) return 0;
  return (int)lround(BREATH_FLOOR + fmin(x, 1) * (BREATH_FULL - BREATH_FLOOR));
}

#endif
