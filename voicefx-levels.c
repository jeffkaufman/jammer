// Do the vocal effects sit where the vocoder does?  Plays the spoken number
// clips the speech recognizer has kept through the vocoder and through each
// of its alternatives (voicefx.h), and says how loud each came out, by
// perceived loudness at stage volume (loudness.h),
// against the vocoder, with the VFX_LEVEL that would bring it level.
//
//   make voicefx-levels && ./voicefx-levels [clip.wav ...]
//
// With no clips, all of them in
// ~/Library/Application Support/com.jefftk.jammer/numbers/heard.  Each is
// preceded by two seconds of its own first 50ms, looped, so the room gate has
// the room to settle on before the voice starts.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "whistle.h"
#include "loudness.h"

#define RATE 48000

static float* read_wav(const char* path, long* n) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* raw = malloc(size);
  if (fread(raw, 1, size, f) != (size_t)size) size = 0;
  fclose(f);
  long off = 0, bytes = 0;
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
  *n = bytes / 4;
  float* out = malloc(bytes + 4);
  memcpy(out, raw + off, bytes);
  free(raw);
  return out;
}

// The clip through `fx`, its perceived loudness in dB.
static double loudness(int fx, const float* clip, long n) {
  long lead = 2 * RATE, total = lead + n + RATE;
  float* out = calloc(total, sizeof(float));
  double chord_hz[VOCODER_VOICES], chord_weight[VOCODER_VOICES];
  int chord_notes = vocoder_chord(chord_hz, chord_weight);
  vocoder_prepare(RATE);
  vfx_prepare(RATE);
  VfxBlock b;
  audio_block_ns = 1000000000ULL;
  if (fx > VFX_VOCODER) vfx_block(&b);
  long room = n < RATE / 20 ? n : RATE / 20;
  for (long i = 0; i < total; i++) {
    float in = i < lead ? (room ? clip[i % room] : 0)
             : i < lead + n ? clip[i - lead] : 0;
    float l = fx == VFX_VOCODER
      ? vocoder_process(in, chord_hz, chord_weight, chord_notes, 1)
      : vfx_process(fx, in, &b);
    out[i] = i < lead ? 0 : l;
  }
  double db = perceived_loudness(out, (int)total, RATE);
  free(out);
  return db;
}

static char paths[512][1024];
static int n_paths;

int main(int argc, char** argv) {
  for (int i = 1; i < argc && n_paths < 512; i++) {
    snprintf(paths[n_paths++], sizeof(paths[0]), "%s", argv[i]);
  }
  if (argc < 2) {
    char dir[900];
    snprintf(dir, sizeof(dir),
             "%s/Library/Application Support/com.jefftk.jammer/numbers/heard",
             getenv("HOME"));
    DIR* d = opendir(dir);
    struct dirent* ent;
    while (d && (ent = readdir(d)) && n_paths < 512) {
      size_t len = strlen(ent->d_name);
      if (len > 4 && strcmp(ent->d_name + len - 4, ".wav") == 0) {
        snprintf(paths[n_paths++], sizeof(paths[0]), "%s/%s", dir,
                 ent->d_name);
      }
    }
    if (d) closedir(d);
  }

  // Summed per effect over the clips it made a sound for: the basses are
  // silent for a clip they never heard a pitch in, and that's no level.
  double sum[N_VFX] = {0};
  int counted[N_VFX] = {0};
  int clips = 0;
  for (int p = 0; p < n_paths; p++) {
    long n;
    float* clip = read_wav(paths[p], &n);
    if (!clip || n < RATE / 10) {
      free(clip);
      continue;
    }
    double vocoder = loudness(VFX_VOCODER, clip, n);
    for (int fx = VFX_ROBOT; fx < N_VFX; fx++) {
      double db = loudness(fx, clip, n);
      if (isfinite(db) && isfinite(vocoder)) {
        sum[fx] += db - vocoder;
        counted[fx]++;
      }
    }
    clips++;
    free(clip);
  }
  if (!clips) {
    printf("no clips\n");
    return 1;
  }
  printf("%d clips, against the vocoder:\n", clips);
  for (int fx = VFX_ROBOT; fx < N_VFX; fx++) {
    double db = counted[fx] ? sum[fx] / counted[fx] : 0;
    printf("  %-8s %+5.1f dB   VFX_LEVEL %.2f -> %.2f\n", WHISTLE_FX[fx].name,
           db, VFX_LEVEL[fx], VFX_LEVEL[fx] * pow(10, -db / 20));
  }
  return 0;
}
