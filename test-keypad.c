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
#include "whistle.h"
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

static const Key* key_for_cap_note(int note) {
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].label && KEYS[i].note == note) return &KEYS[i];
  }
  return NULL;
}

static void press(const char* cap) {
  const Key* key = key_for_cap(cap);
  assert(key && "no such key in the layout");
  keypad_key(key->note);
}

// The whole dispatch the window does, whistle included, so the tests exercise
// the routing rather than just the half below it.  Mirrors
// -[JammerView strikeKeyAtIndex:selecting:].
static void strike(const char* cap, bool selecting) {
  const Key* key = key_for_cap(cap);
  assert(key && "no such key in the layout");
  int note = (selecting && key->select_note) ? key->select_note : key->note;
  if (!whistle_key(key, selecting)) {
    if (note == ESCAPE) whistle_reset();
    keypad_key(note);
  }
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
    // The whistle isn't a fluidsynth channel and doesn't go through
    // handle_keypad, so its keys deliberately send nothing.
    bool whistle_key_entry = (k->lit == LIT_WHISTLE_ON);
    CHECK(whistle_key_entry || k->note != 0,
          "bound key %s sends nothing", k->cap);
    CHECK(!whistle_key_entry || k->note == 0,
          "whistle key %s shouldn't reach handle_keypad", k->cap);
    CHECK(k->vk >= 0, "bound key %s has no virtual keycode", k->cap);
    CHECK(!k->select_note || k->lit == LIT_EP_ON,
          "key %s shift-selects but isn't an endpoint toggle", k->cap);
    for (int j = i + 1; j < N_KEYS; j++) {
      if (!KEYS[j].label) continue;
      CHECK(k->note == 0 || KEYS[j].note != k->note,
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

// The two foot basses on 2 and 3 exist to be the original plus a fixed set of
// flags, so what's worth pinning down is exactly which flags -- a derivation
// nobody can check by looking at it is one that quietly drifts.
static void test_extra_footbasses() {
  full_reset();

  struct { const char* cap; int endpoint; const char* name; }
  keys[] = {
    {"2", ENDPOINT_FOOTBASS_2, "foot bass 2"},
    {"3", ENDPOINT_FOOTBASS_3, "foot bass 3"},
  };

  for (int i = 0; i < 2; i++) {
    const Key* key = key_for_cap(keys[i].cap);
    CHECK(key != NULL && key->lit == LIT_EP_ON &&
          key->arg == keys[i].endpoint,
          "%s isn't the %s key", keys[i].cap, keys[i].name);

    CHECK(!c->on[keys[i].endpoint], "%s starts off", keys[i].name);
    press(keys[i].cap);
    CHECK(c->on[keys[i].endpoint], "%s didn't switch %s on",
          keys[i].cap, keys[i].name);
    CHECK(lit(keys[i].cap), "%s should be lit once %s is on",
          keys[i].cap, keys[i].name);
    press(keys[i].cap);
    CHECK(!c->on[keys[i].endpoint], "%s didn't switch %s back off",
          keys[i].cap, keys[i].name);

    select_ep(keys[i].cap);
    CHECK(c->selected_endpoint == keys[i].endpoint,
          "shift-%s didn't select %s", keys[i].cap, keys[i].name);
  }

  // 2 is the foot bass with a shorter note and the doubling: FB + SS + II.
  CHECK(c->downbeat[ENDPOINT_FOOTBASS_2], "fb2 keeps the downbeat");
  CHECK(c->upbeat[ENDPOINT_FOOTBASS_2], "fb2 keeps the upbeat");
  CHECK(c->upbeat_high[ENDPOINT_FOOTBASS_2], "fb2 keeps the high upbeat");
  CHECK(c->shorter[ENDPOINT_FOOTBASS_2], "fb2 is SS");
  CHECK(c->doubled[ENDPOINT_FOOTBASS_2], "fb2 is II");
  CHECK(!c->shortish[ENDPOINT_FOOTBASS_2], "fb2 is not S");
  CHECK(c->voices[ENDPOINT_FOOTBASS_2] == 39,
        "fb2 plays the foot bass's own voice");

  // 3 adds the shortish, drops the downbeat, and takes the S key's voice:
  // FB + S + s + SS + DB off + II.
  CHECK(!c->downbeat[ENDPOINT_FOOTBASS_3], "fb3 has the downbeat off");
  CHECK(c->upbeat[ENDPOINT_FOOTBASS_3], "fb3 keeps the upbeat");
  CHECK(c->upbeat_high[ENDPOINT_FOOTBASS_3], "fb3 keeps the high upbeat");
  CHECK(c->shortish[ENDPOINT_FOOTBASS_3], "fb3 is S");
  CHECK(c->shorter[ENDPOINT_FOOTBASS_3], "fb3 is SS");
  CHECK(c->doubled[ENDPOINT_FOOTBASS_3], "fb3 is II");
  CHECK(c->voices[ENDPOINT_FOOTBASS_3] == 38,
        "fb3 plays SynBass 1, the S key's voice");

  // And the one they were derived from is untouched by any of it.
  CHECK(c->downbeat[ENDPOINT_FOOTBASS] && c->upbeat[ENDPOINT_FOOTBASS] &&
        c->upbeat_high[ENDPOINT_FOOTBASS] &&
        !c->shortish[ENDPOINT_FOOTBASS] && !c->shorter[ENDPOINT_FOOTBASS] &&
        !c->doubled[ENDPOINT_FOOTBASS] &&
        c->voices[ENDPOINT_FOOTBASS] == 39,
        "the original foot bass should be exactly as it was");

  // They are foot basses everywhere it matters, not just in their defaults.
  CHECK(is_footbass(ENDPOINT_FOOTBASS_2) && is_footbass(ENDPOINT_FOOTBASS_3),
        "both should count as foot basses");
  CHECK(!is_footbass(ENDPOINT_ARP) && !is_footbass(ENDPOINT_DRUM),
        "and nothing else should");

  // The pitched kick moved out of the endpoints' way to make room; if it
  // ever moves back on top of one, a kit's kick plays down an endpoint.
  CHECK(CHANNEL_PITCHED_KICK >= N_ENDPOINTS,
        "the pitched kick is sitting on endpoint %d", CHANNEL_PITCHED_KICK);
}

// The whistle is an instrument on this keyboard but not an endpoint, so the
// thing to check is that the selection actually redirects the shared keys --
// and, just as much, that it puts them back.
static void test_whistle() {
  full_reset();
  whistle_reset();

  const Key* one = key_for_cap("1");
  CHECK(one != NULL && one->lit == LIT_WHISTLE_ON,
        "the 1 key should be the whistle");

  // Every voice key resolves to a preset that exists.
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    CHECK(whistle_engine_voice[i] > 0,
          "no preset for whistle voice %s", WHISTLE_VOICES[i].preset);
    const Key* key = key_for_cap_note(WHISTLE_VOICES[i].note);
    CHECK(key != NULL, "no key sends %d for %s", WHISTLE_VOICES[i].note,
          WHISTLE_VOICES[i].preset);
    CHECK(key == NULL || key->group == GROUP_VOICE,
          "%s isn't a voice key", key ? key->cap : "?");
  }

  // Toggling, and selecting without toggling.
  strike("1", false);
  CHECK(whistle_on, "1 didn't switch the whistle on");
  CHECK(lit("1"), "1 should be lit once the whistle is on");
  CHECK(whistle_selected, "toggling should select, as it does an endpoint");
  select_ep("R");
  strike("1", true);
  CHECK(whistle_selected, "shift-1 didn't select the whistle");
  CHECK(whistle_on, "selecting shouldn't switch it off");
  CHECK(key_is_selected_endpoint(one), "1 should show as selected");

  // While it is selected the voice keys are its, and no endpoint's voice
  // changes underneath.
  select_ep("R");                      // flex, and it keeps the selection
  strike("1", true);                   // back to the whistle
  int flex_voice = c->voices[ENDPOINT_FLEX];
  strike("D", false);                  // reese
  CHECK(whistle_voice == 2, "D didn't pick the third whistle voice");
  CHECK(c->voices[ENDPOINT_FLEX] == flex_voice,
        "a whistle voice key changed an endpoint's voice");
  CHECK(lit("D"), "D should be lit for the whistle voice it picked");
  CHECK(!lit("A"), "A shouldn't be lit for a voice that isn't picked");
  CHECK(!key_is_selected_endpoint(key_for_cap("R")),
        "no endpoint is selected while the whistle is");

  // The three voice keys the whistle doesn't use do nothing, and say so.
  strike("B", false);
  CHECK(whistle_voice == 2, "B should have done nothing");
  CHECK(c->voices[ENDPOINT_FLEX] == flex_voice, "B reached the endpoint");
  CHECK(whistle_key_is_dead(key_for_cap("B")), "B should draw as dead");
  CHECK(!whistle_key_is_dead(key_for_cap("D")), "D shouldn't");

  // Octave and volume act on the whistle, within the engine's limits.
  int flex_octave = c->octave_deltas[ENDPOINT_FLEX];
  strike("]", false);
  strike("]", false);
  CHECK(whistle_octave == 2, "] didn't move the whistle octave");
  CHECK(c->octave_deltas[ENDPOINT_FLEX] == flex_octave,
        "] moved an endpoint's octave while the whistle was selected");
  CHECK(lit("]"), "] should be lit once the octave has moved");
  for (int i = 0; i < 10; i++) strike("]", false);
  CHECK(whistle_octave == SYNTH_OCTAVE_SHIFT,
        "the octave should stop at the engine's limit");
  for (int i = 0; i < 20; i++) strike("\\", false);
  CHECK(whistle_octave == -SYNTH_OCTAVE_SHIFT,
        "and at the other one");

  // The trim starts at the top -- see WHISTLE_VOLUME_DEFAULT -- so it moves
  // down first and back up after.
  CHECK(whistle_volume == WHISTLE_VOLUME_DEFAULT,
        "the whistle volume should start at its default");
  strike("-", false);
  CHECK(whistle_volume == WHISTLE_VOLUME_DEFAULT - 1,
        "- didn't move the whistle volume");
  CHECK(lit("-"), "- should be lit once the volume has moved");
  strike("=", false);
  CHECK(whistle_volume == WHISTLE_VOLUME_DEFAULT, "= didn't move it back");
  CHECK(!lit("-"), "- shouldn't be lit once it's back at the default");
  for (int i = 0; i < 20; i++) strike("=", false);
  CHECK(whistle_volume == 9, "the volume should stop at 9");
  for (int i = 0; i < 20; i++) strike("-", false);
  CHECK(whistle_volume == 0, "and at 0");

  // Per-endpoint flags are swallowed rather than applied to whoever was
  // selected last.
  bool flex_chord = c->chord[ENDPOINT_FLEX];
  strike(",", false);
  CHECK(c->chord[ENDPOINT_FLEX] == flex_chord,
        "a modifier key reached an endpoint while the whistle was selected");
  CHECK(whistle_key_is_dead(key_for_cap(",")), ", should draw as dead");

  // Toggles still toggle, and toggling or shift-selecting an endpoint hands
  // the keys back to it.
  bool low_was_on = c->on[ENDPOINT_LOW];
  strike("T", false);
  CHECK(c->on[ENDPOINT_LOW] != low_was_on,
        "an endpoint toggle stopped working while the whistle was selected");
  CHECK(!whistle_selected, "toggling an endpoint didn't leave the whistle");
  CHECK(c->selected_endpoint == ENDPOINT_LOW, "...or didn't select it");
  strike("T", false);                  // put it back

  strike("1", true);
  CHECK(whistle_selected, "shift-1 didn't reselect the whistle");
  strike("T", true);
  CHECK(!whistle_selected, "shift on an endpoint didn't leave the whistle");
  CHECK(c->selected_endpoint == ENDPOINT_LOW, "...or didn't select it");
  CHECK(!whistle_key_is_dead(key_for_cap(",")),
        ", should be live again once an endpoint is selected");

  // And now the same keys mean what they always meant.
  int whistle_was = whistle_voice;
  press("C");
  CHECK(c->voices[ENDPOINT_LOW] == 4, "C didn't pick voice 4 for the endpoint");
  CHECK(whistle_voice == whistle_was, "C moved the whistle voice too");

  // esc resets the instrument but not the setup knobs.
  strike("1", true);
  strike("G", false);
  whistle_gate = 7;
  whistle_level_full = 2;
  strike("esc", false);
  CHECK(!whistle_on && !whistle_selected, "esc didn't reset the whistle");
  CHECK(whistle_voice == 0 && whistle_octave == 0, "...or its voice/octave");
  CHECK(whistle_volume == WHISTLE_VOLUME_DEFAULT, "...or its volume");
  CHECK(whistle_gate == 7 && whistle_level_full == 2,
        "esc shouldn't touch the setup knobs");
  whistle_gate = 5;
  whistle_level_full = 5;
}

int main() {
  jml_setup();
  // The whistle's key handling needs its state and its voice table, but no
  // audio: whistle_resolve_voices only reads the presets table, which is
  // compiled in.
  whistle_init_state();
  whistle_resolve_voices();

  test_layout_is_sane();
  test_select_and_toggle();
  test_voices();
  test_modifier_flags();
  test_octave_and_volume();
  test_musical_mode();
  test_drum_picks_notes_defaults();
  test_globals();
  test_percussion_bank_reaches_the_synth();
  test_extra_footbasses();
  test_whistle();

  if (failures) {
    printf("\n%d failure(s)\n", failures);
    return 1;
  }
  printf("all keypad tests passed (%d keys in the layout)\n", N_KEYS);
  return 0;
}
