// whistlelevels.c -- is the whistle bass at the same perceived volume as the
// endpoints it plays alongside, and what does it take to get it there?
//
// The whistle and fluidsynth are two separate synthesis engines summed into
// the same buffers, mixed against each other by nothing at all.  This is
// where the numbers that mix them come from.
//
// The thing that actually sets the whistle's level is not its volume knob --
// it is `level_full`, the input level a voice treats as blowing as hard as
// the player is going to.  The voice spends the player's breath on loudness
// and brightness, so a `level_full` set well above what the microphone
// actually delivers leaves the voice permanently dark and quiet however far
// up the volume goes.  That is the failure this measures.
//
// Loudness is A-weighted (see aweight.h), not peak, for the same reason
// kitlevels.c weights: a bass note and a whistle at the same peak are nowhere
// near the same loudness.
//
// Build: make whistlelevels
// Run:   ./whistlelevels [--verbose]
//        ./whistlelevels --check    fails if the default has drifted off level

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <fluidsynth.h>

#include "common.h"

// jammermidilib.h wants a platform underneath it; we only want its voice
// table, so give it somewhere harmless to send MIDI and drive fluidsynth
// ourselves.  Same arrangement as kitlevels.c.
uint64_t now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
int attempt(int r, char* m) { (void)m; return r; }
void send_midi(int action, int note, int velocity, int endpoint) {
  (void)action; (void)note; (void)velocity; (void)endpoint;
}
void choose_voice(int channel, int bank, int voice) {
  (void)channel; (void)bank; (void)voice;
}

#include "jammermidilib.h"
#include "voices.h"
#include "aweight.h"
#include "engine.h"

#define RATE 48000            // what the rig came up at; see the README

// A second and a half of room before the note, then two seconds of note.
//
// The lead-in is not padding.  The detector's gate is relative to the noise
// floor it has measured, it starts pessimistic at 0.01, and it only *learns*
// the floor while what it is hearing is not periodic -- so a test that hands
// it a pure tone from a cold start leaves the gate stuck shut above the tone
// and measures silence.  Real use always has room first.
#define LEAD_FRAMES (RATE * 3 / 2)
#define NOTE_FRAMES (RATE * 2)
#define RENDER_FRAMES (LEAD_FRAMES + NOTE_FRAMES)

static float left[RENDER_FRAMES], right[RENDER_FRAMES];
static float mono[RENDER_FRAMES];

// ---------------------------------------------------------------------------
// The reference: what an endpoint actually sounds like
// ---------------------------------------------------------------------------

// The foot bass as jml_setup leaves it -- program 39, CC7 66, gain 1.0 --
// playing a low E.  That is the endpoint the whistle is most directly
// competing with, and the one it would be doubling.
static double measure_fluidsynth(int program, int cc7, int note, int velocity) {
  fluid_settings_t* settings = new_fluid_settings();
  fluid_settings_setnum(settings, "synth.sample-rate", RATE);
  fluid_settings_setnum(settings, "synth.gain", 1.0);
  fluid_settings_setint(settings, "synth.reverb.active", 0);
  fluid_settings_setint(settings, "synth.chorus.active", 0);
  fluid_synth_t* synth = new_fluid_synth(settings);

  // Same as kitlevels.c: the soundfont isn't in the repo, so look where the
  // build leaves it.
  const char* path = getenv("JAMMER_SOUNDFONT");
  if (!path) path = "FluidR3_GM.sf2";
  if (access(path, R_OK) != 0) {
    printf("no %s; run `make soundfont`\n", path);
    exit(1);
  }
  int sfont = fluid_synth_sfload(synth, path, 1);
  fluid_synth_program_select(synth, 0, sfont, 0, program);
  fluid_synth_cc(synth, 0, CC_07, cc7);
  fluid_synth_cc(synth, 0, CC_11, 127);
  fluid_synth_noteon(synth, 0, note, velocity);

  memset(left, 0, sizeof(left));
  memset(right, 0, sizeof(right));
  float* out[2] = {left, right};
  float* fx[2] = {NULL, NULL};
  fluid_synth_process(synth, RENDER_FRAMES, 0, fx, 2, out);
  for (int i = 0; i < RENDER_FRAMES; i++) {
    mono[i] = 0.5f * (left[i] + right[i]);
  }

  delete_fluid_synth(synth);
  delete_fluid_settings(settings);
  return aweight_loudness(mono, RENDER_FRAMES, RATE);
}

// ---------------------------------------------------------------------------
// The whistle
// ---------------------------------------------------------------------------

// A quiet room, then a whistle: a steady tone at `hz` with a short fade in,
// at a peak amplitude of `amplitude`, over a floor of broadband noise 40dB
// under it.  Both halves matter -- see LEAD_FRAMES for why the room does.
static void make_whistle(float* buf, int n, double hz, double amplitude) {
  double phase = 0;
  double step = 2 * M_PI * hz / RATE;
  int fade = RATE / 20;   // 50ms, so the gate sees an onset rather than a click
  unsigned seed = 1;
  for (int i = 0; i < n; i++) {
    // A fixed generator, so the same run gives the same answer.
    seed = seed * 1103515245u + 12345u;
    double noise = ((double)((seed >> 16) & 0x7fff) / 0x4000 - 1.0);
    double room = 0.01 * amplitude * noise;
    if (i < LEAD_FRAMES) {
      buf[i] = (float)room;
      continue;
    }
    int since = i - LEAD_FRAMES;
    double envelope = since < fade ? (double)since / fade : 1.0;
    buf[i] = (float)(amplitude * envelope * sin(phase) + room);
    phase += step;
  }
}

// Render that through the engine at a given full-blow step and return the
// A-weighted loudness of what comes out.
static double measure_whistle(const float* input, int n, int voice,
                              int level_full_step, int volume_step) {
  struct Engine engine;
  engine_init(&engine, RATE);
  engine_set_voice(&engine, voice);
  engine_set_volume(&engine, volume_step);
  engine_set_level_full(&engine, level_full_step);

  for (int i = 0; i < n; i++) {
    float l, r;
    engine_process_stereo(&engine, input[i], &l, &r);
    mono[i] = 0.5f * (l + r);
  }
  // Measure the note once it is established, not the room before it or the
  // half second the detector spends finding it.
  int skip = LEAD_FRAMES + RATE / 2;
  return aweight_loudness(mono + skip, n - skip, RATE);
}

// The voice each key plays, by name, so this measures what jammer actually
// maps rather than a preset index that may have moved.
static int voice_by_name(const char* name) {
  for (int i = 0; i < synth_preset_count(); i++) {
    if (strcmp(synth_preset_name(i), name) == 0) return i + 1;
  }
  return -1;
}

int main(int argc, char** argv) {
  bool verbose = false, check = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    if (strcmp(argv[i], "--check") == 0) check = true;
  }

  if (!aweight_self_test(RATE, verbose)) {
    printf("the A-weighting is wrong; nothing below can be trusted\n");
    return 1;
  }

  // Program 39 / CC7 66 is what jml_setup leaves the foot bass at.
  double reference = measure_fluidsynth(39, 66, 40, 100);
  printf("foot bass (SynBass 2, low E, as jml_setup leaves it): "
         "%.1f dBA\n\n", reference);

  // What a microphone actually delivers.  0.22 is what every preset in the
  // table carries and what the knob is calibrated around; the levels below
  // span a vocal mic at the lip down to a laptop across the room.
  const double INPUTS[] = {0.30, 0.10, 0.03, 0.01, 0.003};
  const int N_INPUTS = (int)(sizeof(INPUTS) / sizeof(INPUTS[0]));

  int voice = voice_by_name("bass");
  if (voice < 0) {
    printf("no preset named \"bass\"\n");
    return 1;
  }

  printf("the bass voice, volume 9, against that reference:\n\n");
  printf("  %-12s", "input peak");
  for (int step = 0; step <= 9; step++) printf(" %6d", step);
  printf("   <- full-blow step\n");

  double best_gap = -1e9;
  int best_step = -1;
  double best_input = 0;

  for (int i = 0; i < N_INPUTS; i++) {
    static float input[RENDER_FRAMES];
    make_whistle(input, RENDER_FRAMES, 1000.0, INPUTS[i]);
    printf("  %-12.3f", INPUTS[i]);
    for (int step = 0; step <= 9; step++) {
      double level = measure_whistle(input, RENDER_FRAMES, voice, step, 9);
      double gap = level - reference;
      printf(" %+6.1f", gap);
      // The loudest the whistle ever gets, over the whole grid.
      if (gap > best_gap) {
        best_gap = gap;
        best_step = step;
        best_input = INPUTS[i];
      }
    }
    printf("\n");
  }
  printf("\n  numbers are dB relative to the foot bass: 0 is matched,\n"
         "  negative is quieter than it.\n\n");

  printf("loudest the whistle gets at volume 9: %+.1f dB "
         "(input %.3f, full-blow step %d)\n", best_gap, best_input, best_step);

  if (check && best_gap < -6) {
    printf("\nFAIL: the whistle cannot reach the endpoints it plays "
           "beside.\n");
    return 1;
  }
  return 0;
}
