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

static int cc11[16], cc7[16], prog[16], bank[16];
static bool saw_cc11[16];
static int note_ons[16];  // how many note-ons each channel has been sent

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
  if (action == MIDI_ON) note_ons[endpoint]++;
}
void choose_voice(int channel, int bank_num, int voice) {
  prog[channel] = voice;
  bank[channel] = bank_num;
}

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
    if (i == ENDPOINT_FLEX) continue;  // its expression is its breath
    CHECK(cc11[i] == MAX_FADE, "endpoint %d expression is %d, want %d",
          i, cc11[i], MAX_FADE);
    if (i != ENDPOINT_DRUM) {
      CHECK(cc7[i] > 0, "endpoint %d volume is 0", i);
    }
  }

  // The drum channel used to never be sent a program change at all, so every
  // kit came out of whatever set the synth defaulted to.
  CHECK(bank[CHANNEL_DRUM] == PERCUSSION_BANK,
        "drum channel is on bank %d, want the percussion bank",
        bank[CHANNEL_DRUM]);
  CHECK(prog[CHANNEL_DRUM] == KITS[c->drum_voice].program,
        "drum channel is on set %d, but the kit wants %d",
        prog[CHANNEL_DRUM], KITS[c->drum_voice].program);

  // A kit with a pitched kick has to set its own channel up, or the kick is
  // silent -- it's outside the endpoint loops that do this for everything
  // else.
  int pitched = -1;
  for (int kit = 0; kit < N_KITS; kit++) {
    if (KITS[kit].kick_program != NO_PITCHED_KICK) pitched = kit;
  }
  if (pitched >= 0) {
    select_drum_kit(pitched);
    CHECK(prog[CHANNEL_PITCHED_KICK] == KITS[pitched].kick_program,
          "pitched-kick channel is on program %d, want %d",
          prog[CHANNEL_PITCHED_KICK], KITS[pitched].kick_program);
    CHECK(bank[CHANNEL_PITCHED_KICK] == 0,
          "a pitched kick is a melodic program, so bank 0, not %d",
          bank[CHANNEL_PITCHED_KICK]);
    CHECK(cc7[CHANNEL_PITCHED_KICK] > 0, "pitched-kick channel volume is 0");
    CHECK(cc11[CHANNEL_PITCHED_KICK] == MAX_FADE,
          "pitched-kick channel expression is %d, want %d",
          cc11[CHANNEL_PITCHED_KICK], MAX_FADE);
    select_drum_kit(KIT_RIM);
  }

  // A fade-out followed by a reset used to leave everything silent too.
  fade_target = 0;
  for (int i = 0; i < MAX_FADE + 10; i++) progress_fades();
  CHECK(cc11[ENDPOINT_LOW] == 0, "fade-out should reach expression 0");
  full_reset();
  for (int i = 0; i < N_ENDPOINTS; i++) {
    if (i == ENDPOINT_FLEX) continue;  // its expression is its breath
    CHECK(cc11[i] == MAX_FADE,
          "endpoint %d still silent after reset following a fade-out", i);
  }

  // Flex's expression is its breath, and the fade has to reach it anyway:
  // breathing into it mid-fade used to put it straight back to full.
  handle_cc(CC_BREATH, 100);
  int breathing = cc11[ENDPOINT_FLEX];
  CHECK(breathing > 0, "breath should open up flex");
  fade_target = 0;
  for (int i = 0; i < MAX_FADE + 10; i++) {
    progress_fades();
    forward_air();
  }
  handle_cc(CC_BREATH, 100);
  CHECK(cc11[ENDPOINT_FLEX] == 0,
        "flex is at %d after a fade out, with breath; want 0",
        cc11[ENDPOINT_FLEX]);
  fade_target = MAX_FADE;
  for (int i = 0; i < MAX_FADE + 10; i++) {
    progress_fades();
    forward_air();
  }
  handle_cc(CC_BREATH, 100);
  CHECK(cc11[ENDPOINT_FLEX] == breathing,
        "flex should be back to %d after fading in, is %d", breathing,
        cc11[ENDPOINT_FLEX]);
  handle_cc(CC_BREATH, 0);
  CHECK(cc11[CHANNEL_PITCHED_KICK] == MAX_FADE,
        "pitched-kick channel still silent after reset following a fade-out");

  // Changing a sounding drone's voice has to strike its note again: a program
  // change doesn't reach notes already sounding, and the old note has just
  // been silenced.  It used to be left off, because the drone still thought
  // the note was sounding.
  c->selected_endpoint = ENDPOINT_DRONE_BASS;
  toggle_endpoint(ENDPOINT_DRONE_BASS);
  CHECK(c->on[ENDPOINT_DRONE_BASS], "the drone should be on");
  int before = note_ons[ENDPOINT_DRONE_BASS];
  select_voice(c, 89);  // warm pad
  CHECK(note_ons[ENDPOINT_DRONE_BASS] > before,
        "changing the drone's voice left it silent");
  toggle_endpoint(ENDPOINT_DRONE_BASS);

  // A chord picked by voice, or a key change, reaches the drones at once --
  // bass and chord both -- with no beat needed to carry it.
  handle_keypad(MIDI_ON, SPEECH_PICKS, 64);  // F3: speech choosing
  CHECK(speech_chooses_notes, "F3 should switch on speech choosing");
  for (int e = ENDPOINT_DRONE_BASS; e <= ENDPOINT_DRONE_CHORD; e++) {
    c->selected_endpoint = e;
    if (!c->on[e]) toggle_endpoint(e);
  }
  int bass_before = note_ons[ENDPOINT_DRONE_BASS];
  int chord_before = note_ons[ENDPOINT_DRONE_CHORD];
  nashville_picks_chord(4);
  CHECK(note_ons[ENDPOINT_DRONE_BASS] > bass_before,
        "the drone bass didn't move to the IV");
  CHECK(note_ons[ENDPOINT_DRONE_CHORD] > chord_before,
        "the drone chord didn't move to the IV");
  bass_before = note_ons[ENDPOINT_DRONE_BASS];
  chord_before = note_ons[ENDPOINT_DRONE_CHORD];
  nashville_picks_chord(6);
  CHECK(note_ons[ENDPOINT_DRONE_BASS] > bass_before &&
        note_ons[ENDPOINT_DRONE_CHORD] > chord_before,
        "the drones didn't move to the vi");
  bass_before = note_ons[ENDPOINT_DRONE_BASS];
  int vi = active_note();
  change_key(root_note + 2);
  CHECK(note_ons[ENDPOINT_DRONE_BASS] > bass_before,
        "a key change didn't reach the drone bass");
  CHECK(active_note() == to_root(vi + 2) && active_chord() == to_root(vi + 2),
        "a key change should move the vi with it, not leave it behind");

  if (failures) { printf("\n%d failure(s)\n", failures); return 1; }
  printf("\nstartup tests passed: every endpoint audible\n");
  return 0;
}
