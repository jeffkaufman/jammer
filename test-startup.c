// Checks the MIDI jammer emits during startup, with stub platform functions
// instead of a real synth.
//
// Guards against a class of bug where the configuration looks right in memory
// but the synth was never told: jammer used to leave every channel with
// expression (CC11) at 0 after startup, which is silence, because
// clear_configuration() sent the fade before clear_status() had restored it.
//
//   make test-mac
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "common.h"

static int cc11[16], cc7[16], prog[16];
static bool saw_cc11[16];

uint64_t now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
int attempt(int r, char* m) { return r; }
void send_midi(int action, int note, int velocity, int endpoint) {
  if (endpoint < 0 || endpoint > 15) return;
  if (action == MIDI_CC && note == 0x0b) { cc11[endpoint] = velocity;
                                           saw_cc11[endpoint] = true; }
  if (action == MIDI_CC && note == 0x07) cc7[endpoint] = velocity;
}
void choose_voice(int channel, int bank, int voice) { prog[channel] = voice; }

#include "jammermidilib.h"
#include "voices.h"

static int failures = 0;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
      printf("FAIL: "); printf(__VA_ARGS__); printf("\n");      \
      failures++;                                               \
    }                                                           \
  } while (0)

int main(void) {
  jml_setup();
  printf("after jml_setup():\n");
  printf("  ch  expression(CC11)  volume(CC7)  program\n");
  int silent = 0;
  for (int i = 0; i < N_ENDPOINTS; i++) {
    printf("  %2d   %s%-3d%s              %-3d          %d\n", i,
           saw_cc11[i] && cc11[i] == 0 ? ">>" : "  ", cc11[i],
           saw_cc11[i] && cc11[i] == 0 ? "<<" : "  ", cc7[i], prog[i]);
    if (saw_cc11[i] && cc11[i] == 0) silent++;
  }
  printf("\nfade_value=%d fade_target=%d MAX_FADE=%d\n",
         fade_value, fade_target, MAX_FADE);
  CHECK(silent == 0, "%d of %d endpoints left with expression 0 (silent)",
        silent, N_ENDPOINTS);
  for (int i = 0; i < N_ENDPOINTS; i++) {
    CHECK(cc11[i] == MAX_FADE, "endpoint %d expression is %d, want %d",
          i, cc11[i], MAX_FADE);
    if (i != ENDPOINT_DRUM) {
      CHECK(cc7[i] > 0, "endpoint %d volume is 0", i);
    }
  }

  // A fade-out followed by a reset used to leave everything silent too.
  fade_target = 0;
  for (int i = 0; i < MAX_FADE + 10; i++) progress_fades();
  CHECK(cc11[ENDPOINT_LOW] == 0, "fade-out should reach expression 0");
  full_reset();
  for (int i = 0; i < N_ENDPOINTS; i++) {
    CHECK(cc11[i] == MAX_FADE,
          "endpoint %d still silent after reset following a fade-out", i);
  }

  if (failures) { printf("\n%d failure(s)\n", failures); return 1; }
  printf("\nstartup tests passed: every endpoint audible\n");
  return 0;
}
