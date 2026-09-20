// Checks that every key in the on-screen layout actually drives jammermidilib,
// and that the lit state follows.  Runs without a synth or a window:
//
//   make test-mac

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

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

// Shift + an endpoint's on/off key, which is how you pick which endpoint the
// modifier keys act on.
static void select_ep(const char* cap) {
  const Key* key = key_for_cap(cap);
  assert(key && "no such key in the layout");
  assert(key->select_note && "that key doesn't select an endpoint");
  keypad_key(key->select_note);
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
    CHECK(!k->select_note || k->lit == LIT_EP_ON,
          "key %s shift-selects but isn't an endpoint toggle", k->cap);
    for (int j = i + 1; j < N_KEYS; j++) {
      if (!KEYS[j].label) continue;
      CHECK(KEYS[j].note != k->note,
            "keys %s and %s both send %d", k->cap, KEYS[j].cap, k->note);
      CHECK(KEYS[j].vk != k->vk,
            "keys %s and %s share keycode %d", k->cap, KEYS[j].cap, k->vk);
      CHECK(!k->select_note || KEYS[j].select_note != k->select_note,
            "keys %s and %s both select %d", k->cap, KEYS[j].cap,
            k->select_note);
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

  select_ep("Q");  // shift-Q: select jawharp
  CHECK(c->selected_endpoint == ENDPOINT_JAWHARP,
        "shift-Q didn't select jawharp");
  CHECK(!c->on[ENDPOINT_JAWHARP], "selecting shouldn't switch anything on");
  CHECK(key_is_selected_endpoint(key_for_cap("Q")), "Q should show as selected");
  CHECK(!key_is_selected_endpoint(key_for_cap("W")), "W should not");

  CHECK(!lit("Q"), "jawharp starts off");
  press("Q");  // toggle jawharp on
  CHECK(c->on[ENDPOINT_JAWHARP], "Q didn't turn the jawharp on");
  CHECK(lit("Q"), "Q should be lit once the jawharp is on");
  CHECK(key_is_selected_endpoint(key_for_cap("Q")),
        "toggling shouldn't move the selection");
  press("Q");
  CHECK(!c->on[ENDPOINT_JAWHARP], "Q didn't turn the jawharp back off");

  // Every endpoint is reachable, and selecting never toggles.
  for (int i = 0; i < N_KEYS; i++) {
    if (!KEYS[i].select_note) continue;
    bool was_on = c->on[KEYS[i].arg];
    keypad_key(KEYS[i].select_note);
    CHECK(c->selected_endpoint == KEYS[i].arg,
          "shift-%s didn't select endpoint %d", KEYS[i].cap, KEYS[i].arg);
    CHECK(c->on[KEYS[i].arg] == was_on,
          "shift-%s changed whether its endpoint was on", KEYS[i].cap);
  }
}

static void test_voices() {
  full_reset();

  select_ep("R");  // select flex
  press("C");  // electric piano
  CHECK(c->voices[ENDPOINT_FLEX] == 4, "C didn't pick voice 4");
  CHECK(lit("C"), "C should be lit for the endpoint using voice 4");
  CHECK(!lit("Z"), "Z should not be lit");

  press("Z");  // pan flute
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "Z didn't pick voice 75");
  CHECK(lit("Z") && !lit("C"), "lit voice didn't move to Z");

  // With drums selected the same keys pick drum sounds instead.
  select_ep("tab");
  CHECK(c->selected_endpoint == ENDPOINT_DRUM, "shift-tab didn't select drums");
  press("A");
  CHECK(c->drum_voice == KIT_RIM, "A didn't pick the rim kit");
  CHECK(lit("A"), "A should be lit for the rim kit");
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "picking a drum changed a voice");

  press("Z");
  CHECK(c->drum_voice == KIT_808_A, "Z didn't pick the first 808 kit");
  CHECK(lit("Z"), "Z should be lit for the 808 kit");
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "picking a kit changed a voice");

  // The voice keys with no kit on them do nothing at all with the drum
  // selected -- in particular they must not fall through and set a melodic
  // voice on a channel that's playing a percussion set.
  const char* blank[] = {"S", "D", "F", "G", "H", "B", "N", "M"};
  for (int i = 0; i < (int)(sizeof(blank) / sizeof(blank[0])); i++) {
    int was_kit = c->drum_voice;
    int was_voice = c->voices[ENDPOINT_DRUM];
    press(blank[i]);
    CHECK(c->drum_voice == was_kit, "%s changed the kit; it should be blank",
          blank[i]);
    CHECK(c->voices[ENDPOINT_DRUM] == was_voice,
          "%s set a melodic voice on the drum channel", blank[i]);
    CHECK(!lit(blank[i]), "%s should not light up with the drum selected",
          blank[i]);
  }

  // Each kit names sounds the drum channel can actually play.
  for (int kit = 0; kit < N_KITS; kit++) {
    CHECK(KITS[kit].kick > 0 && KITS[kit].snare > 0 && KITS[kit].hihat > 0,
          "a kit is missing a sound");
  }

  // No kit on the keyboard uses a pitched kick at the moment, but the
  // support is still there and still has to work: selecting such a kit sets
  // its own channel up, the gate releases the note, and silencing the drum
  // beats the gate to it.  A held melodic note doesn't decay on its own.
  select_drum_kit(KIT_SYNTH);
  CHECK(KITS[c->drum_voice].kick_program != NO_PITCHED_KICK,
        "KIT_SYNTH should have a pitched kick");
  CHECK(KITS[c->drum_voice].kick_gate_ms > 0,
        "a pitched kick needs a gate or it drones");

  pitched_kick_note = KITS[c->drum_voice].kick;
  pitched_kick_off_at = now() + NS_PER_SEC;
  maybe_end_pitched_kick();
  CHECK(pitched_kick_note != -1, "the gate ended the kick early");
  pitched_kick_off_at = now();
  maybe_end_pitched_kick();
  CHECK(pitched_kick_note == -1, "the gate never ended the kick");

  pitched_kick_note = KITS[c->drum_voice].kick;
  endpoint_notes_off(ENDPOINT_DRUM);
  CHECK(pitched_kick_note == -1,
        "silencing the drum left the pitched kick ringing");

  press("A");
  CHECK(KITS[c->drum_voice].kick_program == NO_PITCHED_KICK,
        "the Rim kit shouldn't have a pitched kick");
  CHECK(pitched_kick_note == -1,
        "switching off a pitched-kick kit left a note sounding");
}

static void test_modifier_flags() {
  full_reset();
  select_ep("T");  // select low

  CHECK(!lit("K"), "upbeat starts off for low");
  press("K");
  CHECK(c->upbeat[ENDPOINT_LOW], "K didn't set upbeat");
  CHECK(lit("K"), "K should be lit");
  press("K");
  CHECK(!c->upbeat[ENDPOINT_LOW], "K didn't clear upbeat");

  press(",");
  CHECK(c->chord[ENDPOINT_LOW] && lit(","), "comma didn't set chord");

  // Flags are per endpoint, so switching endpoints switches what's lit.
  select_ep("Y");  // select hi
  CHECK(!lit(","), "chord leaked from low to hi");
}

static void test_octave_and_volume() {
  full_reset();
  select_ep("T");

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

// Turning the drum's note-picking on should leave the foot bass set up to
// play what it picks, however things happened to be beforehand.
static void test_drum_picks_notes_defaults() {
  full_reset();

  press("F9");
  CHECK(drum_chooses_notes, "F9 didn't turn drum-picks-notes on");
  CHECK(c->on[ENDPOINT_FOOTBASS], "F9 didn't turn the foot bass on");
  CHECK(c->selected_endpoint == ENDPOINT_FOOTBASS,
        "F9 didn't select the foot bass");
  CHECK(c->voices[ENDPOINT_FOOTBASS] == 32, "F9 didn't pick acoustic bass");
  CHECK(!c->upbeat[ENDPOINT_FOOTBASS], "F9 didn't clear upbeat");
  CHECK(c->vel[ENDPOINT_FOOTBASS], "F9 didn't set vel");
  CHECK(c->octave_deltas[ENDPOINT_FOOTBASS] == 1, "F9 didn't set oct+");
  CHECK(c->volume_deltas[ENDPOINT_FOOTBASS] == 35, "F9 didn't set vol+35");

  // Off and on again lands in the same place rather than toggling back.
  press("F9");
  CHECK(!drum_chooses_notes, "F9 didn't turn drum-picks-notes off");
  CHECK(c->on[ENDPOINT_FOOTBASS], "turning it off shouldn't stop the bass");
  press("F9");
  CHECK(c->on[ENDPOINT_FOOTBASS] && c->voices[ENDPOINT_FOOTBASS] == 32 &&
        !c->upbeat[ENDPOINT_FOOTBASS] && c->vel[ENDPOINT_FOOTBASS] &&
        c->octave_deltas[ENDPOINT_FOOTBASS] == 1 &&
        c->volume_deltas[ENDPOINT_FOOTBASS] == 35,
        "a second F9 didn't leave the same setup");

  // And it overrides whatever the foot bass was doing before.
  full_reset();
  select_ep("W");
  press("Z");  // pan flute
  press("\\");  // octave down
  press("-");   // and quieter
  CHECK(c->upbeat[ENDPOINT_FOOTBASS], "upbeat is on by default for foot bass");
  CHECK(c->octave_deltas[ENDPOINT_FOOTBASS] == -1, "octave should be down");
  CHECK(c->volume_deltas[ENDPOINT_FOOTBASS] == -5, "volume should be down");
  press("F9");
  CHECK(c->voices[ENDPOINT_FOOTBASS] == 32 && !c->upbeat[ENDPOINT_FOOTBASS] &&
        c->vel[ENDPOINT_FOOTBASS] &&
        c->octave_deltas[ENDPOINT_FOOTBASS] == 1 &&
        c->volume_deltas[ENDPOINT_FOOTBASS] == 35,
        "F9 didn't override the earlier setup");
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

// The kit table asks for bank 128, but it's the platform's choose_voice that
// has to pass it through, and the rest of the tests stub or skip that.  When
// it clamped the bank to 127 every kit silently became a grand piano on the
// drum channel, and nothing here noticed.  So: a real synth, and read the
// program back off the channel.
static void test_percussion_bank_reaches_the_synth() {
  if (access("FluidR3_GM.sf2", R_OK) != 0) {
    printf("no soundfont here, skipping the percussion bank check\n");
    return;
  }

  // A synth with no audio driver: we only want to ask it what it's set to.
  fl_settings = new_fluid_settings();
  fluid_settings_setint(fl_settings, "synth.midi-channels", 16);
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");
  fl_synth = new_fluid_synth(fl_settings);
  fl_sfont_id = fluid_synth_sfload(fl_synth, "FluidR3_GM.sf2", 1);
  CHECK(fl_sfont_id != FLUID_FAILED, "couldn't load the soundfont");
  if (fl_sfont_id == FLUID_FAILED) return;

  int sfont, bank, program;

  select_drum_kit(KIT_808_A);
  fluid_synth_get_program(fl_synth, CHANNEL_DRUM, &sfont, &bank, &program);
  CHECK(bank == PERCUSSION_BANK,
        "drum channel is on bank %d, want the percussion bank %d",
        bank, PERCUSSION_BANK);
  CHECK(program == KITS[KIT_808_A].program,
        "drum channel is on set %d, want %d", program,
        KITS[KIT_808_A].program);

  select_drum_kit(KIT_SYNTH);
  fluid_synth_get_program(fl_synth, CHANNEL_PITCHED_KICK, &sfont, &bank,
                          &program);
  CHECK(bank == 0, "a pitched kick is a melodic program, so bank 0, not %d",
        bank);
  CHECK(program == KITS[KIT_SYNTH].kick_program,
        "pitched-kick channel is on program %d, want %d", program,
        KITS[KIT_SYNTH].kick_program);

  select_drum_kit(KIT_RIM);
  fluid_synth_get_program(fl_synth, CHANNEL_DRUM, &sfont, &bank, &program);
  CHECK(bank == PERCUSSION_BANK && program == KITS[KIT_RIM].program,
        "going back to a Standard kit left the drum channel on %d-%d",
        bank, program);

  delete_fluid_synth(fl_synth);
  delete_fluid_settings(fl_settings);
  fl_synth = NULL;
  fl_settings = NULL;
}

int main() {
  jml_setup();

  test_layout_is_sane();
  test_select_and_toggle();
  test_voices();
  test_modifier_flags();
  test_octave_and_volume();
  test_musical_mode();
  test_drum_picks_notes_defaults();
  test_globals();
  test_percussion_bank_reaches_the_synth();

  if (failures) {
    printf("\n%d failure(s)\n", failures);
    return 1;
  }
  printf("all keypad tests passed (%d keys in the layout)\n", N_KEYS);
  return 0;
}
