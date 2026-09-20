// Checks that every key in the on-screen layout actually drives jammermidilib,
// and that the lit state follows.  Runs without a synth or a window:
//
//   make test-mac

#include <assert.h>
#include <stdio.h>

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "keylayout.h"
#include "keypad.h"

static int failures = 0;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
      failures++;                                               \
    }                                                           \
  } while (0)

static const Key* key_for_cap(const char* cap) {
  for (int i = 0; i < N_KEYS; i++) {
    if (strcmp(KEYS[i].cap, cap) == 0 && KEYS[i].label) return &KEYS[i];
  }
  return NULL;
}

static void press(const char* cap) {
  const Key* key = key_for_cap(cap);
  assert(key && "no such key in the layout");
  keypad_key(key->note);
}

static bool lit(const char* cap) {
  const Key* key = key_for_cap(cap);
  assert(key);
  return key_is_lit(key);
}

// Every bound key should reach a case handle_keypad() knows about, and no two
// keys should send the same thing.
static void test_layout_is_sane() {
  for (int i = 0; i < N_KEYS; i++) {
    const Key* k = &KEYS[i];
    CHECK(k->cap != NULL, "key %d has no cap", i);
    if (!k->label) {
      CHECK(k->vk == -1 || k->note == 0,
            "unbound key %s still has a note", k->cap);
      continue;
    }
    CHECK(k->note != 0, "bound key %s sends nothing", k->cap);
    CHECK(k->vk >= 0, "bound key %s has no virtual keycode", k->cap);
    for (int j = i + 1; j < N_KEYS; j++) {
      if (!KEYS[j].label) continue;
      CHECK(KEYS[j].note != k->note,
            "keys %s and %s both send %d", k->cap, KEYS[j].cap, k->note);
      CHECK(KEYS[j].vk != k->vk,
            "keys %s and %s share keycode %d", k->cap, KEYS[j].cap, k->vk);
    }
  }

  // Nothing should overlap on screen.
  for (int i = 0; i < N_KEYS; i++) {
    for (int j = i + 1; j < N_KEYS; j++) {
      const Key* a = &KEYS[i];
      const Key* b = &KEYS[j];
      double ah = a->h > 0 ? a->h : 1, bh = b->h > 0 ? b->h : 1;
      bool x_overlap = a->x < b->x + b->w && b->x < a->x + a->w;
      bool y_overlap = a->row < b->row + bh && b->row < a->row + ah;
      CHECK(!(x_overlap && y_overlap),
            "keys %s and %s overlap", a->cap, b->cap);
    }
    CHECK(KEYS[i].x + KEYS[i].w <= N_LAYOUT_COLS + 0.001,
          "key %s runs off the right edge", KEYS[i].cap);
  }
}

static void test_select_and_toggle() {
  full_reset();

  press("1");  // select jawharp
  CHECK(c->selected_endpoint == ENDPOINT_JAWHARP, "1 didn't select jawharp");
  CHECK(lit("1"), "1 should be lit once jawharp is selected");
  CHECK(!lit("2"), "2 should not be lit");

  CHECK(!lit("Q"), "jawharp starts off");
  press("Q");  // toggle jawharp on
  CHECK(c->on[ENDPOINT_JAWHARP], "Q didn't turn the jawharp on");
  CHECK(lit("Q"), "Q should be lit once the jawharp is on");
  press("Q");
  CHECK(!c->on[ENDPOINT_JAWHARP], "Q didn't turn the jawharp back off");
}

static void test_voices() {
  full_reset();

  press("4");  // select flex
  press("C");  // electric piano
  CHECK(c->voices[ENDPOINT_FLEX] == 4, "C didn't pick voice 4");
  CHECK(lit("C"), "C should be lit for the endpoint using voice 4");
  CHECK(!lit("Z"), "Z should not be lit");

  press("Z");  // pan flute
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "Z didn't pick voice 75");
  CHECK(lit("Z") && !lit("C"), "lit voice didn't move to Z");

  // With drums selected the same keys pick drum sounds instead.
  press("`");
  CHECK(c->selected_endpoint == ENDPOINT_DRUM, "` didn't select drums");
  press("D");
  CHECK(c->drum_voice == KIT_SNARE, "D didn't pick the snare");
  CHECK(lit("D"), "D should be lit for the snare");
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "picking a drum changed a voice");
}

static void test_modifier_flags() {
  full_reset();
  press("5");  // select low

  CHECK(!lit("K"), "upbeat starts off for low");
  press("K");
  CHECK(c->upbeat[ENDPOINT_LOW], "K didn't set upbeat");
  CHECK(lit("K"), "K should be lit");
  press("K");
  CHECK(!c->upbeat[ENDPOINT_LOW], "K didn't clear upbeat");

  press(",");
  CHECK(c->chord[ENDPOINT_LOW] && lit(","), "comma didn't set chord");

  // Flags are per endpoint, so switching endpoints switches what's lit.
  press("6");  // select hi
  CHECK(!lit(","), "chord leaked from low to hi");
}

static void test_octave_and_volume() {
  full_reset();
  press("5");

  CHECK(!lit("]") && !lit("\\"), "octave starts at 0");
  press("]");
  CHECK(c->octave_deltas[ENDPOINT_LOW] == 1, "] didn't raise the octave");
  CHECK(lit("]") && !lit("\\"), "only OCT+ should be lit");
  press("\\");
  press("\\");
  CHECK(c->octave_deltas[ENDPOINT_LOW] == -1, "\\ didn't lower the octave");
  CHECK(lit("\\") && !lit("]"), "only OCT- should be lit");

  press("=");
  CHECK(c->volume_deltas[ENDPOINT_LOW] == 5, "= didn't raise the volume");
  CHECK(lit("="), "VOL+ should be lit");
  press("-");
  press("-");
  CHECK(c->volume_deltas[ENDPOINT_LOW] == -5, "- didn't lower the volume");
  CHECK(lit("-") && !lit("="), "only VOL- should be lit");
}

static void test_musical_mode() {
  full_reset();
  CHECK(musical_mode == MODE_MAJOR && lit("↑"), "starts in major");
  press("↓");
  CHECK(musical_mode == MODE_MINOR, "down arrow didn't select minor");
  CHECK(lit("↓") && !lit("↑"), "lit mode didn't move");
}

// F8 and delete arm a three-digit entry, exactly as kbd.py does.
static void test_digit_entry() {
  full_reset();

  int was = root_note;
  press("F8");
  CHECK(armed_note == F8, "F8 didn't arm root-note entry");
  CHECK(lit("F8"), "F8 should be lit while armed");

  press("0"); press("6"); press("2");
  CHECK(armed_note == 0, "entry didn't finish after three digits");
  CHECK(!lit("F8"), "F8 should stop being lit");
  CHECK(root_note == to_root(62), "root note wasn't set to 62 (got %d, was %d)",
        root_note, was);
  CHECK(!jig_time, "digits leaked through to the jig toggle");

  // A non-digit cancels the entry rather than being swallowed.
  press("F8");
  press("Q");
  CHECK(armed_note == 0, "a non-digit should cancel the entry");
  CHECK(c->on[ENDPOINT_JAWHARP], "the cancelling key should still act");

  // Out-of-range values are dropped, as on the Pi.
  root_note = to_root(30);
  press("F8");
  press("2"); press("0"); press("0");
  CHECK(root_note == to_root(30), "200 should have been rejected");

  // Volume entry goes to the selected endpoint's current voice.
  press("4");  // flex
  press("del");
  CHECK(armed_note == DELETE && lit("del"), "del didn't arm volume entry");
  press("0"); press("9"); press("9");
  CHECK(c->manual_volumes[c->voices[ENDPOINT_FLEX]] == 99,
        "volume entry didn't stick");
}

static void test_globals() {
  full_reset();
  press("0");
  CHECK(jig_time && lit("0"), "0 didn't toggle jig time");
  press("F9");
  CHECK(drum_chooses_notes && lit("F9"), "F9 didn't toggle drum-picks-notes");
  press("/");
  CHECK(fade_target == 0 && lit("/"), "/ didn't fade out");
  press("/");
  CHECK(fade_target == MAX_FADE && !lit("/"), "/ didn't fade back in");

  press("esc");
  CHECK(!jig_time && !drum_chooses_notes, "escape didn't reset");
}

int main() {
  jml_setup();

  test_layout_is_sane();
  test_select_and_toggle();
  test_voices();
  test_modifier_flags();
  test_octave_and_volume();
  test_musical_mode();
  test_digit_entry();
  test_globals();

  if (failures) {
    printf("\n%d failure(s)\n", failures);
    return 1;
  }
  printf("all keypad tests passed (%d keys in the layout)\n", N_KEYS);
  return 0;
}
