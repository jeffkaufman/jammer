// Do the mandolin's effects sit where the mandolin does, played for real?
// Plays the recordings Mandolin > Record Mandolin keeps through each of its
// effects (mandolin.h), one at a time, and says how loud each came out, by
// perceived loudness at stage volume (loudness.h), against the mandolin
// as it came in, and how near full scale it peaked -- plain, and at Breath
// FX's 300%, before Boost or the Mandolin volume.
//
//   make mandolin-levels && ./mandolin-levels [recording.wav ...]
//
// With no recordings, all of them in
// ~/Library/Application Support/com.jefftk.jammer/mandolin.  A recording
// that's all but silent -- one of the room, for the gate -- is only
// measured for its noise floor.
//
// And for each, how much of it Shimmer's detector took for pitched: a take
// of scratches alone should be mostly not, and one of ringing chords mostly.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "whistle.h"
#include "loudness.h"
#include "numrec.h"

#define RATE 48000
#define BLOCK 480

// The effects measured, and whether each is a voice beside the mandolin or
// the mandolin itself, through it.
static const int EFFECTS[] = {
  MANDO_BASS, MANDO_SYNTH, MANDO_OCTAVE, MANDO_SHIMMER, MANDO_VOCODER,
  MANDO_DRIVE, MANDO_LESLIE,
};
#define N_EFFECTS ((int)(sizeof(EFFECTS) / sizeof(EFFECTS[0])))

// The recording through `voices`, into `out`: the voices alone, or for the
// chain the mandolin through it.  Returns the peak of everything together.
static double run(const float* x, long n, unsigned voices, bool chain,
                  float* out) {
  mando_reset();
  mando_prepare(RATE);
  mando_voices = voices;
  mando_publish();
  double peak = 0;
  for (long at = 0; at < n; at += BLOCK) {
    int len = n - at < BLOCK ? (int)(n - at) : BLOCK;
    mando_process(x + at, len, RATE);
    for (int i = 0; i < len; i++) {
      out[at + i] = chain ? mando_block[i] : mando_voice_block[i];
      peak = fmax(peak, fabs(mando_block[i] + mando_voice_block[i]));
    }
  }
  return peak;
}

static double db(double x) { return 20 * log10(fmax(x, 1e-9)); }

static char paths[256][1024];
static int n_paths;

static int by_name(const void* a, const void* b) {
  return strcmp((const char*)a, (const char*)b);
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc && n_paths < 256; i++) {
    snprintf(paths[n_paths++], sizeof(paths[0]), "%s", argv[i]);
  }
  if (argc < 2) {
    char dir[900];
    snprintf(dir, sizeof(dir),
             "%s/Library/Application Support/com.jefftk.jammer/mandolin",
             getenv("HOME"));
    DIR* d = opendir(dir);
    struct dirent* ent;
    while (d && (ent = readdir(d)) && n_paths < 256) {
      size_t len = strlen(ent->d_name);
      if (len > 4 && strcmp(ent->d_name + len - 4, ".wav") == 0) {
        snprintf(paths[n_paths++], sizeof(paths[0]), "%s/%s", dir,
                 ent->d_name);
      }
    }
    if (d) closedir(d);
    qsort(paths, n_paths, sizeof(paths[0]), by_name);
  }
  if (!n_paths) {
    printf("no recordings: Mandolin > Record Mandolin makes them\n");
    return 1;
  }

  // D major, for the Synth and the Vocoder.
  atomic_store(&audio_chord_root, 2);
  atomic_store(&audio_chord_third, 4);
  atomic_store(&audio_chord_fifth, 7);
  audio_block_ns = 1000000000ULL;

  double sum[N_EFFECTS] = {0};
  int counted[N_EFFECTS] = {0};
  for (int p = 0; p < n_paths; p++) {
    long long n = 0;
    double rate = 0;
    float* x = nr_read_wav(paths[p], &n, &rate);
    const char* name = strrchr(paths[p], '/') ? strrchr(paths[p], '/') + 1
                                              : paths[p];
    if (!x || rate != RATE) {
      printf("%s: can't read, or not at %dHz\n", name, RATE);
      free(x);
      continue;
    }
    double peak = 0, power = 0;
    int clipped = 0;
    for (long long i = 0; i < n; i++) {
      peak = fmax(peak, fabs(x[i]));
      power += x[i] * x[i];
      if (fabsf(x[i]) >= 0.999f) clipped++;
    }
    printf("%s: %.1fs, peak %.1fdB, RMS %.1fdB", name, n / rate, db(peak),
           db(sqrt(power / n)));
    if (clipped) printf(", CLIPPED at the input (%d samples)", clipped);
    if (peak < 0.01) {
      printf(" -- the room: set the gate above that\n");
      free(x);
      continue;
    }

    // How much Shimmer's detector took for pitched, over what's loud.
    MandoLine line;
    mando_line_prepare(&line, RATE, TONAL_RATE);
    double env = 0, tonal_sum = 0;
    int looks = 0;
    for (long long i = 0; i < n; i++) {
      env = fmax(fabs(x[i]), env * 0.9995);
      double t = tonal_run(&line, x[i]);
      if (t >= 0 && env > peak * 0.05) {
        tonal_sum += t;
        looks++;
      }
    }
    printf(", %.0f%% pitched\n", looks ? 100 * tonal_sum / looks : 0);

    float* out = malloc(sizeof(float) * n);
    for (long long i = 0; i < n; i++) out[i] = x[i];
    double plain = perceived_loudness(out, (int)n, RATE);
    for (int e = 0; e < N_EFFECTS; e++) {
      int fx = EFFECTS[e];
      bool chain = fx >= MANDO_DRIVE;
      double fx_peak = run(x, n, MANDO_BIT(fx), chain, out);
      double loud = perceived_loudness(out, (int)n, RATE);
      atomic_store(&audio_breath, BREATH_FULL);
      double full_peak = run(x, n, MANDO_BIT(fx) | MANDO_BIT(MANDO_BREATH),
                             chain, out);
      atomic_store(&audio_breath, 0);
      bool silent = !isfinite(loud) || loud < plain - 60;
      char level[16] = " silent";
      if (!silent) {
        snprintf(level, sizeof(level), "%+5.1fdB", loud - plain);
        sum[e] += loud - plain;
        counted[e]++;
      }
      printf("  %-8s %-8s peak %+5.1fdB, %+5.1fdB at full breath%s\n",
             MANDO_VOICES[fx].name, level, db(fx_peak), db(full_peak),
             full_peak > 1 ? "  OVER" : "");
    }
    free(out);
    free(x);
  }

  printf("\nagainst the mandolin, on average over the recordings it played:\n");
  for (int e = 0; e < N_EFFECTS; e++) {
    if (!counted[e]) continue;
    printf("  %-8s %+5.1fdB\n", MANDO_VOICES[EFFECTS[e]].name,
           sum[e] / counted[e]);
  }
  return 0;
}
