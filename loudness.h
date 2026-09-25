#ifndef JML_LOUDNESS_H
#define JML_LOUDNESS_H

// Perceived loudness, for levelling sounds against each other: the drum
// kits, the pads, the whistle, the vocal effects.
//
// Peak amplitude is the wrong yardstick.  The ear is less sensitive down
// where a kick lives than up where a hihat does, so matching two sounds by
// peak leaves the kick quieter than the hat it's supposed to sit under.  The
// weighting here is the ear's own, from the ISO 226:2003 equal-loudness
// contours, at LOUDNESS_PHON -- how loud the rig actually plays.
//
// That level is the point.  This used to be A-weighting, which is the ear at
// a quiet 40 phon, and the ear's bass gets much better as things get loud:
// at 75Hz A-weighting takes off about 23dB, where at 90 phon the ear is 12dB
// down.  Levelled that way on a loud rig, anything mostly bass -- a sub, an
// 808 -- came out about 10dB louder than it measured.
//
// The weighting is applied in the frequency domain, from the contour
// itself.  A bilinear-transformed filter was tried first, for A-weighting,
// and thrown out: it drifted 3-9dB low above 12kHz at 44.1kHz, because the
// transform compresses the frequency axis near Nyquist, squarely on hihats
// and cymbals.
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

// Stage volume: a loud PA, at the dancers.
#define LOUDNESS_PHON 90

// ISO 226:2003, table 1: at each frequency, the exponent for loudness
// perception, the magnitude of the linear transfer function normalized at
// 1kHz, and the threshold of hearing.
#define LD_POINTS 29
static const double LD_HZ[LD_POINTS] = {
  20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
  630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
  10000, 12500,
};
static const double LD_ALPHA[LD_POINTS] = {
  0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330,
  0.315, 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244,
  0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301,
};
static const double LD_LU[LD_POINTS] = {
  -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1,
  -2.0, -1.1, -0.4, 0.0, 0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7, 2.5, 1.2,
  -2.1, -7.1, -11.2, -10.7, -3.1,
};
static const double LD_TF[LD_POINTS] = {
  78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4,
  8.6, 6.2, 4.4, 3.0, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6.0, -5.4, -1.5, 6.0,
  12.6, 13.9, 12.3,
};

// The sound pressure level a tone at table point `i` needs to sound as loud
// as `phon`: ISO 226's formula (4.1).
static double ld_contour_point(int i, double phon) {
  double af = 4.47e-3 * (pow(10, 0.025 * phon) - 1.15) +
              pow(0.4 * pow(10, (LD_TF[i] + LD_LU[i]) / 10 - 9),
                  LD_ALPHA[i]);
  return 10 / LD_ALPHA[i] * log10(af) - LD_LU[i] + 94;
}

// And at any frequency: between the table's points in log frequency, and
// past its ends falling away steeply -- 24dB an octave under 20Hz and 12
// over 12.5kHz, where the standard stops and the ear is going with it.
static double loudness_contour_db(double hz, double phon) {
  if (hz <= LD_HZ[0]) {
    return ld_contour_point(0, phon) + 24 * log2(LD_HZ[0] / fmax(hz, 1));
  }
  if (hz >= LD_HZ[LD_POINTS - 1]) {
    return ld_contour_point(LD_POINTS - 1, phon) +
           12 * log2(hz / LD_HZ[LD_POINTS - 1]);
  }
  int i = 0;
  while (LD_HZ[i + 1] < hz) i++;
  double t = log(hz / LD_HZ[i]) / log(LD_HZ[i + 1] / LD_HZ[i]);
  return ld_contour_point(i, phon) +
         t * (ld_contour_point(i + 1, phon) - ld_contour_point(i, phon));
}

// The weighting, as a gain: how much louder than a 1kHz tone of the same
// level a tone at `hz` sounds, at LOUDNESS_PHON -- the contour upside down,
// and 0dB at 1kHz.
static double loudness_gain(double hz) {
  if (hz <= 0) return 0;
  double db = loudness_contour_db(1000, LOUDNESS_PHON) -
              loudness_contour_db(hz, LOUDNESS_PHON);
  return pow(10.0, db / 20.0);
}

// ---------------------------------------------------------------------------
// Measuring
// ---------------------------------------------------------------------------

// 8192 at 44.1kHz is 186ms, close enough to the ear's integration time.
#define LD_FFT_ORDER 13
#define LD_FRAME (1 << LD_FFT_ORDER)
#define LD_HOP (LD_FRAME / 4)

// In-place iterative radix-2 FFT.
static void ld_fft(double* re, double* im) {
  for (int i = 1, j = 0; i < LD_FRAME; i++) {  // bit-reversal permutation
    int bit = LD_FRAME >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= LD_FRAME; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    double wr = cos(angle), wi = sin(angle);
    for (int i = 0; i < LD_FRAME; i += len) {
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

// The weighted mean square of one frame, already windowed.
static double ld_frame_power(const float* x, int n, double rate) {
  static double re[LD_FRAME], im[LD_FRAME];
  static double window[LD_FRAME], weight[LD_FRAME];
  static double built_for_rate = 0;

  if (built_for_rate != rate) {
    for (int i = 0; i < LD_FRAME; i++) {
      window[i] = 0.5 * (1 - cos(2 * M_PI * i / LD_FRAME));  // Hann
      int bin = i <= LD_FRAME / 2 ? i : LD_FRAME - i;        // mirror
      double g = loudness_gain(bin * rate / LD_FRAME);
      weight[i] = g * g;
    }
    built_for_rate = rate;
  }

  for (int i = 0; i < LD_FRAME; i++) {
    re[i] = (i < n ? x[i] : 0.0) * window[i];
    im[i] = 0;
  }
  ld_fft(re, im);

  // Parseval: the mean square of the windowed frame is the summed bin power
  // over N^2.  Dividing by the Hann window's own mean square (3/8) takes the
  // window back out, so the number means the same as an unwindowed RMS.
  double power = 0;
  for (int i = 0; i < LD_FRAME; i++) {
    power += (re[i] * re[i] + im[i] * im[i]) * weight[i];
  }
  return power / ((double)LD_FRAME * LD_FRAME) / 0.375;
}

// The loudest frame of the weighted signal, in dB.  -INFINITY for silence.
static double perceived_loudness(const float* samples, int n, double rate) {
  double best = 0;
  for (int start = 0; start == 0 || start + LD_FRAME <= n; start += LD_HOP) {
    double power = ld_frame_power(samples + start, n - start, rate);
    if (power > best) best = power;
  }
  return best > 0 ? 10.0 * log10(best) : -INFINITY;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

// Checks the curve against the standard, then measures sines through the
// whole path -- windowing, FFT, weighting -- and checks them against the
// curve, relative to 1kHz.  A weighting that's silently wrong produces
// confident wrong numbers, which is worse than not weighting at all, so
// nothing should trust this without running it.
static bool loudness_self_test(double rate, bool verbose) {
  bool ok = true;

  // The formula, at 40 phon, against the standard's own table of that
  // contour (ISO 226:2003, annex): if the constants are wrong, this is off.
  static const double CONTOUR_40[LD_POINTS] = {
    99.85, 93.94, 88.17, 82.63, 77.78, 73.08, 68.48, 64.37, 60.59, 56.70,
    53.41, 50.40, 47.58, 44.98, 43.05, 41.34, 40.06, 40.01, 41.82, 42.51,
    39.23, 36.51, 35.61, 36.65, 40.01, 45.83, 51.80, 54.28, 51.49,
  };
  for (int i = 0; i < LD_POINTS; i++) {
    double got = ld_contour_point(i, 40);
    double off = fabs(got - CONTOUR_40[i]);
    if (off > 0.05) ok = false;
    if (verbose || off > 0.05) {
      printf("  %7.1f Hz  40 phon: want %6.2f dB SPL  got %6.2f  %s\n",
             LD_HZ[i], CONTOUR_40[i], got, off > 0.05 ? "OFF" : "ok");
    }
  }

  // The measurement, against the curve.
  static const double HZ[] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000,
                              8000, 12500};
  int n = (int)(sizeof(HZ) / sizeof(HZ[0]));
  static float tone[LD_FRAME * 2];
  double reference = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < LD_FRAME * 2; k++) {
        tone[k] = (float)sin(2 * M_PI * HZ[i] * k / rate);
      }
      double got = perceived_loudness(tone, LD_FRAME * 2, rate);
      if (HZ[i] == 1000 && pass == 0) reference = got;
      if (pass == 0) continue;

      double want = 20 * log10(loudness_gain(HZ[i]));
      double relative = got - reference;
      double off = fabs(relative - want);
      // 0.4dB: everything lands inside 0.1dB except the bottom octave, where
      // the curve is steep enough across one FFT bin that the window's
      // leakage biases the result.
      if (off > 0.4) ok = false;
      if (verbose || off > 0.4) {
        printf("  %7.1f Hz  want %6.1f dB  got %6.1f dB  %s\n", HZ[i], want,
               relative, off > 0.4 ? "OFF" : "ok");
      }
    }
  }
  return ok;
}

#endif
