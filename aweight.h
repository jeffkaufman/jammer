#ifndef JML_AWEIGHT_H
#define JML_AWEIGHT_H

// A-weighted loudness, for comparing drum sounds against each other.
//
// Peak amplitude is the wrong yardstick here.  The ear is far less sensitive
// down where a kick lives than up where a hihat does -- about 30dB less at
// 50Hz than at 3kHz -- so matching two sounds by peak leaves the kick much
// quieter than the hat it is supposed to sit under.  A-weighting (IEC 61672)
// is the standard curve for that difference.
//
// The weighting is applied in the frequency domain, from the closed form of
// the analog curve.  The obvious alternative, a bilinear-transformed biquad
// cascade, was tried first and thrown out: it tracks the standard to within
// 0.1dB up to 5kHz but drifts badly above that -- 3.3dB low at 12.5kHz and
// 8.6dB low at 16kHz at a 44.1kHz rate -- because the bilinear transform
// compresses the frequency axis near Nyquist.  That error lands squarely on
// hihats and cymbals, which is precisely what this is for.
//
// Loudness is the loudest frame of the weighted signal, in dB, where a frame
// is about 190ms.  That length is deliberate: it is roughly the ear's
// integration time, so a short click and a long boom carrying the same total
// energy don't come out equally loud.  Plain energy over the whole hit would
// rate a long quiet decay above a short sharp one.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The curve
// ---------------------------------------------------------------------------

#define AW_F1 20.598997
#define AW_F2 107.65265
#define AW_F3 737.86223
#define AW_F4 12194.217
#define AW_GAIN_1K_DB 1.9997  // makes the curve 0dB at 1kHz

// |H_A(f)|, the analog A-weighting magnitude.  Exact at every frequency;
// nothing here is discretized.
static double aweight_gain(double hz) {
  if (hz <= 0) return 0;
  double f2 = hz * hz;
  double f1s = AW_F1 * AW_F1, f2s = AW_F2 * AW_F2;
  double f3s = AW_F3 * AW_F3, f4s = AW_F4 * AW_F4;

  double num = f4s * f2 * f2;
  double den = (f2 + f1s) * sqrt((f2 + f2s) * (f2 + f3s)) * (f2 + f4s);
  return (num / den) * pow(10.0, AW_GAIN_1K_DB / 20.0);
}

static double aweight_gain_db(double hz) {
  double g = aweight_gain(hz);
  return g > 0 ? 20.0 * log10(g) : -INFINITY;
}

// ---------------------------------------------------------------------------
// Measuring
// ---------------------------------------------------------------------------

// 8192 at 44.1kHz is 186ms, close enough to the ear's integration time.
#define AW_FFT_ORDER 13
#define AW_FRAME (1 << AW_FFT_ORDER)
#define AW_HOP (AW_FRAME / 4)

// In-place iterative radix-2 FFT.
static void aw_fft(double* re, double* im) {
  for (int i = 1, j = 0; i < AW_FRAME; i++) {  // bit-reversal permutation
    int bit = AW_FRAME >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= AW_FRAME; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    double wr = cos(angle), wi = sin(angle);
    for (int i = 0; i < AW_FRAME; i += len) {
      double cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        double tr = re[b] * cr - im[b] * ci;
        double ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr;        im[a] += ti;
        double ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

// The A-weighted mean square of one frame, already windowed.
static double aw_frame_power(const float* x, int n, double rate) {
  static double re[AW_FRAME], im[AW_FRAME];
  static double window[AW_FRAME], weight[AW_FRAME];
  static double built_for_rate = 0;

  if (built_for_rate != rate) {
    for (int i = 0; i < AW_FRAME; i++) {
      window[i] = 0.5 * (1 - cos(2 * M_PI * i / AW_FRAME));  // Hann
      int bin = i <= AW_FRAME / 2 ? i : AW_FRAME - i;        // mirror
      double g = aweight_gain(bin * rate / AW_FRAME);
      weight[i] = g * g;
    }
    built_for_rate = rate;
  }

  for (int i = 0; i < AW_FRAME; i++) {
    re[i] = (i < n ? x[i] : 0.0) * window[i];
    im[i] = 0;
  }
  aw_fft(re, im);

  // Parseval: the mean square of the windowed frame is the summed bin power
  // over N^2.  Dividing by the Hann window's own mean square (3/8) takes the
  // window back out, so the number means the same as an unwindowed RMS.
  double power = 0;
  for (int i = 0; i < AW_FRAME; i++) {
    power += (re[i] * re[i] + im[i] * im[i]) * weight[i];
  }
  return power / ((double)AW_FRAME * AW_FRAME) / 0.375;
}

// The loudest frame of the A-weighted signal, in dB.  -INFINITY for silence.
static double aweight_loudness(const float* samples, int n, double rate) {
  double best = 0;
  for (int start = 0; start == 0 || start + AW_FRAME <= n; start += AW_HOP) {
    double power = aw_frame_power(samples + start, n - start, rate);
    if (power > best) best = power;
  }
  return best > 0 ? 10.0 * log10(best) : -INFINITY;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

// Measures a sine through the whole path -- windowing, FFT, weighting -- and
// checks it against the published curve, relative to 1kHz.  A weighting
// that's silently wrong produces confident wrong numbers, which is worse
// than not weighting at all, so nothing should trust this without running it.
static bool aweight_self_test(double rate, bool verbose) {
  struct { double hz, want; } EXPECTED[] = {
    {31.5, -39.4}, {63, -26.2}, {125, -16.1}, {250, -8.6}, {500, -3.2},
    {1000, 0.0}, {2000, 1.2}, {4000, 1.0}, {8000, -1.1}, {12500, -4.3},
    {16000, -6.6},
  };
  int n = (int)(sizeof(EXPECTED) / sizeof(EXPECTED[0]));
  static float tone[AW_FRAME * 2];
  bool ok = true;

  double reference = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < AW_FRAME * 2; k++) {
        tone[k] = (float)sin(2 * M_PI * EXPECTED[i].hz * k / rate);
      }
      double got = aweight_loudness(tone, AW_FRAME * 2, rate);
      if (EXPECTED[i].hz == 1000 && pass == 0) reference = got;
      if (pass == 0) continue;

      double relative = got - reference;
      double off = fabs(relative - EXPECTED[i].want);
      // 0.4dB: everything lands inside 0.1dB except the bottom octave, where
      // the curve is steep enough across one FFT bin that the window's
      // leakage biases the result about 0.3dB high.  Kicks live at 50-60Hz,
      // where that bias has already faded to nothing.
      if (off > 0.4) ok = false;
      if (verbose || off > 0.4) {
        printf("  %7.1f Hz  want %6.1f dB  got %6.1f dB  %s\n",
               EXPECTED[i].hz, EXPECTED[i].want, relative,
               off > 0.4 ? "OFF" : "ok");
      }
    }
  }
  return ok;
}

#endif
