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
#include "speechwords.h"
#include "speechgate.h"
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

// The second drones on 8 and 9, and the drones' own list of pads on the voice
// keys.
static void test_drones() {
  full_reset();

  struct { const char* cap; int endpoint; int like; const char* name; }
  keys[] = {
    {"8", ENDPOINT_DRONE_BASS_2, ENDPOINT_DRONE_BASS, "drone bass 2"},
    {"9", ENDPOINT_DRONE_CHORD_2, ENDPOINT_DRONE_CHORD, "drone chord 2"},
  };

  for (int i = 0; i < 2; i++) {
    const Key* key = key_for_cap(keys[i].cap);
    CHECK(key != NULL && key->lit == LIT_EP_ON &&
          key->arg == keys[i].endpoint,
          "%s isn't the %s key", keys[i].cap, keys[i].name);

    // Cleared exactly like the drone it's a second copy of.
    int e = keys[i].endpoint, o = keys[i].like;
    CHECK(c->voices[e] == c->voices[o] && c->chord[e] == c->chord[o] &&
          c->shorter[e] == c->shorter[o],
          "%s should start out like the first one", keys[i].name);
    CHECK(is_drone(e) && holds_bass_note(e),
          "%s should count as a drone", keys[i].name);

    CHECK(!c->on[e], "%s starts off", keys[i].name);
    press(keys[i].cap);
    CHECK(c->on[e], "%s didn't switch %s on", keys[i].cap, keys[i].name);
    CHECK(c->selected_endpoint == e, "toggling %s didn't select it",
          keys[i].name);
    CHECK(current_note[e] != -1, "%s is on but holding no note",
          keys[i].name);
    press(keys[i].cap);
    CHECK(!c->on[e], "%s didn't switch %s back off", keys[i].cap,
          keys[i].name);
    CHECK(current_note[e] == -1, "%s is off but still holding a note",
          keys[i].name);

    select_ep("R");
    select_ep(keys[i].cap);
    CHECK(c->selected_endpoint == e, "shift-%s didn't select %s",
          keys[i].cap, keys[i].name);
  }
  CHECK(!is_drone(ENDPOINT_JAWHARP) && holds_bass_note(ENDPOINT_JAWHARP),
        "the jawharp holds a note but isn't a drone");
  CHECK(!holds_bass_note(ENDPOINT_FOOTBASS) && !is_drone(ENDPOINT_DRUM),
        "nothing else is a drone");

  // Every pad on the list is on a voice key, and picks and lights there, on
  // each of the four drones.
  int drones[] = {ENDPOINT_DRONE_BASS, ENDPOINT_DRONE_CHORD,
                  ENDPOINT_DRONE_BASS_2, ENDPOINT_DRONE_CHORD_2};
  for (int d = 0; d < 4; d++) {
    c->selected_endpoint = drones[d];
    for (int v = 0; v < N_DRONE_VOICES; v++) {
      const Key* key = key_for_cap_note(DRONE_VOICES[v].note);
      CHECK(key && key->group == GROUP_VOICE, "pad %d isn't on a voice key",
            DRONE_VOICES[v].program);
      if (!key) continue;
      keypad_key(key->note);
      CHECK(c->voices[drones[d]] == DRONE_VOICES[v].program,
            "%s didn't pick %d on endpoint %d", key->cap,
            DRONE_VOICES[v].program, drones[d]);
      CHECK(key_is_lit(key), "%s should be lit for the pad it picked",
            key->cap);
      CHECK(drone_voice_on_key(key) == v, "%s should draw as its pad",
            key->cap);
    }
  }
  CHECK(drone_voice_for_note('H') >= 0 &&
        DRONE_VOICES[drone_voice_for_note('H')].program == 18,
        "Rock Organ should still be on H");

  // The voice keys with no pad do nothing, and draw dead.
  select_ep("I");
  press("Z");
  const char* blank[] = {"B", "N", "M"};
  for (int i = 0; i < 3; i++) {
    press(blank[i]);
    CHECK(c->voices[ENDPOINT_DRONE_BASS] == 89,
          "%s changed the drone's voice; it should be blank", blank[i]);
    CHECK(drone_key_is_dead(key_for_cap(blank[i])), "%s should draw dead",
          blank[i]);
    CHECK(!lit(blank[i]), "%s shouldn't light", blank[i]);
  }

  // And everywhere else the voice keys are the usual ones again.
  select_ep("R");
  press("A");
  CHECK(c->voices[ENDPOINT_FLEX] == 39, "A should be SynBass 2 on flex");
  CHECK(drone_voice_on_key(key_for_cap("A")) < 0 &&
        !drone_key_is_dead(key_for_cap("B")),
        "the drone labels shouldn't follow you off the drones");
  CHECK(CHANNEL_PITCHED_KICK >= N_ENDPOINTS,
        "the pitched kick is sitting on endpoint %d", CHANNEL_PITCHED_KICK);
}

// Feeds a steady pitch for a while, in 5ms blocks, the way the audio thread
// would.  Returns true if a note finished during it.
static bool feed_for(WhistleNoteTracker* t, double midi, double seconds,
                     double* pitch) {
  bool done = false;
  double hz = midi > 0 ? 440.0 * pow(2, (midi - 69) / 12.0) : 0;
  for (double s = 0; s < seconds - 1e-9; s += 0.005) {
    if (wn_feed(t, midi > 0, hz, 0.005, pitch)) done = true;
  }
  return done;
}

// The whistle choosing the chord: turning a stream of pitches into notes, the
// notes into Drum Some's chords, and the key that switches it.
static void test_whistle_picks() {
  WhistleNoteTracker t;
  wn_reset(&t);
  double pitch = -1;

  // A scoop up from a whole tone below into an A, held, then released: the
  // held note is what counts, and only once it has stopped.
  CHECK(!feed_for(&t, 79, 0.04, &pitch), "reported during the scoop");
  CHECK(!feed_for(&t, 80, 0.03, &pitch), "reported during the scoop");
  CHECK(!feed_for(&t, 81, 0.40, &pitch), "reported while still sounding");
  CHECK(!feed_for(&t, 0, 0.02, &pitch), "reported before the gap was long");
  CHECK(feed_for(&t, 0, 0.10, &pitch), "a held note never finished");
  CHECK(fabs(pitch - 81) < 0.25, "scoop into A came out as %.2f", pitch);

  // Too short to count.
  CHECK(!feed_for(&t, 81, 0.10, &pitch) && !feed_for(&t, 0, 0.2, &pitch),
        "a 100ms blip counted as a note");

  // The detector dropping out briefly doesn't split one note into two.
  pitch = -1;
  bool early = feed_for(&t, 76, 0.10, &pitch);
  early |= feed_for(&t, 0, 0.02, &pitch);
  early |= feed_for(&t, 76, 0.10, &pitch);
  CHECK(!early, "a 20ms dropout ended the note");
  CHECK(feed_for(&t, 0, 0.1, &pitch) && fabs(pitch - 76) < 0.25,
        "the note either side of a dropout didn't count as one");

  // Snapping, in D major: I D, IV G, V A, vi B.
  full_reset();
  root_note = to_root(26);
  fifth_note = to_root(root_note + 7);
  drum_chooses_some_notes = true;
  musical_mode = MODE_MAJOR;
  CHECK(drum_some_pedal_for_note(81) == MIDI_PEDAL_34, "A should be V");
  CHECK(drum_some_pedal_for_note(79) == MIDI_PEDAL_4, "G should be IV");
  CHECK(drum_some_pedal_for_note(74) == MIDI_PEDAL_3, "D should be I");
  CHECK(drum_some_pedal_for_note(83) == MIDI_PEDAL_1, "B should be vi");
  CHECK(drum_some_pedal_for_note(72) == MIDI_PEDAL_1,
        "C is a semitone from B and a tone from D, so vi");
  CHECK(drum_some_pedal_for_note(80) == -1,
        "G# is between G and A; a tie should pick nothing");
  // Minor: i D, bVII C, V A, bVI Bb.
  musical_mode = MODE_MINOR;
  CHECK(drum_some_pedal_for_note(82) == MIDI_PEDAL_34, "Bb should be bVI");
  CHECK(drum_some_pedal_for_note(72) == MIDI_PEDAL_4, "C should be bVII");
  // Freygish gives the I to two pedals; that isn't a tie.
  musical_mode = MODE_BETH_COHENS;
  CHECK(drum_some_pedal_for_note(74) >= 0, "D in Freygish should be the I");

  // The key: F8 switches it on, with Drum Some underneath and lit as F8.
  full_reset();
  root_note = to_root(26);
  fifth_note = to_root(root_note + 7);
  whistle_picks_note(81);
  CHECK(active_note() == root_note, "whistling did something with it off");
  press("F8");
  CHECK(whistle_chooses_notes && drum_chooses_some_notes,
        "F8 didn't switch on the whistle choosing");
  CHECK(lit("F8") && !lit("F5"), "F8 should light, and F5 shouldn't");

  whistle_picks_note(81);  // A
  CHECK(active_note() == to_root(33), "whistling A didn't pick the V");
  whistle_picks_note(83);  // B
  CHECK(active_note() == to_root(35) && chord_type == CHORD_MINOR,
        "whistling B didn't pick the vi, minor");
  whistle_picks_note(80);  // G#, a tie
  CHECK(active_note() == to_root(35), "a tie changed the chord");

  // The feet keep time but don't choose.
  handle_feet(MIDI_ON, MIDI_PEDAL_3, 100);
  handle_feet(MIDI_ON, MIDI_PEDAL_4, 100);
  CHECK(active_note() == to_root(35), "a pedal changed the chord");

  // F5 hands the choice back to the feet, leaving Drum Some on.
  press("F5");
  CHECK(!whistle_chooses_notes && drum_chooses_some_notes && lit("F5"),
        "F5 should switch to the feet choosing");
  handle_feet(MIDI_ON, MIDI_PEDAL_4, 100);
  CHECK(active_note() == to_root(26 + 5), "the feet didn't take over");
  press("F5");
  CHECK(!drum_chooses_some_notes, "F5 again should switch Drum Some off");

  // F8 off takes Drum Some with it; F9 and escape both end it.
  press("F8");
  press("F8");
  CHECK(!whistle_chooses_notes && !drum_chooses_some_notes,
        "F8 off should leave neither on");
  press("F8");
  press("F9");
  CHECK(drum_chooses_notes && !whistle_chooses_notes &&
        !drum_chooses_some_notes, "F9 should take over from the whistle");
  press("F9");
  press("F8");
  press("esc");
  CHECK(!whistle_chooses_notes && !drum_chooses_some_notes,
        "escape should end the whistle choosing");
}

static SwAction next_action(const char* const* words, int n, int* consumed,
                            bool settled);

// Spoken Nashville numbers: which words count, acting on a transcription
// that revises itself, and the chord each number gives.
static void test_nashville_numbers() {
  CHECK(nw_number_for_word("Four") == 4 && nw_number_for_word("seven.") == 7 &&
        nw_number_for_word("1") == 1 && nw_number_for_word("SIX") == 6,
        "number words and digits should count");
  CHECK(!nw_number_for_word("for") && !nw_number_for_word("to") &&
        !nw_number_for_word("8") && !nw_number_for_word("eleven") &&
        !nw_number_for_word(""), "those shouldn't count");

  // "go to" then revised to "go two": the revision counts, once.
  static char names[256][SW_NAME_MAX];
  static int keys[256];
  int n_names = key_spoken_names(names, keys, 256);
  int consumed = 0;
  const char* first[] = {"go", "to"};
  CHECK(next_action(first, 2, &consumed, false).kind == SW_NONE,
        "'to' isn't a number");
  const char* revised[] = {"go", "two"};
  SwAction a = next_action(revised, 2, &consumed, false);
  CHECK(a.kind == SW_NUMBER && a.value == 2, "the revision to 'two'");
  CHECK(next_action(revised, 2, &consumed, false).kind == SW_NONE,
        "acted on twice");
  const char* more[] = {"go", "two", "and", "five", "four"};
  a = next_action(more, 5, &consumed, false);
  SwAction b = next_action(more, 5, &consumed, false);
  CHECK(a.value == 5 && b.value == 4 &&
        next_action(more, 5, &consumed, false).kind == SW_NONE,
        "later numbers should each come once, in order");
  (void)n_names;
}

// Runs the parser against the buttons as they're named right now.
static SwAction next_action(const char* const* words, int n, int* consumed,
                            bool settled) {
  static char names[256][SW_NAME_MAX];
  static int keys[256];
  int n_names = key_spoken_names(names, keys, 256);
  SwVocab buttons = {(const char (*)[SW_NAME_MAX])names, keys, n_names};
  SwVocab modes = sw_mode_vocab(MODE_MAJOR, MODE_MINOR, MODE_MIXO,
                                MODE_BETH_COHENS);
  return sw_next_action(words, n, consumed, &buttons, &modes, settled);
}

// The whole of what a phrase does, settled, as "press <cap>" / "select <cap>"
// / "<number>" separated by spaces, for comparing in one go.
static const char* phrase(const char* const* words, int n, bool settled) {
  static char out[256];
  out[0] = '\0';
  int consumed = 0;
  SwAction a;
  while ((a = next_action(words, n, &consumed, settled)).kind != SW_NONE) {
    char one[32];
    if (a.kind == SW_NUMBER) {
      snprintf(one, sizeof(one), "%d", a.value);
    } else if (a.kind == SW_KEY || a.kind == SW_MODE) {
      snprintf(one, sizeof(one), "%s=%d", a.kind == SW_KEY ? "key" : "mode",
               a.value);
    } else {
      snprintf(one, sizeof(one), "%s %s",
               a.kind == SW_PRESS ? "press" : "select", KEYS[a.value].cap);
    }
    if (out[0]) strcat(out, " ");
    strcat(out, one);
  }
  return out;
}

// "press <button>": the keyword, the names, and waiting for a name that
// could still grow.
static void test_spoken_presses() {
  full_reset();
  whistle_reset();

  // Every key that does something has a name.
  static char names[256][SW_NAME_MAX];
  static int keys[256];
  int n = key_spoken_names(names, keys, 256);
  for (int i = 0; i < N_KEYS; i++) {
    if (!KEYS[i].label) continue;
    bool named = false;
    for (int k = 0; k < n; k++) if (keys[k] == i) named = true;
    CHECK(named, "key %s has no spoken name", KEYS[i].cap);
  }

  // And in no state is one key's name the same as another's, or the start of
  // it: a name that's the start of another has to wait to see whether it's
  // going to grow, and saying the longer one with a pause in it presses the
  // shorter.
  for (int state = 0; state < 4; state++) {
    static const char* STATES[] = {"flex", "drum", "drone", "whistle"};
    whistle_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS : ENDPOINT_FLEX;
    if (state == 3) whistle_selected = true;
    n = key_spoken_names(names, keys, 256);
    for (int j = 0; j < n; j++) {
      for (int k = 0; k < n; k++) {
        if (j == k || keys[j] == keys[k]) continue;
        CHECK(strncmp(names[j], names[k], strlen(names[j])) != 0,
              "with %s selected, %s's '%s' is the start of %s's '%s'",
              STATES[state], KEYS[keys[j]].cap, names[j], KEYS[keys[k]].cap,
              names[k]);
      }
    }
  }
  whistle_reset();
  c->selected_endpoint = ENDPOINT_FLEX;

#define PHRASE(settled, ...) \
  phrase((const char*[]){__VA_ARGS__}, \
         (int)(sizeof((const char*[]){__VA_ARGS__}) / sizeof(char*)), settled)

  CHECK(strcmp(PHRASE(true, "foot", "bass"), "") == 0,
        "a name without 'press' shouldn't press anything");
  CHECK(strcmp(PHRASE(false, "press", "foot", "bass"), "press W") == 0,
        "'press foot bass' needn't wait: nothing longer starts that way");
  CHECK(strcmp(PHRASE(false, "press", "bounce", "bass", "two"),
               "press 2 2") == 0,
        "'press bounce bass' then a separate two");
  CHECK(strcmp(PHRASE(false, "Press", "skip", "bass"), "press 3") == 0,
        "skip bass");
  CHECK(strcmp(PHRASE(false, "press", "drum", "kit", "four"),
               "press tab 4") == 0, "'press drum kit four'");
  CHECK(strcmp(PHRASE(false, "press", "drum"), "") == 0,
        "'press drum' is the start of three names; wait");
  CHECK(strcmp(PHRASE(true, "press", "drum"), "") == 0,
        "and 'drum' on its own isn't any of them");
  CHECK(strcmp(PHRASE(false, "press", "drum", "chooses", "notes"),
               "press F9") == 0, "drum chooses notes");
  CHECK(strcmp(PHRASE(true, "press", "drum", "chooses"), "") == 0,
        "the cut-short label isn't a name of its own");
  CHECK(strcmp(PHRASE(false, "press", "speech", "recognition"),
               "press F8") == 0, "speech recognition");
  CHECK(strcmp(PHRASE(false, "press", "whistle", "bass"), "press 1") == 0,
        "whistle bass");
  CHECK(strcmp(PHRASE(false, "press", "pad", "chord"), "press 9") == 0,
        "pad chord");
  CHECK(strcmp(PHRASE(false, "press", "channel", "swap"), "press F2") == 0,
        "channel swap");
  CHECK(strcmp(PHRASE(false, "press", "pulse"), "press F4") == 0, "pulse");
  CHECK(strcmp(PHRASE(false, "press", "arpeggiator"), "press E") == 0,
        "arpeggiator");
  CHECK(strcmp(PHRASE(false, "press", "upper"), "press Y") == 0, "upper");
  CHECK(strcmp(PHRASE(false, "press", "mixolydian"), "press ←") == 0,
        "mixolydian");
  CHECK(strcmp(PHRASE(true, "press", "banana", "four"), "4") == 0,
        "a keyword that names nothing should be dropped");
  CHECK(strcmp(PHRASE(true, "press", "octave", "up"), "press ]") == 0,
        "aliases should work");
  CHECK(strcmp(PHRASE(true, "select", "skip", "bass"), "select 3") == 0,
        "select");
  CHECK(strcmp(PHRASE(true, "press", "warm", "pad"), "") == 0,
        "warm pad isn't on any key unless a drone is selected");

  // The recognizer hands words over slowly: "press" can sit alone past the
  // settle time before "foot bass" arrives, and mustn't be thrown away.
  {
    int consumed = 0;
    const char* early[] = {"press"};
    CHECK(next_action(early, 1, &consumed, true).kind == SW_NONE &&
          consumed == 0, "a lone 'press' should keep waiting, even settled");
    const char* later[] = {"press", "foot", "bass"};
    SwAction a = next_action(later, 3, &consumed, true);
    CHECK(a.kind == SW_PRESS && a.value == key_for_cap("W") - KEYS,
          "'foot bass' arriving after a settled 'press' should press W");
    const char* half[] = {"change", "key"};
    consumed = 0;
    CHECK(next_action(half, 2, &consumed, true).kind == SW_NONE &&
          consumed == 0, "'change key' should wait for its key");
  }

  // A first guess that fits no name is revised soon after, so while the
  // speaker is still going the "press" waits rather than being dropped.
  {
    int consumed = 0;
    const char* guess[] = {"press", "our", "page"};
    CHECK(next_action(guess, 3, &consumed, false).kind == SW_NONE &&
          consumed == 0, "'press our page' should wait for a revision");
    const char* revised[] = {"press", "arpeggiator"};
    SwAction a = next_action(revised, 2, &consumed, false);
    CHECK(a.kind == SW_PRESS && a.value == key_for_cap("E") - KEYS,
          "the revision to 'press arpeggiator' should press E");
    char title[48];
    key_spoken_title(key_for_cap("E"), title, sizeof(title));
    CHECK(strcmp(title, "arpeggiator") == 0, "E should be called '%s'",
          title);
  }

  // After a lead-in, sound-alikes count; bare, they don't.
  CHECK(strcmp(PHRASE(false, "press", "foot", "base"), "press W") == 0,
        "'foot base' should be foot bass");
  CHECK(strcmp(PHRASE(false, "pressed", "pad", "cord"), "press 9") == 0,
        "'pressed pad cord' should be pad chord");
  CHECK(strcmp(PHRASE(true, "for"), "") == 0,
        "a bare 'for' mustn't pick the IV");
  CHECK(strcmp(PHRASE(false, "set", "mode", "to", "minor", "now"),
               "mode=3") == 0, "'set' works like 'change'");
  CHECK(strcmp(PHRASE(true, "set", "key", "too", "F", "sharp"),
               "key=6") == 0, "set key too F sharp");

  // Changing key and mode.
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "A", "now"),
               "key=9") == 0, "change key to A");
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "B"), "") == 0,
        "'B' could still be 'B flat'; wait");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "B"), "key=11") == 0,
        "and settled, it's B");
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "B", "flat"),
               "key=10") == 0, "B flat");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "B♭"), "key=10") == 0,
        "B♭ as written");
  CHECK(strcmp(PHRASE(true, "change", "key", "two", "Bb"), "key=10") == 0,
        "Bb, with 'to' heard as 'two'");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "F#"), "key=6") == 0,
        "F#");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "see", "sharp"),
               "key=1") == 0, "a sound-alike letter");
  CHECK(strcmp(PHRASE(true, "change", "key", "G"), "key=7") == 0,
        "'to' is optional");
  char want[32];
  snprintf(want, sizeof(want), "mode=%d", MODE_MINOR);
  CHECK(strcmp(PHRASE(false, "change", "mode", "to", "minor"), want) == 0,
        "change mode to minor");
  snprintf(want, sizeof(want), "mode=%d", MODE_BETH_COHENS);
  CHECK(strcmp(PHRASE(false, "change", "mode", "to", "freygish"), want) == 0,
        "change mode to freygish");
  CHECK(strcmp(PHRASE(true, "change", "the", "key", "to", "A"), "") == 0,
        "not quite the phrase: nothing");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "banana", "five"),
               "5") == 0, "a key that names nothing, then a number");

  // The voice keys answer to whatever they show.
  select_ep("I");
  CHECK(strcmp(PHRASE(true, "press", "warm", "pad"), "press Z") == 0,
        "with a drone selected, warm pad is on Z");
  CHECK(strcmp(PHRASE(true, "press", "vox", "lead"), "") == 0,
        "and vox lead isn't anywhere");
  CHECK(strcmp(PHRASE(true, "press", "saw", "lead"), "") == 0,
        "and B, N and M do nothing, so don't answer");
  select_ep("tab");
  CHECK(strcmp(PHRASE(true, "press", "room", "two"), "press C") == 0,
        "with the drum selected, room 2 is on C");
  strike("1", true);
  CHECK(strcmp(PHRASE(true, "press", "reese"), "press D") == 0,
        "with the whistle selected, reese is on D");
  CHECK(strcmp(PHRASE(true, "press", "frequency", "modulator"), "press G") == 0,
        "frequency modulator");
  CHECK(strcmp(PHRASE(true, "press", "sub", "fm"), "press H") == 0, "sub fm");
  CHECK(strcmp(PHRASE(true, "press", "high", "drawbar"), "press C") == 0,
        "high drawbar");
  whistle_reset();

  CHECK(key_selects(key_for_cap("W")) && key_selects(key_for_cap("1")) &&
        !key_selects(key_for_cap("J")), "which keys shift-select");

  // And they do what they say.
  full_reset();
  const char* words[] = {"change", "key", "to", "E", "flat"};
  int consumed = 0;
  SwAction a = next_action(words, 5, &consumed, true);
  CHECK(a.kind == SW_KEY, "change key to E flat");
  change_key(a.value);
  CHECK(root_note == to_root(3) && fifth_note == to_root(10),
        "change_key should move the root and the fifth");
#undef PHRASE
}

// The recognizer's dictionary (all_spoken_phrases, and speechphrases.c from
// it) only helps if what it's taught is what the matcher accepts: every
// phrase has to be some button's name in some state, and every word given a
// pronunciation has to be in some phrase.
static void test_speech_dictionary() {
  full_reset();
  static char known[1024][SW_NAME_MAX];
  int n_known = 0;
  for (int state = 0; state < 4; state++) {
    whistle_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS : ENDPOINT_FLEX;
    if (state == 3) whistle_selected = true;
    static char names[256][SW_NAME_MAX];
    static int keys[256];
    int n = key_spoken_names(names, keys, 256);
    for (int i = 0; i < n && n_known < 1024; i++) {
      snprintf(known[n_known++], SW_NAME_MAX, "%s", names[i]);
    }
  }
  whistle_reset();

  static char phrases[512][48];
  int n = all_spoken_phrases(phrases, 512);
  CHECK(n > 50, "only %d phrases", n);
  for (int i = 0; i < n; i++) {
    char name[SW_NAME_MAX];
    sw_name(phrases[i], name, sizeof(name));
    bool found = false;
    for (int k = 0; k < n_known; k++) {
      if (strcmp(known[k], name) == 0) found = true;
    }
    CHECK(found, "the recognizer is taught '%s', which presses nothing",
          phrases[i]);
    CHECK(!strchr(phrases[i], '\n'), "'%s' has a line break", phrases[i]);
  }

  for (int p = 0; p < (int)(sizeof(SW_PRONUNCIATIONS) /
                            sizeof(SW_PRONUNCIATIONS[0])); p++) {
    bool used = strcmp(SW_PRONUNCIATIONS[p].word, "mixolydian") == 0 ||
                strcmp(SW_PRONUNCIATIONS[p].word, "freygish") == 0;
    for (int i = 0; i < n; i++) {
      if (strstr(phrases[i], SW_PRONUNCIATIONS[p].word)) used = true;
    }
    CHECK(used, "'%s' has a pronunciation but isn't in any phrase",
          SW_PRONUNCIATIONS[p].word);
  }
}

// The gate in front of the speech recognizer: quiet comes out as silence,
// and loud comes through with a little before it and a little after.
static void test_speech_gate() {
  SpeechGate g;
  sg_init(&g, 48000);
  g.threshold = 0.1f;  // -20dB
  int second = 48000, w = g.window;
  static float in[48000 * 3], out[48000 * 3 + 480];
  // A second of room, a tenth of a second of talking, then more room.
  for (int i = 0; i < 3 * second; i++) {
    bool talking = i >= second && i < second + second / 10;
    in[i] = talking ? 0.5f : 0.01f;
  }
  int n = sg_process(&g, in, 3 * second, out);
  CHECK(n == 3 * second - SG_PRE_WINDOWS * w,
        "everything but any look-ahead should be out, got %d", n);
  CHECK(!sg_open(&g), "the gate should have closed again");
  CHECK(sg_windows_since_loud(&g) == (3 * second - second - second / 10) / w,
        "the last loud window is where the talking stopped");

  // It comes out late, but in order: out[i] is in[i], gated.
  int start = second, end = second + second / 10;
  CHECK(out[start - 20 * w] == 0,
        "room 200ms before the talking should be silent");
  CHECK(out[start - 5 * w] == (SG_PRE_WINDOWS >= 5 ? 0.01f : 0),
        "the 50ms just before the talking: only with enough look-ahead");
  CHECK(out[start + 100] == 0.5f, "the talking itself should");
  CHECK(out[end + 20 * w] == 0.01f,
        "200ms after, the hold should still be open");
  CHECK(out[end + 40 * w] == 0, "400ms after, it should be shut again");
}

// The chord each spoken number gives.
static void test_nashville_chords() {
  // The chords: the major key on the root, whatever the arrows say.
  full_reset();
  root_note = to_root(26);  // D
  fifth_note = to_root(root_note + 7);
  nashville_picks_chord(4);
  CHECK(active_note() == root_note, "a number did something with F8 off");
  press("F8");
  musical_mode = MODE_MINOR;
  struct { int number, note, type; } want[] = {
    {1, 26, CHORD_MAJOR}, {2, 28, CHORD_MINOR}, {3, 30, CHORD_MINOR},
    {4, 31, CHORD_MAJOR}, {5, 33, CHORD_MAJOR}, {6, 35, CHORD_MINOR},
    {7, 25, CHORD_DIM},
  };
  for (int i = 0; i < 7; i++) {
    nashville_picks_chord(want[i].number);
    CHECK(active_note() == to_root(want[i].note) &&
          active_chord() == to_root(want[i].note) &&
          chord_type == want[i].type,
          "%d should be note %d type %d, got %d type %d", want[i].number,
          to_root(want[i].note), want[i].type, active_note(), chord_type);
  }
  nashville_picks_chord(8);
  nashville_picks_chord(0);
  CHECK(active_note() == to_root(25), "out-of-range numbers did something");

  // Numbers and whistled notes take turns.
  musical_mode = MODE_MAJOR;
  whistle_picks_note(81);  // A
  CHECK(active_note() == to_root(33), "whistling after a number didn't work");
  nashville_picks_chord(2);
  CHECK(active_note() == to_root(28), "a number after whistling didn't work");
  press("esc");
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
  test_drones();
  test_whistle();
  test_whistle_picks();
  test_nashville_numbers();
  test_spoken_presses();
  test_nashville_chords();
  test_speech_dictionary();
  test_speech_gate();

  if (failures) {
    printf("\n%d failure(s)\n", failures);
    return 1;
  }
  printf("all keypad tests passed (%d keys in the layout)\n", N_KEYS);
  return 0;
}
