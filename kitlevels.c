// kitlevels.c -- check that the drum kits are at the same perceived volume,
// and work out the velocity scales that would put them there.
//
// The percussion sets in the soundfont are not mixed against each other at
// all: switching kits without correction is a jump in volume rather than a
// change of sound.  Each kit therefore carries a velocity scale per sound,
// and this is where those numbers come from.
//
// Loudness is A-weighted (see aweight.h), not peak.  That matters a lot here:
// a kick and a hihat at the same peak are nowhere near the same loudness,
// and matching a kit by peak leaves the kick buried.
//
// The scales aren't derived from a formula for how velocity maps to level --
// they're found by searching for the velocity that actually measures the
// same as the reference.  The reference is the Standard set, which is what
// the jammer has always played and what the kits were tuned around.
//
// Build: make kitlevels
// Run:   ./kitlevels [--verbose]
//        ./kitlevels --check    fails if a kit has drifted off level

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

// jammermidilib.h wants a platform underneath it; we only want its kit table,
// so give it somewhere harmless to send MIDI and drive fluidsynth ourselves.
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

#define RATE 44100
#define RENDER_FRAMES (RATE * 2)  // 2s, long enough for the slowest tail

static fluid_synth_t* synth;
static int sfont_id;
static float left[RENDER_FRAMES], right[RENDER_FRAMES];

// Plays one sound the way arpeggiate_drum would and returns its A-weighted
// loudness.  A pitched kick is released after its gate, exactly as the
// jammer releases it; a percussion sample is left to ring.
static double measure(int bank, int program, int note, int velocity,
                      int gate_ms) {
  if (velocity < 1) velocity = 1;
  if (velocity > 127) velocity = 127;

  fluid_synth_system_reset(synth);
  if (fluid_synth_program_select(synth, CHANNEL_DRUM, sfont_id, bank,
                                 program) == FLUID_FAILED) {
    return -INFINITY;
  }
  fluid_synth_noteon(synth, CHANNEL_DRUM, note, velocity);

  if (gate_ms > 0) {
    int held = RATE * gate_ms / 1000;
    fluid_synth_write_float(synth, held, left, 0, 1, right, 0, 1);
    fluid_synth_noteoff(synth, CHANNEL_DRUM, note);
    fluid_synth_write_float(synth, RENDER_FRAMES - held, left + held, 0, 1,
                            right + held, 0, 1);
  } else {
    fluid_synth_write_float(synth, RENDER_FRAMES, left, 0, 1, right, 0, 1);
  }
  return aweight_loudness(left, RENDER_FRAMES, RATE);
}

// The velocity whose loudness comes closest to the target.  Loudness rises
// with velocity, so this is a binary search; searching beats assuming a
// curve, because the soundfont switches samples at velocity layers and the
// relationship isn't smooth.
static int velocity_for(int bank, int program, int note, int gate_ms,
                        double target) {
  int lo = 1, hi = 127;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (measure(bank, program, note, mid, gate_ms) < target) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// arpeggiate_drum's default when the drum isn't following the foot's
// velocity, and so the velocity these scales should be right at.
#define NOMINAL_VELOCITY 90

typedef struct {
  const char* name;
  int kit;
} Named;

#define SND_KICK 0
#define SND_SNARE 1
#define SND_HIHAT 2

// Where a sound should sit relative to the reference, when that isn't level
// with it.  Two kinds of entry, and the difference is worth keeping straight:
//
//  * a sound whose role is quieter or louder by nature, so levelling it
//    would turn it into something else;
//  * a correction by ear.  A-weighting is a good model of loudness, but it's
//    a model -- one number for a whole spectrum -- and the room and the
//    music get the last word.  Recording the correction here rather than
//    editing a scale directly keeps the scales derivable: change a kit or
//    the soundfont, re-run, and the ear correction is still applied.
static const struct {
  int kit;
  int sound;
  double offset_db;
  const char* why;
} LEVEL_OFFSETS[] = {
  {KIT_RIM, SND_SNARE, -3.9, "a side stick is a quiet sound by nature"},
  {KIT_CLAP, SND_SNARE, 8.0, "FluidR3's hand clap is simply loud"},
  {KIT_808_A, SND_HIHAT, -3.0, "by ear"},
  {KIT_808_B, SND_KICK, 3.0, "by ear"},
  {KIT_808_B, SND_HIHAT, -3.0, "by ear"},
  {KIT_ROOM6, SND_KICK, 3.0, "by ear"},
};

static double expected_offset(int kit, int sound) {
  int n = (int)(sizeof(LEVEL_OFFSETS) / sizeof(LEVEL_OFFSETS[0]));
  for (int i = 0; i < n; i++) {
    if (LEVEL_OFFSETS[i].kit == kit && LEVEL_OFFSETS[i].sound == sound) {
      return LEVEL_OFFSETS[i].offset_db;
    }
  }
  return 0.0;
}

// Generous enough not to trip on a soundfont or fluidsynth that rounds
// differently, tight enough that a kit nobody levelled stands out.
#define LEVEL_TOLERANCE_DB 1.5

static const Named KIT_NAMES[] = {
  {"Rim", KIT_RIM}, {"Rim 2", KIT_RIM2}, {"Snare", KIT_SNARE},
  {"Clap", KIT_CLAP}, {"E.Snare", KIT_ESNARE}, {"Ride", KIT_RIDE},
  {"Bongos", KIT_BONGO}, {"Blocks", KIT_BLOCK},
  {"808 A", KIT_808_A}, {"808 B", KIT_808_B},
  {"Room 2", KIT_ROOM2}, {"Room 6", KIT_ROOM6}, {"Synth", KIT_SYNTH},
};

int main(int argc, char** argv) {
  bool verbose = argc > 1 && strcmp(argv[1], "--verbose") == 0;
  bool check = argc > 1 && strcmp(argv[1], "--check") == 0;

  int failures = 0;

  printf("checking the A-weighting against the published curve:\n");
  if (!aweight_self_test(RATE, verbose)) {
    printf("A-weighting is wrong; the numbers below would be meaningless\n");
    return 1;
  }
  printf("  ok\n\n");

  // macapi.h has a proper search, but including it would collide with the
  // stubs above, and this only ever runs from the source directory.
  const char* soundfont = getenv("JAMMER_SOUNDFONT");
  if (!soundfont) soundfont = "FluidR3_GM.sf2";
  if (access(soundfont, R_OK) != 0) {
    // The soundfont isn't in the repo, so in --check this is "can't tell",
    // not "the kits are wrong".
    printf("no %s, skipping the kit level check\n", soundfont);
    return check ? 0 : 1;
  }
  fluid_settings_t* settings = new_fluid_settings();
  fluid_settings_setnum(settings, "synth.gain", 1.0);
  fluid_settings_setint(settings, "synth.reverb.active", 0);
  fluid_settings_setint(settings, "synth.chorus.active", 0);
  fluid_settings_setnum(settings, "synth.sample-rate", RATE);
  synth = new_fluid_synth(settings);
  sfont_id = fluid_synth_sfload(synth, soundfont, 1);
  if (sfont_id == FLUID_FAILED) { printf("couldn't load soundfont\n"); return 1; }

  // What the kits have always sounded like: the Standard set, at the scales
  // the original kits used.
  const DrumKit* reference = &KITS[KIT_SNARE];
  double target_kick = measure(PERCUSSION_BANK, reference->program,
                               reference->kick,
                               NOMINAL_VELOCITY * reference->kick_vel, 0);
  double target_snare = measure(PERCUSSION_BANK, reference->program,
                                reference->snare,
                                NOMINAL_VELOCITY * reference->snare_vel, 0);
  double target_hihat = measure(PERCUSSION_BANK, reference->program,
                                reference->hihat,
                                NOMINAL_VELOCITY * reference->hihat_vel, 0);
  if (!check) {
    printf("reference (Standard set at velocity %d):\n", NOMINAL_VELOCITY);
    printf("  kick %6.1f dBA   snare %6.1f dBA   hihat %6.1f dBA\n\n",
           target_kick, target_snare, target_hihat);
    printf("%-8s %-22s %-24s %s\n", "kit", "kick", "snare", "hihat");
    printf("%-8s %-22s %-24s %s\n", "", "now -> want  scale",
           "now -> want  scale", "now -> want  scale");
  }

  for (int i = 0; i < (int)(sizeof(KIT_NAMES) / sizeof(KIT_NAMES[0])); i++) {
    const DrumKit* k = &KITS[KIT_NAMES[i].kit];
    bool pitched = k->kick_program != NO_PITCHED_KICK;
    int kick_bank = pitched ? 0 : PERCUSSION_BANK;
    int kick_prog = pitched ? k->kick_program : k->program;
    int gate = pitched ? k->kick_gate_ms : 0;

    double now_kick = measure(kick_bank, kick_prog, k->kick,
                              NOMINAL_VELOCITY * k->kick_vel, gate);
    double now_snare = measure(PERCUSSION_BANK, k->program, k->snare,
                               NOMINAL_VELOCITY * k->snare_vel, 0);
    double now_hihat = measure(PERCUSSION_BANK, k->program, k->hihat,
                               NOMINAL_VELOCITY * k->hihat_vel, 0);

    // Aim at the reference plus whatever offset this sound is meant to have.
    double want_kick =
      velocity_for(kick_bank, kick_prog, k->kick, gate,
                   target_kick + expected_offset(KIT_NAMES[i].kit, SND_KICK))
      / (double)NOMINAL_VELOCITY;
    double want_snare =
      velocity_for(PERCUSSION_BANK, k->program, k->snare, 0,
                   target_snare + expected_offset(KIT_NAMES[i].kit, SND_SNARE))
      / (double)NOMINAL_VELOCITY;
    double want_hihat =
      velocity_for(PERCUSSION_BANK, k->program, k->hihat, 0,
                   target_hihat + expected_offset(KIT_NAMES[i].kit, SND_HIHAT))
      / (double)NOMINAL_VELOCITY;

    double off[3] = {now_kick - target_kick, now_snare - target_snare,
                     now_hihat - target_hihat};

    if (check) {
      static const char* SOUND[3] = {"kick", "snare", "hihat"};
      for (int sound = 0; sound < 3; sound++) {
        double want = expected_offset(KIT_NAMES[i].kit, sound);
        if (fabs(off[sound] - want) > LEVEL_TOLERANCE_DB) {
          printf("FAIL: %s %s is %+.1f dB from the reference, want %+.1f\n",
                 KIT_NAMES[i].name, SOUND[sound], off[sound], want);
          failures++;
        }
      }
      continue;
    }

    printf("%-8s %5.1f %+6.1f %5.2f   %5.1f %+6.1f %5.2f     %5.1f %+6.1f %5.2f\n",
           KIT_NAMES[i].name,
           now_kick, off[0], want_kick,
           now_snare, off[1], want_snare,
           now_hihat, off[2], want_hihat);
  }

  if (check) {
    if (failures) {
      printf("\n%d kit sound(s) off level; re-run ./kitlevels for the "
             "scales to use\n", failures);
      return 1;
    }
    printf("kit level check passed: %d kits within %.1f dBA\n",
           (int)(sizeof(KIT_NAMES) / sizeof(KIT_NAMES[0])),
           LEVEL_TOLERANCE_DB);
    return 0;
  }

  printf("\n\"now\" is dBA at the kit's current scale, then how far that is\n"
         "from the reference, then the scale that would match it.\n");
  return 0;
}
