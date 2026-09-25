// Does the microphone breathe when it should, and how much when it shouldn't?
// Runs breathmic.h's detectors -- Blow Noise, and Whistle Breath
// on the whistle engine's own pitch detector -- over recordings, and over
// synthetic noise standing in for blowing, and says how much breath each one
// found.
//
//   ./breathmic-eval [--gate N] [--full N] [--blow-gate N] [--blow-full N]
//                    file ...
//
// .wav is 32-bit float, the way jammer keeps its number clips; .f32 is raw
// mono float at 48kHz, the way whistle-synth keeps its recordings.  With no
// files, it runs the synthetic noise and the number clips in
// ~/Library/Application Support/com.jefftk.jammer/numbers.
//
// For each: the peak breath (0-127, as the breath controller would send it)
// and the share of the time the Breath Gate would be open.  Speech should
// score little; a whistle should score on Whistle Breath; the noise on Blow
// Noise.

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "breathmic.h"
#include "engine.h"

#define RATE 48000

static int full_step = 5;       // Whistle Breath's
static int gate_step = 8;       // and its gate, as jammer's knob has it
static int blow_full_step = 3;  // the Blows'
static int blow_gate_step = 3;  // and their gate

static float* read_audio(const char* path, long* n) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* raw = malloc(size);
  if (fread(raw, 1, size, f) != (size_t)size) size = 0;
  fclose(f);
  long off = 0, bytes = size;
  if (size > 12 && memcmp(raw, "RIFF", 4) == 0) {
    bytes = 0;
    for (long p = 12; p + 8 <= size;) {
      uint32_t len;
      memcpy(&len, raw + p + 4, 4);
      if (memcmp(raw + p, "data", 4) == 0) {
        off = p + 8;
        bytes = len < size - off ? len : size - off;
        break;
      }
      p += 8 + len + (len & 1);
    }
  }
  *n = bytes / 4;
  float* out = malloc(bytes + 4);
  memcpy(out, raw + off, bytes);
  free(raw);
  return out;
}

typedef struct {
  int peak;
  double open;  // share of the time over BREATH_GATE_OPEN
} Score;

static void count(Score* sc, int cc, long* open) {
  if (cc > sc->peak) sc->peak = cc;
  *open += breath_blown(cc) > BREATH_GATE_OPEN;
}

// Noise, volume, whistle.
static void score(const float* s, long n, Score* out) {
  static struct Engine e;
  engine_init(&e, RATE);
  // jammer's knob, five steps past whistle-synth's: see whistle_gate_margin.
  pitch_set_gate(&e.detector, 1.5f * powf(10.0f, 0.1f * (14 - gate_step)));
  BreathMic bm[3];
  for (int k = 0; k < 3; k++) bm_init(&bm[k], RATE);
  double full = engine_level_full_for_step(full_step);
  double blow_full = engine_level_full_for_step(blow_full_step);
  double blow_gate = bm_blow_gate_for_step(blow_gate_step);
  long open[3] = {0};
  memset(out, 0, 3 * sizeof(*out));
  for (long i = 0; i < n; i++) {
    float l, r;
    engine_process_stereo(&e, s[i], &l, &r);
    count(&out[0], bm_cc(bm_blow_noise(&bm[0], s[i], blow_gate, blow_full)),
          &open[0]);
    count(&out[2], bm_cc(bm_whistle(&bm[2], e.detector.hint.voiced,
                                    e.detector.hint.level, full)), &open[2]);
  }
  for (int k = 0; k < 3; k++) out[k].open = n ? (double)open[k] / n : 0;
}

static void report(const char* name, const float* s, long n) {
  Score sc[3];
  score(s, n, sc);
  double rms = 0;
  for (long i = 0; i < n; i++) rms += (double)s[i] * s[i];
  rms = n ? sqrt(rms / n) : 0;
  printf("%-36.36s %6.1fs rms %.4f   noise %3d %5.1f%%   whistle %3d %5.1f%%\n",
         name, (double)n / RATE, rms, sc[0].peak, 100 * sc[0].open,
         sc[2].peak, 100 * sc[2].open);
}

static uint32_t rng = 12345;
static double white(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (double)rng / 2147483648.0 - 1;
}

// Noise at `rms`, tilted: 0 white, -3dB/oct pink, -6dB/oct brown, as one-pole
// sums good enough for this.
static void synth_noise(const char* name, int tilt, double rms) {
  long n = RATE * 2;
  float* s = malloc(sizeof(float) * n);
  double b[7] = {0}, lp = 0, sum = 0;
  for (long i = 0; i < n; i++) {
    double w = white(), x = w;
    if (tilt == 1) {  // Paul Kellet's pink
      b[0] = 0.99886 * b[0] + w * 0.0555179;
      b[1] = 0.99332 * b[1] + w * 0.0750759;
      b[2] = 0.96900 * b[2] + w * 0.1538520;
      b[3] = 0.86650 * b[3] + w * 0.3104856;
      b[4] = 0.55000 * b[4] + w * 0.5329522;
      b[5] = -0.7616 * b[5] - w * 0.0168980;
      x = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362;
      b[6] = w * 0.115926;
    } else if (tilt == 2) {
      lp = 0.995 * lp + 0.1 * w;
      x = lp;
    }
    s[i] = (float)x;
    sum += x * x;
  }
  double scale = rms / sqrt(sum / n);
  for (long i = 0; i < n; i++) s[i] *= (float)scale;
  char label[64];
  snprintf(label, sizeof(label), "%s @ %.3f", name, rms);
  report(label, s, n);
  free(s);
}

static void run_file(const char* path) {
  long n;
  float* s = read_audio(path, &n);
  if (!s) {
    printf("%s: can't read\n", path);
    return;
  }
  const char* slash = strrchr(path, '/');
  report(slash ? slash + 1 : path, s, n);
  free(s);
}

static void run_dir(const char* dir) {
  DIR* d = opendir(dir);
  if (!d) return;
  struct dirent* ent;
  while ((ent = readdir(d))) {
    size_t len = strlen(ent->d_name);
    if (len > 4 && strcmp(ent->d_name + len - 4, ".wav") == 0) {
      char path[2048];
      snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
      run_file(path);
    }
  }
  closedir(d);
}

int main(int argc, char** argv) {
  int first = 1;
  while (first + 1 < argc && strncmp(argv[first], "--", 2) == 0) {
    if (strcmp(argv[first], "--gate") == 0) {
      gate_step = atoi(argv[first + 1]);
    } else if (strcmp(argv[first], "--full") == 0) {
      full_step = atoi(argv[first + 1]);
    } else if (strcmp(argv[first], "--blow-gate") == 0) {
      blow_gate_step = atoi(argv[first + 1]);
    } else if (strcmp(argv[first], "--blow-full") == 0) {
      blow_full_step = atoi(argv[first + 1]);
    }
    first += 2;
  }
  printf("full blow: whistle step %d, %.3f; blows step %d, %.3f\n",
         full_step, engine_level_full_for_step(full_step), blow_full_step,
         engine_level_full_for_step(blow_full_step));
  if (first < argc) {
    for (int i = first; i < argc; i++) run_file(argv[i]);
    return 0;
  }
  const char* names[] = {"white noise", "pink noise", "brown noise"};
  double levels[] = {0.003, 0.01, 0.03, 0.1};
  for (int t = 0; t < 3; t++) {
    for (int l = 0; l < 4; l++) synth_noise(names[t], t, levels[l]);
  }
  char dir[1024];
  snprintf(dir, sizeof(dir),
           "%s/Library/Application Support/com.jefftk.jammer/numbers",
           getenv("HOME"));
  run_dir(dir);
  strncat(dir, "/heard", sizeof(dir) - strlen(dir) - 1);
  run_dir(dir);
  return 0;
}
