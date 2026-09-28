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
#include "numrec.h"

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
// the routing rather than just the half below it: what
// -[JammerView strikeKeyAtIndex:selecting:] calls.
static void strike(const char* cap, bool selecting) {
  const Key* key = key_for_cap(cap);
  assert(key && "no such key in the layout");
  strike_key_locked(key, selecting);
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
    // The whistle and the mandolin aren't fluidsynth channels and don't go
    // through handle_keypad, so their keys deliberately send nothing.
    bool whistle_key_entry = (k->lit == LIT_WHISTLE_ON ||
                              k->lit == LIT_MANDO_ON);
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
  press("Z");
  press("A");
  CHECK(c->drum_voice == KIT_RIM, "A didn't pick the rim kit");
  CHECK(lit("A"), "A should be lit for the rim kit");
  // The lit kit's key again: no kit at all, and nothing lit.
  press("A");
  CHECK(c->drum_voice == KIT_NONE && !lit("A") && !lit("Z"),
        "A again should switch the kit off");
  press("A");
  CHECK(c->drum_voice == KIT_RIM && lit("A"), "and A again should pick Rim");
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "picking a drum changed a voice");

  press("Z");
  CHECK(c->drum_voice == KIT_808_A, "Z didn't pick the first 808 kit");
  CHECK(lit("Z"), "Z should be lit for the 808 kit");
  CHECK(c->voices[ENDPOINT_FLEX] == 75, "picking a kit changed a voice");

  // The voice keys with no kit on them do nothing at all with the drum
  // selected -- in particular they must not fall through and set a melodic
  // voice on a channel that's playing a percussion set.
  // S and D are the Feet and the Stompy Feet, but only with the Mac's sound
  // for them, and there's none here.
  const char* blank[] = {"S", "D", "G", "H", "B", "N"};
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
  select_ep("E");  // select the arp

  CHECK(!lit("["), "pre unique starts off for the arp");
  press("[");
  CHECK(c->pre_unique[ENDPOINT_ARP], "[ didn't set pre unique");
  CHECK(lit("["), "[ should be lit");
  press("[");
  CHECK(!c->pre_unique[ENDPOINT_ARP], "[ didn't clear pre unique");

  select_ep("W");  // select the foot bass
  press(",");
  CHECK(c->chord[ENDPOINT_FOOTBASS] && lit(","), "comma didn't set chord");

  // Flags are per endpoint, so switching endpoints switches what's lit.
  select_ep("E");  // select the arp
  CHECK(!lit(","), "chord leaked from the foot bass to the arp");
}

// A modifier the selected endpoint never reads draws dead, and dark even if
// it's set, since being on does nothing.
static void test_ignored_flags_are_dead() {
  full_reset();

  select_ep("R");  // flex: played from the piano, always at full velocity
  CHECK(key_is_dead(key_for_cap("P")), "II should be dead on Flex");
  CHECK(key_is_dead(key_for_cap("J")) && !lit("J"),
        "DOWN BEAT is set by default but should be dead and dark on Flex");
  CHECK(key_is_dead(key_for_cap(".")), "VEL should be dead on Flex");
  CHECK(key_is_dead(key_for_cap(",")), "CHORD should be dead on Flex");
  CHECK(!key_is_dead(key_for_cap("]")), "OCT+ should be live on Flex");
  CHECK(!key_is_dead(key_for_cap("F4")), "PULSE should be live on Flex");

  // Nor does it move Flex up, which it used to: the two octaves are for the
  // chord it builds, and the piano's endpoints don't build one.
  int plain = endpoint_note(40, ENDPOINT_FLEX);
  press(",");
  CHECK(endpoint_note(40, ENDPOINT_FLEX) == plain,
        "CHORD shouldn't move Flex's notes");
  press(",");

  select_ep("T");  // low: the piano's velocity, if asked
  CHECK(!key_is_dead(key_for_cap(".")), "VEL should be live on Low");
  CHECK(key_is_dead(key_for_cap(";")), "CLIPPED should be dead on Low");

  select_ep("W");  // the foot bass reads everything
  for (int i = 0; i < N_KEYS; i++) {
    CHECK(!key_is_dead(&KEYS[i]), "%s should be live on the foot bass",
          KEYS[i].cap);
  }

  select_ep("tab");  // the drum: no pitch to move, and CHORD's BREATH FILL
  CHECK(!key_is_dead(key_for_cap(",")), "BREATH FILL should be live on the "
        "drum");
  CHECK(key_is_dead(key_for_cap("]")) && key_is_dead(key_for_cap("\\")),
        "OCT should be dead on the drum");
  CHECK(!key_is_dead(key_for_cap("P")), "II should be live on the drum");

  select_ep("Q");  // the jawharp holds a note: no rhythm
  CHECK(key_is_dead(key_for_cap("P")) && key_is_dead(key_for_cap("K")),
        "II and UP BEAT should be dead on the jawharp");
  CHECK(!key_is_dead(key_for_cap("'")), "SHORTER re-strikes the jawharp");

  select_ep("I");  // a drone: II and Q are its trance gate
  CHECK(!key_is_dead(key_for_cap("P")) && !key_is_dead(key_for_cap("[")),
        "II and Q should be live on a drone");
  CHECK(key_is_dead(key_for_cap("L")), "UP HIGH should be dead on a drone");
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

  press("5");
  CHECK(kick_duck && lit("5"), "5 didn't toggle kick duck");

  press("esc");
  CHECK(!jig_time && !drum_chooses_notes && !kick_duck,
        "escape didn't reset");
}

// Kick Duck is set off by the rig's kick sounding, and by the kick pedal
// whether or not the drum is on: only while it's on, and once per hit.
static int kicks_ducked;
static void count_kick_duck(uint64_t beat_ns) {
  CHECK(beat_ns > 0, "a duck needs a beat to come back up over");
  kicks_ducked++;
}

static void test_kick_duck() {
  full_reset();
  kick_hook = count_kick_duck;
  c->on[ENDPOINT_DRUM] = true;
  c->downbeat[ENDPOINT_DRUM] = true;  // the kick; the drum clears to hats
  arpeggiate_drum(0, now());
  CHECK(kicks_ducked == 0, "a kick ducked with Kick Duck off");
  press("5");
  arpeggiate_drum(0, now());
  CHECK(kicks_ducked == 1, "a kick didn't duck with Kick Duck on");
  arpeggiate_drum(72 / 2, now());
  CHECK(kicks_ducked == 1, "the upbeat has no kick, so shouldn't duck");
  c->downbeat[ENDPOINT_DRUM] = false;
  arpeggiate_drum(0, now());
  CHECK(kicks_ducked == 1, "no kick on the downbeat, so no duck");

  // The pedals playing a drum synth of their own, with the drum here off.
  last_kick_duck_ns = 0;
  c->on[ENDPOINT_DRUM] = false;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
  CHECK(kicks_ducked == 2, "the kick pedal didn't duck with the drum off");
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(kicks_ducked == 2, "the snare pedal shouldn't duck");

  // The pedal and the drum's kick on the same hit are one duck.
  last_kick_duck_ns = 0;
  c->on[ENDPOINT_DRUM] = true;
  c->downbeat[ENDPOINT_DRUM] = true;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
  arpeggiate_drum(0, now());
  CHECK(kicks_ducked == 3, "one hit ducked %d times", kicks_ducked - 2);
  kick_hook = NULL;
  full_reset();
}

// The breath sweeps, and the Breath Gate's percussion, are told what's on
// and how hard you're blowing whenever either changes, and escape switches
// it all off.
static int told_breath;
static unsigned told_fx;
static double told_breath_gain;
static void record_breath_gain(double gain) {
  told_breath_gain = gain;
}

// The drum's side, told alongside, is apart from the rest: told_drum_right.
static bool told_drum_right;
static void record_breath(int breath, unsigned fx) {
  told_breath = breath;
  told_fx = fx & ~BREATH_FX_DRUM_RIGHT;
  told_drum_right = fx & BREATH_FX_DRUM_RIGHT;
}

static void test_breath_fx() {
  full_reset();
  breath_hook = record_breath;
  struct { const char* cap; unsigned fx; } keys[] = {
    {"4", BREATH_FX_SWEEP_BASS}, {"6", BREATH_FX_SWEEP_TREBLE},
    {"7", BREATH_FX_SWEEP_PEAK},
  };
  unsigned all = 0;
  for (int i = 0; i < 3; i++) {
    press(keys[i].cap);
    all |= keys[i].fx;
    CHECK(told_fx == all && lit(keys[i].cap), "%s didn't switch its sweep on",
          keys[i].cap);
  }
  handle_cc(CC_BREATH, 90);
  CHECK(told_breath == 90, "the breath effects weren't told the breath");
  press("6");
  CHECK(told_fx == (all & ~BREATH_FX_SWEEP_TREBLE) && !lit("6"),
        "6 didn't switch only its own sweep off");
  press("esc");
  CHECK(told_fx == 0 && !lit("4") && !lit("7"),
        "escape didn't switch the sweeps off");
  breath_hook = NULL;
  full_reset();
}

// The Breath Gate on `: a drone chord like Dc, starting on Warm Pad, whose
// voice keys are the drones' pads plus its own percussion on B, N and M.  On
// one of those it plays no notes, and the Mac's audio is told to play it
// instead -- only while it's on.
static void test_breath_gate() {
  full_reset();
  breath_hook = record_breath;
  const Key* key = key_for_cap("`");
  CHECK(key && key->lit == LIT_EP_ON && key->arg == ENDPOINT_BREATH,
        "` isn't the Breath Gate");
  CHECK(is_drone(ENDPOINT_BREATH) && c->chord[ENDPOINT_BREATH] &&
        c->voices[ENDPOINT_BREATH] == VOICE_BREATH_NO_PAD &&
        c->breath_layers == BREATH_LAYER_BRUSHES,
        "the Breath Gate should start as the Brushes alone, with no pad");

  press("`");
  CHECK(c->on[ENDPOINT_BREATH] && c->selected_endpoint == ENDPOINT_BREATH,
        "` didn't switch the Breath Gate on and select it");
  CHECK(lit("J") && !lit("A") && !lit("C"),
        "J should show the Brushes on, and no pad");
  CHECK(told_fx == BREATH_FX_BRUSH, "the Brushes should be all the Mac "
        "plays");

  // The rest of this is a pad, Halo Pad, on its own.
  press("C");
  press("J");
  CHECK(c->voices[ENDPOINT_BREATH] == 94 && c->breath_layers == 0,
        "C and A should leave Halo Pad alone");

  // Its chord waits for a breath, and each breath after a rest strikes it
  // afresh; a dip that doesn't come to rest leaves it sounding.
  CHECK(current_note[ENDPOINT_BREATH] == -1,
        "the chord shouldn't sound before the first breath");
  handle_cc(CC_BREATH, 60);
  CHECK(current_note[ENDPOINT_BREATH] != -1, "a breath didn't strike the chord");
  handle_cc(CC_BREATH, 8);
  CHECK(current_note[ENDPOINT_BREATH] != -1,
        "a dip short of rest shouldn't let the chord go");
  handle_cc(CC_BREATH, 0);
  CHECK(current_note[ENDPOINT_BREATH] == -1,
        "coming to rest didn't let the chord go");
  update_bass(/*force_refresh=*/true);
  CHECK(current_note[ENDPOINT_BREATH] == -1,
        "nothing but a breath should strike it again");
  handle_cc(CC_BREATH, 60);
  CHECK(current_note[ENDPOINT_BREATH] != -1,
        "the next breath didn't strike it again");

  // Its own voices are layers, over the pad: each on and off by itself.
  // They're the home row, the blue keys, apart from the pads below.
  const char* caps[] = {"S", "D", "F", "G", "H"};
  unsigned fxs[] = {BREATH_FX_GUIRA, BREATH_FX_GUIRO, BREATH_FX_WASHBOARD,
                    BREATH_FX_RISER, BREATH_FX_WOBBLE};
  const char* labels[] = {"Guira", "Guiro", "Wash\nboard", "Noise\nRiser",
                          "Wobble"};
  for (int i = 0; i < 5; i++) {
    const Key* k = key_for_cap(caps[i]);
    CHECK(!drone_key_is_dead(k), "%s should be alive on the Breath Gate",
          caps[i]);
    CHECK(strcmp(key_current_label(k), labels[i]) == 0,
          "%s should show %s on the Breath Gate", caps[i], labels[i]);
    press(caps[i]);
    CHECK((told_fx & fxs[i]) && lit(caps[i]), "%s didn't switch on", caps[i]);
    CHECK(c->voices[ENDPOINT_BREATH] == 94 && lit("C") &&
          current_note[ENDPOINT_BREATH] != -1,
          "%s shouldn't have stopped the pad", caps[i]);
    press(caps[i]);
    CHECK(!(told_fx & fxs[i]) && !lit(caps[i]),
          "%s again didn't switch it off", caps[i]);
  }

  press("G");
  press("H");
  CHECK(told_fx == (BREATH_FX_RISER | BREATH_FX_WOBBLE) &&
        lit("G") && lit("H") && lit("C") &&
        c->breath_layers == (BREATH_LAYER_RISER | BREATH_LAYER_WOBBLE),
        "the riser and wobble should both be on, over the pad");

  // The pad's key again lets go of the pad, leaving the layers.
  press("C");
  CHECK(c->voices[ENDPOINT_BREATH] == VOICE_BREATH_NO_PAD && !lit("C") &&
        current_note[ENDPOINT_BREATH] == -1 &&
        told_fx == (BREATH_FX_RISER | BREATH_FX_WOBBLE),
        "C again should let go of the pad and only the pad");
  update_bass(/*force_refresh=*/true);
  CHECK(current_note[ENDPOINT_BREATH] == -1,
        "a chord change shouldn't play notes with no pad");
  press("`");
  CHECK(told_fx == 0, "switching it off didn't stop its layers");
  press("`");
  CHECK(told_fx == (BREATH_FX_RISER | BREATH_FX_WOBBLE),
        "switching it back on didn't");

  // A pad again, and the layers stay.
  press("X");
  CHECK(c->voices[ENDPOINT_BREATH] == 90 && lit("X") && lit("G") &&
        current_note[ENDPOINT_BREATH] != -1,
        "X should bring a pad back under the layers");
  handle_cc(CC_BREATH, 0);

  // The bottom row is its pads, the orange keys: seven of the drones', with
  // Polysynth, Halo and Sweep where they are on every drone.
  const char* pads[] = {"Z", "X", "C", "V", "B", "N", "M"};
  const int programs[] = {50, 90, 94, 95, 54, 62, 18};
  for (int i = 0; i < 7; i++) {
    const Key* k = key_for_cap(pads[i]);
    int v = drone_voice_on_key(k);
    CHECK(breath_voice_for_note(k->note) < 0 && v >= 0 &&
          DRONE_VOICES[v].program == programs[i] && !drone_key_is_dead(k),
          "%s should pick pad %d on the Breath Gate", pads[i], programs[i]);
  }
  press("M");
  CHECK(c->voices[ENDPOINT_BREATH] == 18 && lit("M"),
        "M should pick Rock Organ on the Breath Gate");

  // The other drones still leave B, N and M empty, and keep all ten pads.
  select_ep("9");
  CHECK(drone_key_is_dead(key_for_cap("B")), "B should be empty on Dc2");
  CHECK(strcmp(key_current_label(key_for_cap("A")), "Church\nOrgan") == 0,
        "A should still be a pad on Dc2");
  breath_hook = NULL;
  full_reset();
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
  fluid_settings_setint(fl_settings, "synth.midi-channels", 32);
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

  // The kick has a channel of its own, for Kick Duck, which has to follow
  // the drum channel's kit.
  CHECK(CHANNEL_KICK != CHANNEL_DRUM && CHANNEL_KICK >= N_ENDPOINTS &&
        CHANNEL_KICK != CHANNEL_PITCHED_KICK,
        "the kick's channel should be a spare one of its own");
  fluid_synth_get_program(fl_synth, CHANNEL_KICK, &sfont, &bank, &program);
  CHECK(bank == PERCUSSION_BANK && program == KITS[KIT_RIM].program,
        "the kick's channel is on %d-%d, not the drum channel's kit",
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
// A note held into the vocoder keeps sounding: the gate that keeps the room
// out mustn't take a long note for the room.  It used to, after about five
// seconds.  And once the note stops, the room alone doesn't drone the chord,
// even a room that's got louder -- once it's been that loud long enough.
static double vocoder_rms(double hz, double level, double seconds) {
  double chord_hz[VOCODER_VOICES], chord_weight[VOCODER_VOICES];
  int n = vocoder_chord(chord_hz, chord_weight);
  static double phase;
  static uint32_t r = 7;
  double sum = 0;
  int frames = (int)(48000 * seconds), tail = 4800;
  for (int i = 0; i < frames; i++) {
    phase += hz / 48000;
    if (phase >= 1) phase -= 1;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    double room = 0.001 * ((double)r / 2147483648.0 - 1);
    float in = (float)(level * sin(2 * M_PI * phase) + room);
    float out = vocoder_process(&vocoder, in, chord_hz, chord_weight, n, 1);
    if (i >= frames - tail) sum += (double)out * out;
  }
  return sqrt(sum / tail);
}

// Each vocal effect makes a sound of the voice, and none of the room: the
// gate in front of them keeps the band in the microphone out of the PA.
static void test_voice_fx() {
  // Robot rings with the whole chord, the third once it's known.
  VfxBlock chord;
  atomic_store(&audio_chord_root, 26);  // D
  atomic_store(&audio_chord_third, 3);  // minor
  atomic_store(&audio_chord_fifth, 7);
  vfx_block(&chord);
  CHECK(fabs(chord.carrier_hz[0] - midi_hz(50)) < 1e-6 &&
        fabs(chord.carrier_hz[1] - midi_hz(53)) < 1e-6 &&
        chord.carrier_weight[1] > 0 &&
        fabs(chord.carrier_hz[2] - midi_hz(57)) < 1e-6,
        "Robot should ring D, F and A for D minor");
  atomic_store(&audio_chord_third, 0);
  vfx_block(&chord);
  CHECK(chord.carrier_weight[1] == 0, "and no third before it's known");

  // Voice Bass goes an octave under the voice, whatever the rig is playing:
  // a voice at 150Hz comes out at 75Hz, over a G bass or any other.
  atomic_store(&audio_bass_note, 31);   // G, which it should pay no mind
  vfx_prepare(48000);
  vfx_block(&chord);
  static float sung[48000];
  double phase = 0;
  for (int i = 0; i < 48000; i++) {
    phase += 150.0 / 48000;
    if (phase >= 1) phase -= 1;
    double v = 0;
    for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
    if (i == 24000) atomic_store(&audio_bass_note, 28);  // E, midway
    sung[i] = vfx_process(VFX_BASS, (float)(0.1 * v), &chord);
  }
  CHECK(fabs(vfx.pitch_hz - 150) < 3,
        "Voice Bass heard %.1fHz for a voice at 150Hz", vfx.pitch_hz);
  CHECK(fabs(vfx.sub_hz - 75) < 2, "Voice Bass's sub is at %.1fHz, not 75",
        vfx.sub_hz);
  // And what comes out is at 75Hz: the lag its last half second best
  // matches itself at, over a bass's range.
  int best_lag = 0;
  double best = -1;
  for (int lag = 48000 / 200; lag <= 48000 / 50; lag++) {
    double xy = 0, xx = 0, yy = 0;
    for (int i = 24000; i < 48000 - lag; i++) {
      xy += (double)sung[i] * sung[i + lag];
      xx += (double)sung[i] * sung[i];
      yy += (double)sung[i + lag] * sung[i + lag];
    }
    double r = xy / sqrt(xx * yy + 1e-20);
    if (r > best) {
      best = r;
      best_lag = lag;
    }
  }
  CHECK(fabs(48000.0 / best_lag - 75) < 2,
        "Voice Bass came out at %.0fHz, not 75", 48000.0 / best_lag);
  // Without clicks, as the voice's pitch moves and it stops and starts: no
  // sample jumps further from the last than a smooth signal at that level
  // could.  Grains that changed length partway through jumped ten times that.
  vfx_prepare(48000);
  vfx_block(&chord);
  double worst = 0, prev = 0;
  phase = 0;
  for (int i = 0; i < 48000 * 6; i++) {
    double t = i / 48000.0, ft = fmod(t, 1.0);
    double on = fmin(1, fmin(ft / 0.01, fmax(0, (0.8 - ft) / 0.01)));
    phase += (165 + 35 * sin(2 * M_PI * t / 3)) / 48000;
    if (phase >= 1) phase -= 1;
    double v = 0;
    for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
    float y = vfx_process(VFX_BASS, (float)(on * 0.1 * v + 0.0005 * sin(i)),
                          &chord);
    if (i > 48000 && fabs(y - prev) > worst) worst = fabs(y - prev);
    prev = y;
  }
  CHECK(worst < 0.02, "Voice Bass clicks: a %.3f jump in one sample", worst);
  atomic_store(&audio_bass_note, 26);

  // Saw Bass goes an octave under the voice too.
  vfx_prepare(48000);
  phase = 0;
  for (int i = 0; i < 48000; i++) {
    phase += 150.0 / 48000;
    if (phase >= 1) phase -= 1;
    double v = 0;
    for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
    sung[i] = vfx_process(VFX_SAW, (float)(0.1 * v), &chord);
  }
  CHECK(fabs(vfx.sub_hz - 75) < 2, "Saw Bass is at %.1fHz, not 75",
        vfx.sub_hz);
  double saw_rms = 0;
  for (int i = 24000; i < 48000; i++) saw_rms += (double)sung[i] * sung[i];
  CHECK(sqrt(saw_rms / 24000) > 0.01, "Saw Bass is silent");

  // Wah goes an octave under a voice at 150Hz too: the output's best
  // self-match over a bass's range is at 75Hz.
  int more[] = {VFX_WAH};
  for (int m = 0; m < 1; m++) {
    vfx_prepare(48000);
    phase = 0;
    for (int i = 0; i < 48000; i++) {
      phase += 150.0 / 48000;
      if (phase >= 1) phase -= 1;
      double v = 0;
      for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
      sung[i] = vfx_process(more[m], (float)(0.1 * v), &chord);
    }
    double want = 75;
    int lag = 0;
    double best_r = -1;
    for (int l = 48000 / 200; l <= 48000 / 30; l++) {
      double xy = 0, xx = 0, yy = 0;
      for (int i = 24000; i < 48000 - l; i++) {
        xy += (double)sung[i] * sung[i + l];
        xx += (double)sung[i] * sung[i];
        yy += (double)sung[i + l] * sung[i + l];
      }
      double r = xy / sqrt(xx * yy + 1e-20);
      if (r > best_r + 1e-3) {  // the first of equals: the fundamental
        best_r = r;
        lag = l;
      }
    }
    CHECK(fabs(48000.0 / lag - want) < 2, "%s came out at %.1fHz, not %.1f",
          WHISTLE_FX[more[m]].name, 48000.0 / lag, want);
  }

  // Both basses at once share the one pitch, listened for once a sample, and
  // both sound: the way whistle_mix runs them side by side.
  vfx_prepare(48000);
  phase = 0;
  double both[2] = {0, 0};
  for (int i = 0; i < 48000; i++) {
    phase += 150.0 / 48000;
    if (phase >= 1) phase -= 1;
    double v = 0;
    for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
    VfxInput in = vfx_input((float)(0.1 * v), true);
    float a = vfx_effect(VFX_BASS, in, &chord);
    float s2 = vfx_effect(VFX_SAW, in, &chord);
    if (i >= 24000) {
      both[0] += (double)a * a;
      both[1] += (double)s2 * s2;
    }
  }
  CHECK(fabs(vfx.sub_hz - 75) < 2 && fabs(vfx.pitch_hz - 150) < 3,
        "both basses together are at %.1fHz, not 75", vfx.sub_hz);
  CHECK(both[0] > 0 && both[1] > 0, "both basses together should sound");

  for (int fx = VFX_ROBOT; fx < N_VFX; fx++) {
    vfx_prepare(48000);
    VfxBlock b;
    vfx_block(&b);
    static uint32_t r = 3;
    double voice = 0, room = 0, phase = 0;
    for (int i = 0; i < 48000 * 4; i++) {
      r ^= r << 13;
      r ^= r >> 17;
      r ^= r << 5;
      double hiss = 0.001 * ((double)r / 2147483648.0 - 1);
      // Two seconds of room, then a second of a voice at 150Hz, then the
      // room again, for as long as a tail might ring.
      bool talking = i >= 48000 * 2 && i < 48000 * 3;
      phase += 150.0 / 48000;
      if (phase >= 1) phase -= 1;
      double v = 0;
      for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
      float in = (float)(hiss + (talking ? 0.1 * v : 0));
      float out = vfx_process(fx, in, &b);
      double e = (double)out * out;
      if (talking) voice += e;
      else if (i < 48000 * 2) room += e;
    }
    voice = sqrt(voice / 48000);
    room = sqrt(room / (48000 * 2));
    CHECK(voice > 0.005, "%s is %.4f for a voice", WHISTLE_FX[fx].name, voice);
    CHECK(room < voice * 0.01, "%s lets the room through: %.4f",
          WHISTLE_FX[fx].name, room);
  }
}

// The whistle guard keeps whistled notes out of the vocal effects and lets
// a voice through, going by the whistle's own pitch detector: a whistle at
// 1kHz and a vowel at 150Hz, each a second long, after the room.
static double guarded_share(bool whistle) {
  static struct Engine e;
  engine_init(&e, 48000);
  pitch_set_gate(&e.detector, (float)whistle_gate_margin(5));
  WhistleGuard g;
  whistle_guard_init(&g, 48000);
  static uint32_t r = 9;
  double phase = 0, in_energy = 0, out_energy = 0;
  for (int i = 0; i < 48000 * 2; i++) {
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    double x = 0.0005 * ((double)r / 2147483648.0 - 1);
    if (i >= 48000) {
      double t = (i - 48000) / 48000.0;
      double env = fmin(1, t / 0.01) * fmin(1, (1 - t) / 0.01);
      phase += (whistle ? 1000.0 : 150.0) / 48000;
      if (phase >= 1) phase -= 1;
      double v = 0;
      if (whistle) {
        v = sin(2 * M_PI * phase);
      } else {
        for (int h = 1; h <= 20; h++) v += sin(2 * M_PI * h * phase) / (h * h);
      }
      x += 0.1 * env * v;
    }
    float l, rr;
    engine_process_stereo(&e, (float)x, &l, &rr);
    float y = whistle_guard_run(&g, (float)x, e.detector.hint.voiced ||
      e.detector.hint.confidence > WHISTLE_GUARD_CONFIDENCE);
    if (i >= 48000) {
      in_energy += x * x;
      out_energy += (double)y * y;
    }
  }
  return out_energy / in_energy;
}

static void test_whistle_guard() {
  double whistled = guarded_share(true), spoken = guarded_share(false);
  CHECK(whistled < 0.05, "%.0f%% of a whistle got through to the effects",
        100 * whistled);
  CHECK(spoken > 0.9, "only %.0f%% of a voice got through to the effects",
        100 * spoken);
}

// The effects' own gate: a voice under it doesn't open them, however quiet
// the room, and one over it does.
static void test_fx_gate() {
  for (int db = -20; db >= -40; db -= 20) {
    room_gate_threshold = pow(10, db / 20.0);
    RoomGate g;
    room_gate_init(&g, 48000);
    double phase = 0, open = 0;
    for (int i = 0; i < 48000; i++) {
      phase += 300.0 / 48000;
      if (phase >= 1) phase -= 1;
      float in = (float)(i < 24000 ? 0 : 0.03 * sin(2 * M_PI * phase));
      open = room_gate_run(&g, in);
    }
    CHECK(db == -20 ? open == 0 : open == 1,
          "a -30dB voice against a %ddB gate left it %.2f open", db, open);
  }
  room_gate_threshold = pow(10, VFX_GATE_DEFAULT_DB / 20.0);

  // The menu's None switches them all off.
  whistle_choose_fx(VFX_BIT(VFX_ROBOT) | VFX_BIT(VFX_BASS));
  CHECK(atomic_load(&whistle_pub_fx) ==
          (VFX_BIT(VFX_ROBOT) | VFX_BIT(VFX_BASS)),
        "two effects didn't reach the audio");
  whistle_choose_fx(0);
  CHECK(atomic_load(&whistle_pub_fx) == 0, "None didn't switch them off");
  whistle_fx_gate_db = -30;
  whistle_publish();
  CHECK(atomic_load(&whistle_pub_fx_gate_db) == -30,
        "the gate didn't reach the audio");
  whistle_fx_gate_db = VFX_GATE_DEFAULT_DB;
  whistle_publish();
}

static void test_vocoder_holds() {
  vocoder_prepare(&vocoder, 48000);
  vocoder_rms(0, 0, 3);
  double early = vocoder_rms(1000, 0.05, 1);
  CHECK(early > 0.01, "the vocoder should sound for a note (%.4f)", early);
  double late = vocoder_rms(1000, 0.05, 14);
  CHECK(late > early * 0.5,
        "a note held 15s into the vocoder faded from %.4f to %.4f", early,
        late);
  double room = vocoder_rms(0, 0, 5);
  CHECK(room < early * 0.05, "the room alone droned the vocoder (%.4f)", room);
  vocoder_rms(1000, 0.01, 25);
  double louder = vocoder_rms(1000, 0.01, 1);
  CHECK(louder < early * 0.05,
        "a room that got louder still droned it after 25s (%.4f)", louder);
}

// The mandolin: on from the start, left option on and off and shift-left
// option to select it, its voices on the keys, and its sound through.
static void test_mandolin() {
  full_reset();
  whistle_reset();
  mando_reset();

  const Key* opt = key_for_cap("opt");
  CHECK(opt != NULL && opt->lit == LIT_MANDO_ON && opt->vk == kVK_Option,
        "left option should be the mandolin");
  CHECK(mando_on && lit("opt"), "the mandolin should start on");

  strike("opt", false);
  CHECK(!mando_on && !lit("opt"), "option didn't switch the mandolin off");
  CHECK(mando_selected && key_is_selected_endpoint(opt),
        "toggling should select, as it does the whistle");
  strike("opt", false);
  CHECK(mando_on, "option didn't switch it back on");

  // Selecting the whistle or an endpoint takes the keys back, and selecting
  // the mandolin takes them from the whistle.
  strike("1", true);
  CHECK(whistle_selected && !mando_selected,
        "shift-1 should select the whistle instead");
  strike("opt", true);
  CHECK(mando_selected && !whistle_selected && mando_on,
        "shift-option should select the mandolin without switching it");
  CHECK(!key_is_selected_endpoint(key_for_cap("1")), "nor show the whistle");

  // Its voices, each on and off, and nothing an endpoint's.
  select_ep("R");
  strike("opt", true);
  int flex_voice = c->voices[ENDPOINT_FLEX];
  for (int v = 0; v < N_MANDO_VOICES; v++) {
    char cap[2] = {(char)MANDO_VOICES[v].note, 0};
    const Key* key = key_for_cap_note(MANDO_VOICES[v].note);
    CHECK(key != NULL && !key_is_dead(key), "%s should be live",
          MANDO_VOICES[v].name);
    CHECK(key && strcmp(key_current_label(key), MANDO_VOICES[v].label) == 0,
          "%s's key should say so", MANDO_VOICES[v].name);
    strike(key ? key->cap : cap, false);
    CHECK(mando_voices == MANDO_BIT(v) && key && key_is_lit(key),
          "%s's key didn't switch it on", MANDO_VOICES[v].name);
    strike(key ? key->cap : cap, false);
    CHECK(mando_voices == 0, "%s's key didn't switch it off",
          MANDO_VOICES[v].name);
  }
  CHECK(c->voices[ENDPOINT_FLEX] == flex_voice,
        "a mandolin voice key changed an endpoint's voice");
  CHECK(key_is_dead(key_for_cap("K")) && key_is_dead(key_for_cap(";")) &&
        key_is_dead(key_for_cap("]")), "keys it doesn't use should be dead");
  bool flex_downbeat = c->downbeat[ENDPOINT_FLEX];
  strike("J", false);
  strike("J", false);
  strike("K", false);
  CHECK(c->downbeat[ENDPOINT_FLEX] == flex_downbeat,
        "the row keys should be the mandolin's, not the endpoint's");
  // Its voices are always on the left, so F2 has nothing to do.
  bool flex_pan = c->pans[ENDPOINT_FLEX];
  CHECK(key_is_dead(key_for_cap("F2")), "F2 should be dead");
  strike("F2", false);
  CHECK(c->pans[ENDPOINT_FLEX] == flex_pan, "F2 shouldn't swap an endpoint");
  select_ep("R");
  strike("R", true);
  CHECK(!mando_selected, "shift over an endpoint should take the keys back");

  // esc: back to on, with nothing over it.
  mando_on = false;
  mando_voices = MANDO_BIT(MANDO_BOOST);
  strike("esc", false);
  CHECK(mando_on && !mando_voices && !mando_selected,
        "esc should leave the mandolin on and plain");
}

// A block of the mandolin through, from `in`, `n` samples at 48kHz; the
// last one's output.
static float mando_left_peak;  // what mando_run's last block put on the left

static float mando_run(double hz, double amp, int n, bool harmonics) {
  static float in[480];
  float last = 0;
  static double phase;
  for (int done = 0; done < n; done += 480) {
    for (int i = 0; i < 480; i++) {
      phase += hz / 48000;
      double x = sin(2 * M_PI * phase);
      if (harmonics) {
        x += 0.5 * sin(4 * M_PI * phase) + 0.3 * sin(6 * M_PI * phase);
      }
      in[i] = (float)(amp * x);
    }
    mando_process(in, 480, 48000);
    last = mando_block[479] + mando_voice_block[479];
    float left[480] = {0}, right[480] = {0};
    float* out[2] = {left, right};
    mando_add(out, 2, 480);
    mando_left_peak = 0;
    for (int i = 0; i < 480; i++) {
      mando_left_peak = fmaxf(mando_left_peak, fabsf(left[i]));
    }
    float gain = atomic_load(&mando_gain);  // mando_add's, which it scales by
    // Less whatever the limiter took off the last sample.
    double to[2];
    for (int side = 0; side < 2; side++) {
      double env = mando_limiter[side].env;
      to[side] = gain * (env > MANDO_LIMIT ? MANDO_LIMIT / env : 1);
    }
    CHECK(fabs(right[479] - to[1] * mando_block[479]) < 1e-6 &&
          fabs(left[479] - to[0] * mando_voice_block[479]) < 1e-6,
          "the mandolin on the right and its voices on the left");
  }
  return last;
}

static void test_mandolin_sound() {
  mando_reset();
  mando_prepare(48000);

  // Straight through, then louder, then muted for the tuner, then off.
  float in = 0;
  mando_run(440, 0.1, 4800, false);
  for (int i = 0; i < 480; i++) {
    CHECK(fabsf(mando_block[i]) <= 0.1001f, "straight through went over");
  }
  double peak = 0;
  for (int i = 0; i < 480; i++) peak = fmax(peak, fabs(mando_block[i]));
  CHECK(fabs(peak - 0.1) < 0.002, "straight through peaks at %.4f", peak);
  (void)in;

  mando_voices = MANDO_BIT(MANDO_BOOST);
  mando_publish();
  mando_run(440, 0.1, 4800, false);
  peak = 0;
  for (int i = 0; i < 480; i++) peak = fmax(peak, fabs(mando_block[i]));
  CHECK(fabs(20 * log10(peak / 0.1) - 6) < 0.3, "Boost is %+.1fdB, not +6",
        20 * log10(peak / 0.1));

  // The tuner, to a cent, on each open string and harmonics.
  mando_voices = MANDO_BIT(MANDO_TUNER) | MANDO_BIT(MANDO_BOOST);
  mando_publish();
  for (int s = 0; s < 4; s++) {
    double hz = 440 * pow(2, (MANDO_STRINGS[s] - 69) / 12.0);
    mando_run(hz * pow(2, 7 / 1200.0), 0.05, 9600, s % 2);
    double heard = atomic_load(&mando_meter_hz) / 100.0;
    double cents = 1200 * log2(heard / hz);
    CHECK(fabs(cents - 7) < 1.5, "the tuner heard string %d %+.1f cents off,"
          " not +7", s + 1, cents);
    peak = 0;
    for (int i = 0; i < 480; i++) peak = fmax(peak, fabs(mando_block[i]));
    CHECK(peak < 1e-6, "the tuner should mute the mandolin");
  }
  mando_run(440, 0.0001, 9600, false);
  CHECK(atomic_load(&mando_meter_hz) == 0, "the tuner heard a note in hiss");

  // Bass: the whistle's, reaching down to the mandolin's G.
  mando_voices = MANDO_BIT(MANDO_BASS);
  mando_publish();
  mando_run(196, 0.2, 24000, true);
  const struct PitchHint* h = &mando.engine.detector.hint;
  CHECK(h->voiced && fabs(1200 * log2(h->freq / 196)) < 20,
        "the Bass didn't find a mandolin's G (%s, %.1fHz)",
        h->voiced ? "voiced" : "unvoiced", h->freq);

  // And the rest make a sound over it: all but Breath FX, which is a
  // setting.
  for (int v = MANDO_VOCODER; v < N_MANDO_VOICES; v++) {
    if (v == MANDO_BREATH || v >= MANDO_TALKBOX) continue;
    mando_prepare(48000);
    mando_voices = MANDO_BIT(v);
    mando_publish();
    float dry[480];
    mando_run(330, 0.2, 24000, true);
    memcpy(dry, mando_voice_block, sizeof(dry));
    CHECK(mando_left_peak > 0.05, "%s isn't on the left",
          MANDO_VOICES[v].name);
    mando_on = true;
    mando_voices = 0;
    mando_publish();
    mando_run(330, 0.2, 4800, true);
    double diff = 0;
    for (int i = 0; i < 480; i++) diff = fmax(diff, fabs(dry[i]));
    CHECK(diff > 0.05, "%s made nothing", MANDO_VOICES[v].name);
  }

  // Its own gate: a -30dB mandolin under a -20dB gate plays neither of the
  // voices that make something out of nothing, and over a -40dB one plays
  // them, whatever the whistle's gate says.  The others pass what comes in,
  // gate or no gate.
  int gated[] = {MANDO_VOCODER, MANDO_BASS, MANDO_SYNTH, MANDO_DRONE,
                 MANDO_SHIMMER};
  for (int g = 0; g < 5; g++) {
    int v = gated[g];
    bool gates = v == MANDO_VOCODER || v == MANDO_BASS || v == MANDO_SYNTH;
    for (int db = -20; db >= -40; db -= 20) {
      mando_prepare(48000);
      whistle_fx_gate_db = -60 - db;
      mando_voices = MANDO_BIT(v);
      mando_set_gate(db);
      mando_run(330, 0, 48000, true);
      mando_run(330, 0.03 / 1.8, 24000, true);
      peak = 0;
      for (int i = 0; i < 480; i++) {
        peak = fmax(peak, fabs(mando_voice_block[i]));
      }
      CHECK(db == -20 && gates ? peak < 1e-3 : peak > 1e-3,
            "a -30dB mandolin against a %ddB gate gave %s %.4f", db,
            MANDO_VOICES[v].name, peak);
    }
  }
  whistle_fx_gate_db = VFX_GATE_DEFAULT_DB;
  mando_set_gate(VFX_GATE_DEFAULT_DB);

  // Its volume: all of it, the mandolin and its voices, wherever they go.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_VOCODER);
  mando_publish();
  set_mando_gain(0.5);
  mando_run(330, 0.2, 24000, true);  // checks both sides are halved
  CHECK(mando_left_peak > 0.01, "halved, the voices should still be heard");
  set_mando_gain(1);
  CHECK(atomic_load(&mando_gain) == MANDO_GAIN_UNIT,
        "the slider's middle should be where it was set by ear");

  mando_on = false;
  mando_publish();
  mando_run(440, 0.1, 4800, false);
  peak = 0;
  for (int i = 0; i < 480; i++) peak = fmax(peak, fabs(mando_block[i]));
  CHECK(peak < 1e-6, "switched off, the mandolin should be silent");
  mando_reset();
}

// A strummed mandolin chord, G3 D4 B4 G5, each string a plucked, decaying
// saw-ish tone, struck every `period` samples: or, `scratch`, a muted chop,
// a burst of noise dying in 15ms.  Sample `i`.
static float mando_strum(long i, long period, bool scratch) {
  static const double hz[4] = {196.0, 293.66, 493.88, 783.99};
  long t = i % period;
  double since = (double)t / 48000;
  if (scratch) {
    static uint32_t noise = 12345;
    noise ^= noise << 13;
    noise ^= noise >> 17;
    noise ^= noise << 5;
    return (float)(0.3 * exp(-since / 0.015) *
                   ((double)noise / 2147483648.0 - 1));
  }
  double x = 0;
  for (int s = 0; s < 4; s++) {
    for (int h = 1; h <= 6; h++) {
      x += sin(2 * M_PI * hz[s] * h * since) / (h * 1.5);
    }
  }
  return (float)(0.06 * exp(-since / 0.4) * x);
}

// Run `seconds` of strums through, returning the RMS of the right (the
// mandolin) and of the voices, over the last `measure_s` of it.
static void mando_strums(double seconds, long period, bool scratch,
                         double measure_s, double* dry_rms,
                         double* voice_rms) {
  long total = (long)(seconds * 48000), from = total -
    (long)(measure_s * 48000);
  static long i;
  double dry = 0, voice = 0;
  long n = 0;
  for (long done = 0; done < total; done += 480) {
    float in[480];
    for (int k = 0; k < 480; k++) {
      in[k] = seconds > 0 && period > 0
        ? mando_strum(i++, period, scratch) : 0;
    }
    mando_process(in, 480, 48000);
    if (done < from) continue;
    for (int k = 0; k < 480; k++) {
      dry += mando_block[k] * mando_block[k];
      voice += mando_voice_block[k] * mando_voice_block[k];
      n++;
    }
  }
  if (dry_rms) *dry_rms = sqrt(dry / fmax(1, n));
  if (voice_rms) *voice_rms = sqrt(voice / fmax(1, n));
}

// The signal's level at `hz`, by Goertzel, over the first `n` of `buf`,
// Hann windowed so a tone a little off `hz` still counts.
static double goertzel(const float* buf, int n, double hz) {
  double k = 2 * cos(2 * M_PI * hz / 48000), a = 0, b = 0;
  for (int i = 0; i < n; i++) {
    double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
    double c = w * buf[i] + k * a - b;
    b = a;
    a = c;
  }
  return sqrt(a * a + b * b - k * a * b) / n;
}

// A second of the mandolin's G through the Bass, and the RMS of the voices
// over the last half of it.
static double mando_bass_rms(void) {
  mando_run(196, 0.2, 24000, true);
  double sum = 0;
  for (int b = 0; b < 50; b++) {
    mando_run(196, 0.2, 480, true);
    for (int i = 0; i < 480; i++) {
      sum += mando_voice_block[i] * mando_voice_block[i];
    }
  }
  return sqrt(sum / (50 * 480));
}

static void test_mandolin_effects() {
  mando_reset();
  mando_prepare(48000);
  double plain, voice;
  mando_strums(3, 24000, false, 2, &plain, NULL);

  // Breath FX: no voices with the breath at rest, 300% at full, and the
  // mandolin itself left alone.
  double full, rest, dry_rest;
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRONE);
  mando_publish();
  mando_strums(2, 24000, false, 1, NULL, &full);
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRONE) | MANDO_BIT(MANDO_BREATH);
  mando_publish();
  atomic_store(&audio_breath, 0);
  mando_strums(2, 24000, false, 1, &dry_rest, &rest);
  CHECK(rest / full < 0.01,
        "at rest Breath FX left the voices at %.2f", rest / full);
  CHECK(fabs(dry_rest / plain - 1) < 0.01, "and moved the mandolin");
  mando_prepare(48000);
  atomic_store(&audio_breath, BREATH_FULL);
  mando_strums(2, 24000, false, 1, NULL, &rest);
  CHECK(fabs(rest / full - MANDO_BREATH_HIGH) < 0.03,
        "at full Breath FX left the voices at %.2f", rest / full);

  // A breath controller near rest flickers between two steps: the level
  // should glide between them, not jump from block to block, or Shimmer's
  // tail crackles.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_SHIMMER) | MANDO_BIT(MANDO_BREATH);
  mando_publish();
  double lowest = 10, highest = 0;
  for (int b = 0; b < 200; b++) {
    atomic_store(&audio_breath, BREATH_FLOOR + 1 + b % 2);
    mando_strums(0.01, 12000, false, 0.01, NULL, NULL);
    if (b < 100) continue;
    lowest = fmin(lowest, mando.breath);
    highest = fmax(highest, mando.breath);
  }
  CHECK(highest < 1.3 * lowest,
        "a flickering breath moved the effects from %.3f to %.3f", lowest,
        highest);
  atomic_store(&audio_breath, 0);

  // And Drive: pushed harder the harder it's blown -- dirtier, a sine
  // coming out with more of its harmonics -- and staying about as loud as
  // Drive without it.
  double chain_full, chain_breath;
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRIVE);
  mando_publish();
  mando_strums(2, 24000, false, 1, &chain_full, NULL);
  int pushes[] = {0, (BREATH_FLOOR + BREATH_FULL) / 2, BREATH_FULL};
  double dirt[3];
  for (int b = 0; b < 3; b++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(MANDO_DRIVE) | MANDO_BIT(MANDO_BREATH);
    mando_publish();
    atomic_store(&audio_breath, pushes[b]);
    mando_strums(2, 24000, false, 1, &chain_breath, NULL);
    printf("mandolin: drive %+.1fdB against Drive at a breath of %d\n",
           20 * log10(chain_breath / chain_full), pushes[b]);
    CHECK(fabs(20 * log10(chain_breath / chain_full)) < 3,
          "Breath FX left Drive %+.1fdB at a breath of %d",
          20 * log10(chain_breath / chain_full), pushes[b]);
    static float pushed[48000];
    mando_run(330, 0.03, 9600, false);
    for (int k = 0; k < 100; k++) {
      mando_run(330, 0.03, 480, false);
      memcpy(pushed + 480 * k, mando_block, sizeof(float) * 480);
    }
    dirt[b] = (goertzel(pushed, 48000, 990) + goertzel(pushed, 48000, 1650)) /
              goertzel(pushed, 48000, 330);
  }
  CHECK(dirt[0] < dirt[1] && dirt[1] < dirt[2] && dirt[2] > 5 * dirt[0],
        "Breath FX should push Drive harder the harder it's blown (%.3f %.3f "
        "%.3f)", dirt[0], dirt[1], dirt[2]);
  atomic_store(&audio_breath, 0);

  // And the Bass, a switch: none with no breath, all of it with any.  On a
  // G string, since its pitch tracker follows one note, not a chord.
  double bass_full, bass_breath;
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_BASS);
  mando_publish();
  bass_full = mando_bass_rms();
  int breaths[] = {0, BREATH_FLOOR + 2, BREATH_FULL};
  for (int b = 0; b < 3; b++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(MANDO_BASS) | MANDO_BIT(MANDO_BREATH);
    mando_publish();
    atomic_store(&audio_breath, breaths[b]);
    bass_breath = mando_bass_rms();
    double want = b ? 1 : 0;
    CHECK(fabs(bass_breath / bass_full - want) < 0.02,
          "with a breath of %d Breath FX left the Bass at %.2f",
          breaths[b], bass_breath / bass_full);
  }
  atomic_store(&audio_breath, 0);

  // Boost: on the voices too.
  double boosted;
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRONE) | MANDO_BIT(MANDO_BOOST);
  mando_publish();
  mando_strums(2, 24000, false, 1, NULL, &boosted);
  CHECK(fabs(boosted / full - MANDO_BOOST_GAIN) < 0.02,
        "Boost took the voices %.2fx", boosted / full);

  // The chain: Leslie at the mandolin's level, Drive 6dB over, and each
  // doing what it says.
  static float heard[48000];
  int chain[] = {MANDO_DRIVE, MANDO_LESLIE};
  for (int c = 0; c < 2; c++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(chain[c]);
    mando_publish();
    double through;
    mando_strums(3, 24000, false, 2, &through, &voice);
    printf("mandolin: %s %+.1fdB against the mandolin\n",
           MANDO_VOICES[chain[c]].name, 20 * log10(through / plain));
    double want = chain[c] == MANDO_DRIVE ? 6 : 0;
    CHECK(fabs(20 * log10(through / plain) - want) < 2 && voice == 0,
          "%s is %+.1fdB against the mandolin", MANDO_VOICES[chain[c]].name,
          20 * log10(through / plain));
  }

  // The Talkbox: about 3dB over the mandolin wherever the breath has it, and
  // brighter the more it's blown -- its mouth opening -- whether or not
  // Breath FX is on, which leaves it be.
  int talk_breaths[] = {0, (BREATH_FLOOR + BREATH_FULL) / 2, BREATH_FULL};
  double talk_bright[3];
  for (int fx = 0; fx < 2; fx++) {
    for (int b = 0; b < 3; b++) {
      mando_prepare(48000);
      mando_voices = MANDO_BIT(MANDO_TALKBOX) |
                     (fx ? MANDO_BIT(MANDO_BREATH) : 0);
      mando_publish();
      atomic_store(&audio_breath, talk_breaths[b]);
      double through;
      mando_strums(3, 24000, false, 2, &through, &voice);
      if (!fx) {
        printf("mandolin: talkbox %+.1fdB against the mandolin at %d\n",
               20 * log10(through / plain), talk_breaths[b]);
      }
      CHECK(fabs(20 * log10(through / plain) - 3) < 2.5 && voice == 0,
            "the Talkbox is %+.1fdB against the mandolin at %d%s",
            20 * log10(through / plain), talk_breaths[b],
            fx ? " with Breath FX" : "");
      mando_run(220, 0.1, 9600, true);
      for (int k = 0; k < 100; k++) {
        mando_run(220, 0.1, 480, true);
        memcpy(heard + 480 * k, mando_block, sizeof(float) * 480);
      }
      // An A's third harmonic against its fundamental.
      double bright = goertzel(heard, 48000, 660) /
                      goertzel(heard, 48000, 220);
      if (fx) {
        CHECK(fabs(bright / talk_bright[b] - 1) < 0.01,
              "Breath FX moved the Talkbox at %d", talk_breaths[b]);
      } else {
        talk_bright[b] = bright;
      }
    }
  }
  CHECK(talk_bright[0] < talk_bright[1] && talk_bright[1] < talk_bright[2],
        "the Talkbox should open with the breath (%.3f %.3f %.3f)",
        talk_bright[0], talk_bright[1], talk_bright[2]);
  atomic_store(&audio_breath, 0);

  // The limiter: the Talkbox wide open, Boosted, with the volume all the
  // way up and a loud mandolin, over whatever else is on each side, and
  // neither side goes over it; and a quiet one's left alone.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_TALKBOX) | MANDO_BIT(MANDO_BOOST) |
                 MANDO_BIT(MANDO_DRONE);
  mando_publish();
  set_mando_gain(MAX_MANDO_GAIN);
  atomic_store(&audio_breath, BREATH_FULL);
  float limit_peak = 0, limit_in = 0;
  for (int b = 0; b < 100; b++) {
    float in[480], left[480], right[480];
    for (int k = 0; k < 480; k++) {
      in[k] = mando_strum(b * 480 + k, 12000, false);
      left[k] = right[k] = 0.5f;
    }
    mando_process(in, 480, 48000);
    for (int k = 0; k < 480; k++) {
      limit_in = fmaxf(limit_in, fabsf(0.5f + atomic_load(&mando_gain) *
                                         mando_block[k]));
    }
    float* out[2] = {left, right};
    mando_add(out, 2, 480);
    for (int k = 0; k < 480; k++) {
      limit_peak = fmaxf(limit_peak, fmaxf(fabsf(left[k]), fabsf(right[k])));
    }
  }
  CHECK(limit_in > 1.5 && limit_peak <= MANDO_LIMIT + 1e-6,
        "the limiter let %.2f through of %.2f", limit_peak, limit_in);
  set_mando_gain(1);
  atomic_store(&audio_breath, 0);

  // The Leslie: flat out without Breath FX; with it, the breath its motor,
  // and each rotor taking its time -- the horn up in about a second, the
  // drum in several, and both coasting back down.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_LESLIE);
  mando_publish();
  atomic_store(&audio_breath, 0);
  mando_strums(2, 24000, false, 1, NULL, NULL);
  CHECK(mando.leslie.horn_hz == LESLIE_HORN_HZ &&
        mando.leslie.drum_hz == LESLIE_DRUM_HZ,
        "without Breath FX the Leslie should stay fast");
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_LESLIE) | MANDO_BIT(MANDO_BREATH);
  mando_publish();
  mando_strums(12, 24000, false, 1, &chain_breath, NULL);
  CHECK(fabs(mando.leslie.horn_hz - LESLIE_HORN_SLOW_HZ) < 0.05 &&
        fabs(mando.leslie.drum_hz - LESLIE_DRUM_SLOW_HZ) < 0.05,
        "at rest the Leslie should turn slow (%.2f %.2f)",
        mando.leslie.horn_hz, mando.leslie.drum_hz);
  CHECK(fabs(20 * log10(chain_breath / plain)) < 2,
        "and Breath FX shouldn't fade it out (%+.1fdB)",
        20 * log10(chain_breath / plain));
  atomic_store(&audio_breath, BREATH_FULL);
  mando_strums(1, 24000, false, 1, NULL, NULL);
  double horn_1s = mando.leslie.horn_hz, drum_1s = mando.leslie.drum_hz;
  CHECK(horn_1s > 0.9 * LESLIE_HORN_HZ && drum_1s < 0.6 * LESLIE_DRUM_HZ,
        "a second of full breath should have the horn up and the drum "
        "still coming (%.2f %.2f)", horn_1s, drum_1s);
  mando_strums(6, 24000, false, 1, NULL, NULL);
  CHECK(mando.leslie.horn_hz > 0.99 * LESLIE_HORN_HZ &&
        mando.leslie.drum_hz > 0.95 * LESLIE_DRUM_HZ,
        "blown long enough the Leslie should be fast (%.2f %.2f)",
        mando.leslie.horn_hz, mando.leslie.drum_hz);
  atomic_store(&audio_breath, (BREATH_FLOOR + BREATH_FULL) / 2);
  mando_strums(12, 24000, false, 1, NULL, NULL);
  double half = (LESLIE_HORN_SLOW_HZ + LESLIE_HORN_HZ) / 2;
  CHECK(fabs(mando.leslie.horn_hz - half) < 0.5,
        "half a breath should hold the horn about halfway (%.2f)",
        mando.leslie.horn_hz);
  atomic_store(&audio_breath, 0);
  mando_strums(1, 24000, false, 1, NULL, NULL);
  CHECK(mando.leslie.horn_hz < mando.leslie.drum_hz * LESLIE_HORN_HZ /
                                 LESLIE_DRUM_HZ,
        "letting go, the horn should slow quicker than the drum (%.2f %.2f)",
        mando.leslie.horn_hz, mando.leslie.drum_hz);

  // Drive: a sine comes out with harmonics.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRIVE);
  mando_publish();
  mando_run(330, 0.1, 9600, false);
  for (int b = 0; b < 100; b++) {
    mando_run(330, 0.1, 480, false);
    memcpy(heard + 480 * b, mando_block, sizeof(float) * 480);
  }
  double fundamental = goertzel(heard, 48000, 330);
  double harmonics = goertzel(heard, 48000, 660) +
                     goertzel(heard, 48000, 990);
  CHECK(harmonics > 0.1 * fundamental,
        "Drive left a sine clean (%.3f of harmonics)",
        harmonics / fundamental);

  // Leslie: a steady note comes out swinging in level.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_LESLIE);
  mando_publish();
  mando_run(2000, 0.1, 9600, false);
  double lo = 1, hi = 0;
  for (int b = 0; b < 200; b++) {
    mando_run(2000, 0.1, 480, false);
    double rms = 0;
    for (int i = 0; i < 480; i++) rms += mando_block[i] * mando_block[i];
    rms = sqrt(rms / 480);
    lo = fmin(lo, rms);
    hi = fmax(hi, rms);
  }
  CHECK(hi > 1.5 * lo, "the Leslie didn't swing (%.3f to %.3f)", lo, hi);

  // Synth: the chord, whatever the mandolin plays, near its level.
  atomic_store(&audio_chord_root, 2);  // D major
  atomic_store(&audio_chord_third, 4);
  atomic_store(&audio_chord_fifth, 7);
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_SYNTH);
  mando_publish();
  mando_strums(3, 24000, false, 2, NULL, &voice);
  printf("mandolin: synth %+.1fdB against the mandolin\n",
         20 * log10(voice / plain));
  CHECK(fabs(20 * log10(voice / plain)) < 6,
        "the Synth is %+.1fdB against the mandolin",
        20 * log10(voice / plain));
  mando_run(440, 0.1, 9600, false);
  for (int b = 0; b < 100; b++) {
    mando_run(440, 0.1, 480, false);
    memcpy(heard + 480 * b, mando_voice_block, sizeof(float) * 480);
  }
  double d3 = goertzel(heard, 48000, midi_hz(50));
  double a_note = goertzel(heard, 48000, 440);
  double c3 = goertzel(heard, 48000, midi_hz(48));
  CHECK(d3 > 3 * c3 && d3 > a_note,
        "the Synth should play D, not what the mandolin does (%.4f %.4f "
        "%.4f)", d3, a_note, c3);
  // And only by what's pitched: scratches as loud as the strums play
  // hardly any of it.
  double scratch_in, scratch_out, strum_in, strum_out;
  mando_prepare(48000);
  mando_strums(3, 12000, true, 2, &scratch_in, &scratch_out);
  mando_prepare(48000);
  mando_strums(3, 12000, false, 2, &strum_in, &strum_out);
  CHECK(strum_out / strum_in > 10 * (scratch_out / scratch_in),
        "the Synth took scratches (%.4f) nearly as well as chords (%.4f)",
        scratch_out / scratch_in, strum_out / strum_in);
  atomic_store(&audio_chord_root, 0);
  atomic_store(&audio_chord_third, 0);
  atomic_store(&audio_chord_fifth, 7);

  // Shimmer: rings after pitched strums, hardly at all after scratches of
  // as much level, and rings on after it's switched off, until it dies.
  double scratch_dry, pitched_dry, scratched, pitched;
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_SHIMMER);
  mando_publish();
  mando_strums(3, 12000, true, 2, &scratch_dry, &scratched);
  mando_prepare(48000);
  mando_strums(3, 12000, false, 2, &pitched_dry, &pitched);
  printf("mandolin: shimmer %+.1fdB against the mandolin, scratches "
         "%+.1fdB\n", 20 * log10(pitched / pitched_dry),
         20 * log10(scratched / scratch_dry));
  CHECK(pitched / pitched_dry > 8 * (scratched / scratch_dry),
        "Shimmer took scratches (%.4f) nearly as well as chords (%.4f)",
        scratched / scratch_dry, pitched / pitched_dry);
  CHECK(fabs(20 * log10(pitched / pitched_dry) + 4) < 3,
        "Shimmer is %+.1fdB against the mandolin",
        20 * log10(pitched / pitched_dry));
  mando_voices = 0;
  mando_publish();
  double tail;
  mando_strums(0.5, 0, false, 0.2, NULL, &tail);
  CHECK(tail > pitched * 0.05, "Shimmer's tail should ring on (%.4f)", tail);
  mando_strums(20, 0, false, 1, NULL, &tail);
  CHECK(tail < 1e-4, "Shimmer's tail should die away, not build (%.5f)",
        tail);
  mando_reset();
}

// A sine at `hz` through the mandolin for `n` samples, keeping what came out
// on the right (`chain`) or the left into `got`.
static void mando_tone(double hz, double amp, int n, bool chain, float* got) {
  for (int b = 0; b * 480 < n; b++) {
    mando_run(hz, amp, 480, false);
    if (got) {
      memcpy(got + 480 * b, chain ? mando_block : mando_voice_block,
             sizeof(float) * 480);
    }
  }
}

// The pedals keeping time, a beat every `beat_ms` from `from_ms` on the
// clock the blocks are on, as the music hook would say at the block
// starting `at_ms`.
static void mando_pedals(double at_ms, double from_ms, double beat_ms) {
  audio_block_ns = (uint64_t)(1e9 + at_ms * 1e6);
  double beats = floor((at_ms - from_ms) / beat_ms);
  atomic_store(&audio_beat_start_ns,
               (uint64_t)(1e9 + (from_ms + beats * beat_ms) * 1e6));
  atomic_store(&audio_beat_ns, (uint64_t)(beat_ms * 1e6));
}

// RMS over each of `n` stretches of `got`, each `each` samples.
static void mando_levels(const float* got, int n, int each, double* rms) {
  for (int k = 0; k < n; k++) {
    double sum = 0;
    for (int i = 0; i < each; i++) sum += got[k * each + i] * got[k * each + i];
    rms[k] = sqrt(sum / each);
  }
}

// A sine through the mandolin with the pedals keeping a 500ms beat: how loud
// it came out over each 10ms of the last beat, which started as `rms` does.
static void mando_in_time(double hz, double* rms) {
  static float got[24000];
  // Four seconds, the pedals' beats at 3ms past each half second, and the
  // last beat kept: from 3503ms, blocks 353 to 399.
  for (int b = 0; b < 400; b++) {
    mando_pedals(b * 10, 3, 500);
    mando_run(hz, 0.1, 480, false);
    if (b >= 353) {
      memcpy(got + 480 * (b - 353), mando_block, sizeof(float) * 480);
    }
  }
  mando_levels(got, 47, 480, rms);
}

// The effects that know the chord and the beat.
static void test_mandolin_chord_effects() {
  mando_reset();
  atomic_store(&audio_chord_root, 2);  // D major: D F# A
  atomic_store(&audio_chord_third, 4);
  atomic_store(&audio_chord_fifth, 7);
  static float got[48000];
  double plain, voice;
  mando_prepare(48000);
  mando_strums(3, 24000, false, 2, &plain, NULL);

  // Drone: a note in the chord rings on after it stops; one out of it
  // hardly rings; and on a change of chord the old one's ring dies.
  double ring[2];
  double notes[2] = {midi_hz(69), midi_hz(68)};  // A, in it; G#, not
  for (int k = 0; k < 2; k++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(MANDO_DRONE);
    mando_publish();
    mando_tone(notes[k], 0.1, 24000, false, NULL);
    mando_tone(notes[k], 0, 24000, false, got);
    mando_levels(got + 19200, 1, 4800, &ring[k]);  // 400-500ms after
  }
  CHECK(ring[0] > 0.01 && ring[0] > 20 * ring[1],
        "the Drone should ring on for A in D (%.4f), not G# (%.4f)",
        ring[0], ring[1]);
  mando_prepare(48000);
  mando_tone(notes[0], 0.1, 24000, false, NULL);
  atomic_store(&audio_chord_root, 3);  // E flat
  mando_tone(notes[0], 0, 24000, false, got);
  double changed;
  mando_levels(got + 19200, 1, 4800, &changed);
  CHECK(changed < 0.01 * ring[0],
        "a change should damp the old chord's strings (%.5f of %.4f)",
        changed, ring[0]);
  atomic_store(&audio_chord_root, 2);
  mando_prepare(48000);
  mando_strums(3, 24000, false, 2, NULL, &voice);
  printf("mandolin: drone %+.1fdB against the mandolin\n",
         20 * log10(voice / plain));
  CHECK(fabs(20 * log10(voice / plain)) < 6,
        "the Drone is %+.1fdB against the mandolin",
        20 * log10(voice / plain));

  // The new chain: at the mandolin's level.
  int chain[] = {MANDO_RESONATOR, MANDO_HARM_TREM, MANDO_TREMOLO};
  for (int c = 0; c < 3; c++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(chain[c]);
    mando_publish();
    double through;
    mando_strums(3, 24000, false, 2, &through, &voice);
    printf("mandolin: %s %+.1fdB against the mandolin\n",
           MANDO_VOICES[chain[c]].name, 20 * log10(through / plain));
    CHECK(fabs(20 * log10(through / plain)) < 2 && voice == 0,
          "%s is %+.1fdB against the mandolin", MANDO_VOICES[chain[c]].name,
          20 * log10(through / plain));
  }

  // Resonator: a note in the chord through much louder than one out of it.
  double reso[2];
  for (int k = 0; k < 2; k++) {
    mando_prepare(48000);
    mando_voices = MANDO_BIT(MANDO_RESONATOR);
    mando_publish();
    mando_tone(notes[k], 0.1, 24000, true, NULL);
    mando_tone(notes[k], 0.1, 4800, true, got);
    mando_levels(got, 1, 4800, &reso[k]);
  }
  CHECK(reso[0] > 2 * reso[1],
        "the Resonator should favour A in D (%.4f) over G# (%.4f)",
        reso[0], reso[1]);

  // Tremolo: in step with the pedals, full on each 8th and down between.
  double rms[47];
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_TREMOLO);
  mando_publish();
  mando_in_time(2000, rms);
  double top = 0;
  for (int k = 0; k < 47; k++) top = fmax(top, rms[k]);
  // 10ms each: the beat at 0, the 8th at 25, the 16ths at 12 and 37.
  CHECK(rms[0] > 0.8 * top && rms[25] > 0.8 * top && rms[12] < 0.4 * top &&
        rms[37] < 0.4 * top,
        "the Tremolo should be full on the 8ths and down between "
        "(%.3f %.3f %.3f %.3f of %.3f)", rms[0], rms[12], rms[25], rms[37],
        top);
  // And with Breath FX at rest, none of it.
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_TREMOLO) | MANDO_BIT(MANDO_BREATH);
  mando_publish();
  atomic_store(&audio_breath, 0);
  mando_in_time(2000, rms);
  double lo = 1, hi = 0;
  for (int k = 0; k < 47; k++) {
    lo = fmin(lo, rms[k]);
    hi = fmax(hi, rms[k]);
  }
  CHECK(hi < 1.05 * lo, "Breath FX at rest should leave no Tremolo "
        "(%.3f to %.3f)", lo, hi);

  // Harm Trem: the lows loudest on the beat and the highs half a beat on.
  double low[47], high[47];
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_HARM_TREM);
  mando_publish();
  mando_in_time(100, low);
  mando_prepare(48000);
  mando_in_time(4000, high);
  CHECK(low[0] > 2 * low[25] && high[25] > 2 * high[0],
        "Harm Trem should swell the lows on the beat (%.3f %.3f) and the "
        "highs off it (%.3f %.3f)", low[0], low[25], high[0], high[25]);

  atomic_store(&audio_beat_start_ns, 0);
  atomic_store(&audio_beat_ns, 0);
  atomic_store(&audio_chord_root, 0);
  atomic_store(&audio_chord_third, 0);
  atomic_store(&audio_chord_fifth, 7);
  mando_reset();
}

// Record Mandolin: the second input as it came, sample for sample, whatever
// the mandolin was doing to it, and a header that says how long it is.
static void test_mandolin_recording() {
  mando_reset();
  mando_prepare(48000);
  mando_voices = MANDO_BIT(MANDO_DRIVE) | MANDO_BIT(MANDO_SHIMMER);
  mando_publish();
  const char* path = "/tmp/jammer-test-mandolin.wav";
  CHECK(mando_rec_start(path, 48000), "couldn't start recording");
  CHECK(mando_rec_running(), "it should say it's recording");
  static float sent[48000];
  for (int b = 0; b < 100; b++) {
    float in[480];
    for (int i = 0; i < 480; i++) {
      in[i] = sent[b * 480 + i] = mando_strum(b * 480 + i, 12000, b % 3);
    }
    mando_process(in, 480, 48000);
  }
  double seconds = mando_rec_stop();
  CHECK(!mando_rec_running(), "and that it's stopped");
  CHECK(fabs(seconds - 1) < 1e-9, "recorded %.3fs of 1", seconds);
  mando_process(sent, 480, 48000);  // after it stopped: not recorded
  long long n = 0;
  double rate = 0;
  float* got = nr_read_wav(path, &n, &rate);
  CHECK(got && n == 48000 && rate == 48000,
        "read back %lld samples at %.0fHz", n, rate);
  if (got && n == 48000) {
    CHECK(memcmp(got, sent, sizeof(sent)) == 0,
          "the recording isn't the input as it came");
  }
  free(got);
  FILE* f = fopen(path, "rb");
  unsigned char h[44] = {0};
  if (f) {
    fread(h, 1, 44, f);
    fclose(f);
  }
  unsigned data = h[40] | h[41] << 8 | h[42] << 16 | (unsigned)h[43] << 24;
  CHECK(data == 48000 * 4, "the header says %u bytes, not %d", data,
        48000 * 4);
  CHECK(atomic_load(&mando_rec_dropped) == 0, "dropped blocks");
  unlink(path);
  mando_reset();
}

static void test_whistle() {
  full_reset();
  whistle_reset();

  const Key* one = key_for_cap("1");
  CHECK(one != NULL && one->lit == LIT_WHISTLE_ON,
        "the 1 key should be the whistle");

  // Every voice key resolves to a preset that exists.
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    CHECK(whistle_engine_voice[i] > 0 ||
          (!WHISTLE_VOICES[i].preset &&
           whistle_engine_voice[i] == WHISTLE_VOICES[i].own) ||
          (WHISTLE_VOICES[i].own == WHISTLE_PASS_THROUGH &&
           whistle_engine_voice[i] == 0),
          "no preset for whistle voice %s", WHISTLE_VOICES[i].label);
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

  // Every voice key is one of the whistle's: B passes the microphone
  // through, as the engine's voice 0, 6dB under the whistle's level.
  CHECK(!whistle_key_is_dead(key_for_cap("N")), "N shouldn't draw as dead");
  CHECK(!whistle_key_is_dead(key_for_cap("D")), "D shouldn't");
  CHECK(!whistle_key_is_dead(key_for_cap("B")), "B shouldn't");
  int before = atomic_load(&whistle_pub_target_gain);
  strike("B", false);
  CHECK(WHISTLE_VOICES[whistle_voice].own == WHISTLE_PASS_THROUGH &&
        lit("B") && !lit("D") && c->voices[ENDPOINT_FLEX] == flex_voice,
        "B should pick Pass Through");
  CHECK(atomic_load(&whistle_pub_voice) == 0,
        "Pass Through should be the engine's raw input");
  CHECK(abs(2 * atomic_load(&whistle_pub_target_gain) - before) <= 1,
        "Pass Through should be 6dB down (%d against %d)",
        atomic_load(&whistle_pub_target_gain), before);
  strike("D", false);
  CHECK(whistle_voice == 2 && atomic_load(&whistle_pub_target_gain) == before,
        "D should bring Reese back at its level");

  // J is the vocoder, which the engine doesn't know about: a layer over
  // the voice rather than a voice, so the voice keeps playing beside it.
  strike("J", false);
  CHECK((atomic_load(&whistle_pub_fx) == VFX_BIT(VFX_VOCODER)) && lit("J") && lit("D") &&
        whistle_voice == 2, "J should layer the vocoder over Reese");
  CHECK(atomic_load(&whistle_pub_vocoder_gain) > 0 &&
        atomic_load(&whistle_pub_target_gain) > 0,
        "the vocoder and the voice should both be heard");
  strike("F", false);
  CHECK((atomic_load(&whistle_pub_fx) == VFX_BIT(VFX_VOCODER)) && lit("J") && lit("F"),
        "a voice key shouldn't stop the vocoder");
  // The lit voice again silences it, for the vocoder alone; any voice key
  // brings one back, and switching the vocoder off leaves it silenced.
  strike("F", false);
  CHECK(atomic_load(&whistle_pub_target_gain) == 0 && !lit("F") &&
        atomic_load(&whistle_pub_vocoder_gain) > 0,
        "F again should leave the vocoder on its own");
  strike("F", false);
  CHECK(atomic_load(&whistle_pub_target_gain) > 0 && lit("F"),
        "F a third time should bring the voice back");
  strike("F", false);
  strike("D", false);
  CHECK(atomic_load(&whistle_pub_target_gain) > 0 && lit("D") &&
        whistle_voice == 2, "another voice key should bring a voice back");
  strike("D", false);
  strike("J", false);
  CHECK(atomic_load(&whistle_pub_target_gain) == 0 && !lit("D") &&
        !lit("J") && atomic_load(&whistle_pub_fx) == 0,
        "whistle, vocoder, voice, vocoder: nothing should be left playing");
  strike("D", false);
  CHECK(atomic_load(&whistle_pub_target_gain) > 0 && lit("D"),
        "a voice key should bring the voice back");
  strike("D", false);
  CHECK(atomic_load(&whistle_pub_target_gain) > 0,
        "with no vocoder, the lit voice again shouldn't silence it");
  strike("J", false);
  strike("F", false);
  // Over a breath voice, the vocoder's all there is to hear.
  strike("N", false);
  CHECK(atomic_load(&whistle_pub_vocoder_gain) > 0 &&
        atomic_load(&whistle_pub_target_gain) == 0 &&
        atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_WHISTLE,
        "Whistle Breath should breathe under the vocoder, silently");
  strike("J", false);
  CHECK(!(atomic_load(&whistle_pub_fx) == VFX_BIT(VFX_VOCODER)) && !lit("J") &&
        atomic_load(&whistle_pub_vocoder_gain) == 0, "J again didn't stop it");
  strike("D", false);

  // N and M are the breath voices: the audio's told which to listen for,
  // and only while the whistle's on.
  strike("N", false);
  CHECK(atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_WHISTLE && lit("N"),
        "N didn't pick Whistle Breath");
  // Which listens with its own gate and full level, not the bass's.
  CHECK(atomic_load(&whistle_pub_gate) == whistle_breath_gate &&
        atomic_load(&whistle_pub_breath_level_full) ==
          whistle_breath_level_full,
        "Whistle Breath should use its own gate and full level");
  CHECK(c->voices[ENDPOINT_FLEX] == flex_voice, "N reached the endpoint");
  // M is Blow Noise, a layer over the voice: it breathes while the bass
  // plays, and M again switches it off.
  strike("D", false);
  strike("M", false);
  CHECK(atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_BLOW && lit("M") &&
        lit("D") && whistle_voice == 2 &&
        atomic_load(&whistle_pub_target_gain) > 0 &&
        strcmp(key_current_label(key_for_cap("M")), "Blow\nNoise") == 0,
        "M should breathe over Reese, with Reese still sounding");
  strike("M", false);
  CHECK(atomic_load(&whistle_pub_breath) == 0 && !lit("M") && lit("D"),
        "M again should switch Blow Noise off");
  strike("M", false);
  // Whistle Breath has the breath controller while it's the voice.
  strike("N", false);
  CHECK(atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_WHISTLE &&
        lit("M"), "Whistle Breath should have the breath under Blow Noise");
  strike("D", false);
  CHECK(atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_BLOW,
        "and give it back to Blow Noise after");
  strike("1", false);
  CHECK(!whistle_on && atomic_load(&whistle_pub_breath) == 0,
        "switching the whistle off should stop it breathing");
  strike("1", false);
  CHECK(atomic_load(&whistle_pub_breath) == WHISTLE_BREATH_BLOW,
        "and switching it back on should start it again");
  strike("M", false);
  CHECK(atomic_load(&whistle_pub_breath) == 0, "M didn't stop the breathing");
  CHECK(atomic_load(&whistle_pub_gate) == whistle_gate,
        "the bass should be back on its own gate");

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

  // The whistle guard keeps whistling out of the effects only while there's
  // a whistle-controlled voice playing.
  CHECK(atomic_load(&whistle_pub_guard) == whistle_on,
        "the guard should follow the whistle being on");
  bool was_on = whistle_on;
  if (!whistle_on) strike("1", false);
  CHECK(atomic_load(&whistle_pub_guard), "a voice playing should be guarded");
  strike("J", false);
  strike("D", false);
  if (whistle_voice_muted) strike("D", false);  // D, sounding, whatever it was
  strike("D", false);  // and D again, silenced under the vocoder
  CHECK(whistle_voice_muted && !atomic_load(&whistle_pub_guard),
        "with the voice silenced, there's nothing to guard");
  strike("D", false);
  CHECK(atomic_load(&whistle_pub_guard), "and with it back, there is");
  strike("J", false);
  strike("1", false);
  CHECK(!atomic_load(&whistle_pub_guard), "nor with the whistle off");
  if (was_on) strike("1", false);

  // The vocoder and the effects beside it: each its key on and off, and any
  // of them together.
  const char* fx_keys[] = {"J", "K", "L", ";", "'"};
  const char* fx_labels[] = {"Vocoder", "Robot", "Voice\nBass", "Saw\nBass",
                             "Wah\nBass"};
  unsigned all = 0;
  for (int i = 0; i < 5; i++) {
    int fx = VFX_VOCODER + i;
    strike(fx_keys[i], false);
    all |= VFX_BIT(fx);
    CHECK(atomic_load(&whistle_pub_fx) == all && lit(fx_keys[i]) &&
          !whistle_key_is_dead(key_for_cap(fx_keys[i])) &&
          strcmp(key_current_label(key_for_cap(fx_keys[i])),
                 fx_labels[i]) == 0,
          "%s should add %s to the rest", fx_keys[i], fx_labels[i]);
  }
  strike("K", false);
  CHECK(atomic_load(&whistle_pub_fx) == (all & ~VFX_BIT(VFX_ROBOT)) &&
        !lit("K") && lit("J") && lit("L"),
        "K again should switch Robot off and leave the rest");
  whistle_choose_fx(0);
  CHECK(whistle_key_is_dead(key_for_cap(",")), ", should be dead");

  // F2 moves the effects to the second input -- when there is one.
  atomic_store(&whistle_input_count, 1);
  CHECK(whistle_key_is_dead(key_for_cap("F2")),
        "F2 should be dead with only one input");
  strike("F2", false);
  CHECK(!whistle_fx_mic, "F2 shouldn't pick an input that isn't there");
  atomic_store(&whistle_input_count, 2);
  CHECK(!whistle_key_is_dead(key_for_cap("F2")) &&
        strcmp(key_current_label(key_for_cap("F2")), "FX MIC\n1") == 0,
        "F2 should offer the effects' input with two");
  strike("F2", false);
  CHECK(whistle_fx_mic && atomic_load(&whistle_pub_fx_mic) && lit("F2") &&
        strcmp(key_current_label(key_for_cap("F2")), "FX MIC\n2") == 0,
        "F2 didn't move the effects to the second input");
  strike("F2", false);
  CHECK(!whistle_fx_mic && !atomic_load(&whistle_pub_fx_mic),
        "F2 again didn't move them back");
  atomic_store(&whistle_input_count, 0);

  // Both inputs come through the ring together, in step.
  whistle_ring_reset();
  float first[4] = {1, 2, 3, 4}, second[4] = {5, 6, 7, 8}, a[4], b[4];
  whistle_push_input(first, second, 4);
  whistle_pop_input(a, b, 4);
  CHECK(a[2] == 3 && b[2] == 7, "the second input didn't come through");
  whistle_push_input(first, NULL, 4);
  whistle_pop_input(a, b, 4);
  CHECK(a[3] == 4 && b[3] == 0, "no second input should read as silence");

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

  // The gate knob sits five steps up from whistle-synth's: its 0, 11.9x the
  // room, is 5 here, and every step is still 2dB.
  CHECK(fabs(whistle_gate_margin(5) - 1.5 * pow(10, 0.9)) < 1e-9,
        "gate 5 should be whistle-synth's 0");
  CHECK(fabs(whistle_gate_margin(9) / whistle_gate_margin(8) -
             pow(10, -0.1)) < 1e-9, "gate steps should be 2dB apart");
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

    // Cleared like the drone it's a second copy of, except that it starts
    // on Warm Pad rather than Rock Organ.
    int e = keys[i].endpoint, o = keys[i].like;
    CHECK(c->chord[e] == c->chord[o] && c->shorter[e] == c->shorter[o],
          "%s should start out like the first one", keys[i].name);
    CHECK(c->voices[o] == 18 && c->voices[e] == 89,
          "%s should start on Warm Pad", keys[i].name);
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

// F3, number recognition: Drum Some with a spoken number choosing the chord
// instead of the feet, and how it gives way to the other ways of choosing.
static void test_speech_picks() {
  full_reset();
  root_note = to_root(26);  // D
  fifth_note = to_root(root_note + 7);
  press("F3");
  CHECK(speech_chooses_notes && drum_chooses_some_notes,
        "F3 didn't switch on speech choosing");
  CHECK(lit("F3") && !lit("F5"), "F3 should light, and F5 shouldn't");
  CHECK(!speech_commands_on && !lit("F8"),
        "F3 shouldn't switch on spoken commands");

  nashville_picks_chord(6);
  CHECK(active_note() == to_root(35) && chord_type == CHORD_MINOR,
        "six should pick the vi, minor");

  // The feet keep time but don't choose.
  handle_feet(MIDI_ON, MIDI_PEDAL_3, 100);
  handle_feet(MIDI_ON, MIDI_PEDAL_4, 100);
  CHECK(active_note() == to_root(35), "a pedal changed the chord");

  // F5 hands the choice back to the feet, leaving Drum Some on.
  press("F5");
  CHECK(!speech_chooses_notes && drum_chooses_some_notes && lit("F5"),
        "F5 should switch to the feet choosing");
  handle_feet(MIDI_ON, MIDI_PEDAL_4, 100);
  CHECK(active_note() == to_root(26 + 5), "the feet didn't take over");
  press("F5");
  CHECK(!drum_chooses_some_notes, "F5 again should switch Drum Some off");

  // F3 off takes Drum Some with it; F9 and escape both end it.
  press("F3");
  press("F3");
  CHECK(!speech_chooses_notes && !drum_chooses_some_notes,
        "F3 off should leave neither on");
  press("F3");
  press("F9");
  CHECK(drum_chooses_notes && !speech_chooses_notes &&
        !drum_chooses_some_notes, "F9 should take over from speech");
  press("F9");
  press("F3");
  press("esc");
  CHECK(!speech_chooses_notes && !drum_chooses_some_notes,
        "escape should end speech choosing");
}

// F8, speech recognition: spoken commands, on and off by themselves, whatever
// is choosing the chord.
static void test_speech_commands() {
  full_reset();
  speech_commands_on = false;  // a reset leaves it alone
  press("F8");
  CHECK(speech_commands_on && lit("F8"), "F8 didn't switch on commands");
  CHECK(!speech_chooses_notes && !drum_chooses_some_notes && !lit("F3"),
        "F8 shouldn't touch how the chord is chosen");
  press("F3");
  CHECK(speech_commands_on && speech_chooses_notes,
        "F3 and F8 should both be on");
  press("F9");
  CHECK(speech_commands_on && !speech_chooses_notes,
        "F9 should take over from F3 and leave F8 alone");
  press("F9");
  press("F3");
  press("F8");
  CHECK(!speech_commands_on && speech_chooses_notes,
        "F8 off should leave F3 on");
  press("F3");
  CHECK(!speech_commands_on && !speech_chooses_notes, "both should be off");

  // A reset ends number recognition but not spoken commands.
  press("F3");
  press("F8");
  press("esc");
  CHECK(!speech_chooses_notes && speech_commands_on,
        "escape should end F3 and leave F8");
  press("F8");
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
  for (int state = 0; state < 6; state++) {
    static const char* STATES[] = {"flex", "drum", "drone", "whistle",
                                   "breath gate", "jaw harp"};
    whistle_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS :
                           state == 4 ? ENDPOINT_BREATH :
                           state == 5 ? ENDPOINT_JAWHARP : ENDPOINT_FLEX;
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
  CHECK(strcmp(PHRASE(true, "press", "banana", "four"), "") == 0,
        "a keyword that names nothing should be dropped, with what followed "
        "it: the four was part of the misheard name, not a chord");
  select_ep("W");  // the foot bass, which has lengths to shorten
  // Not "shortish", heard as "shorter", nor "shorty", which "shorter" was
  // then heard as: nothing like the key beside it.
  CHECK(strcmp(PHRASE(true, "press", "clipped"), "press ;") == 0, "clipped");
  CHECK(strcmp(PHRASE(true, "press", "shorter"), "press '") == 0, "shorter");
  c->selected_endpoint = ENDPOINT_FLEX;
  CHECK(strcmp(PHRASE(true, "press", "bass", "one"), "press S") == 0,
        "'press bass one' is how the recognizer writes 'press synbass 1'");
  CHECK(strcmp(PHRASE(true, "press", "send", "bass", "one"), "press S") == 0,
        "and on the way there, 'press send bass one'");
  CHECK(strcmp(PHRASE(true, "press", "synth", "bass", "two"), "press A") == 0,
        "synth bass 2");
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
  CHECK(strcmp(PHRASE(true, "set", "mode", "to", "minor"), "") == 0,
        "'set' isn't a lead-in: it sounds too much like 'seven'");
  CHECK(strcmp(PHRASE(true, "change", "key", "too", "F"), "key=5") == 0,
        "change key too F");

  // Changing key and mode.
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "A", "now"),
               "key=9") == 0, "change key to A");
  // A key is one word, so it's acted on as soon as it's heard, with nothing
  // to wait for: not "E" for "ef", or "B" for "bee".
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "B"), "key=11") == 0,
        "B, at once");
  CHECK(strcmp(PHRASE(false, "change", "key", "to", "E"), "key=4") == 0,
        "E, at once, not waiting to see if it's 'ef'");
  CHECK(strcmp(PHRASE(false, "change", "key", "two", "D"), "key=2") == 0,
        "D, with 'to' heard as 'two'");
  CHECK(strcmp(PHRASE(true, "change", "key", "to", "see"), "key=0") == 0,
        "a sound-alike letter");
  CHECK(strcmp(PHRASE(false, "change", "key", "to"), "") == 0,
        "no key yet: wait for it");
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
               "") == 0, "a key that names nothing takes what followed with it");
  // And what's said after that, once it's over, is heard as ever.
  {
    int consumed = 0;
    const char* first[] = {"press", "banana"};
    CHECK(next_action(first, 2, &consumed, true).kind == SW_NONE,
          "a misheard press should do nothing");
    const char* then[] = {"press", "banana", "four"};
    CHECK(next_action(then, 3, &consumed, true).kind == SW_NUMBER,
          "a number said after the misheard press was dropped should count");
  }

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
  const char* words[] = {"change", "key", "to", "E"};
  int consumed = 0;
  SwAction a = next_action(words, 4, &consumed, true);
  CHECK(a.kind == SW_KEY, "change key to E");
  change_key(a.value);
  CHECK(root_note == to_root(4) && fifth_note == to_root(11),
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
  for (int state = 0; state < 7; state++) {
    whistle_reset();
    mando_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS :
                           state == 4 ? ENDPOINT_BREATH :
                           state == 5 ? ENDPOINT_JAWHARP : ENDPOINT_FLEX;
    if (state == 3) whistle_selected = true;
    if (state == 6) mando_selected = true;
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

// The fast number recognizer's review: a recording that sounds like another
// word more than its own is found, worst first, unless it's been reviewed;
// and a review's label wins over the prompt when learning.
static void nr_add_word(NrModel* m, int label, float value, int session,
                        bool reviewed) {
  float feat[20][NR_DIM];
  for (int i = 0; i < 20; i++) {
    for (int d = 0; d < NR_DIM; d++) {
      // A little different each frame and each recording, around `value`.
      feat[i][d] = value + 0.05f * (float)((i * 7 + d * 3 + session) % 5);
    }
  }
  nr_model_add(m, label, feat, 20, 0, session, session * 1000,
               session * 1000 + 800, reviewed);
}

static void test_numrec_unusual() {
  NrParams p = nr_default_params(-20);
  NrModel m = {0};
  int session = 0;
  for (int i = 0; i < 4; i++) nr_add_word(&m, 0, 0, session++, false);  // one
  for (int i = 0; i < 4; i++) nr_add_word(&m, 1, 5, session++, false);  // two
  int odd = session;
  nr_add_word(&m, 0, 5, session++, false);  // "one", said like "two"
  nr_model_finish(&m, &p);
  NrUnusual u[8];
  int n = nr_find_unusual(&m, &p, u, 8);
  CHECK(n == 1 && m.t[u[0].index].session == odd && u[0].nearest == 1 &&
        u[0].score > 1, "the 'one' that sounds like 'two' should be found");
  nr_model_free(&m);

  memset(&m, 0, sizeof(m));
  session = 0;
  for (int i = 0; i < 4; i++) nr_add_word(&m, 0, 0, session++, false);
  for (int i = 0; i < 4; i++) nr_add_word(&m, 1, 5, session++, false);
  nr_add_word(&m, 0, 5, session++, true);  // the same, but reviewed
  nr_model_finish(&m, &p);
  CHECK(nr_find_unusual(&m, &p, u, 8) == 0,
        "a reviewed recording shouldn't be asked about again");
  nr_model_free(&m);

  CHECK(nr_review_label("four") == 3 && nr_review_label("other") == NR_OTHER &&
        nr_review_label("drop") == NR_DROP && nr_review_label("x") == -1,
        "review labels");

  // A review wins over the prompt that was up: a burst of noise under the
  // prompt "four", reviewed as "six", is learned as six; reviewed as
  // "drop", not at all.
  double rate = 48000;
  long long len = (long long)(rate * 3);
  float* x = calloc((size_t)len, sizeof(float));
  uint32_t r = 1;
  for (long long i = (long long)(rate * 1.5); i < (long long)(rate * 1.9);
       i++) {
    r = r * 1664525 + 1013904223;
    x[i] = 0.3f * ((float)(r >> 8) / 8388608.0f - 1);
  }
  NrPrompt prompts[] = {{0, "four"}};
  NrParams sp = nr_default_params(-30);
  memset(&m, 0, sizeof(m));
  nr_learn_session(&m, x, len, rate, prompts, 1, NULL, 0, false, &sp, 0);
  CHECK(m.n == 1 && m.t[0].label == 3 && !m.t[0].reviewed,
        "unreviewed, it's what the prompt said");
  long long start = m.t[0].sample;
  nr_model_free(&m);
  NrReview six = {start, 5};
  nr_learn_session(&m, x, len, rate, prompts, 1, &six, 1, false, &sp, 0);
  CHECK(m.n == 1 && m.t[0].label == 5 && m.t[0].reviewed,
        "reviewed as six, it's six");
  nr_model_free(&m);
  NrReview drop = {start + 100, NR_DROP};
  nr_learn_session(&m, x, len, rate, prompts, 1, &drop, 1, false, &sp, 0);
  CHECK(m.n == 0, "reviewed as drop, it's not learned");
  nr_model_free(&m);
  free(x);
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
  CHECK(active_note() == root_note, "a number did something with F3 off");
  press("F3");
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

  press("esc");
}

// ---------------------------------------------------------------------------
// Builds and drops
// ---------------------------------------------------------------------------

#define MAX_TAPPED 1024
static struct { int action, note, velocity, channel; } tapped[MAX_TAPPED];
static int n_tapped;

static void tap_midi(int action, int note, int velocity, int channel) {
  if (n_tapped >= MAX_TAPPED) return;
  tapped[n_tapped].action = action;
  tapped[n_tapped].note = note;
  tapped[n_tapped].velocity = velocity;
  tapped[n_tapped].channel = channel;
  n_tapped++;
}

// How many of this were sent; -1 for any note.
static int count_tapped(int action, int note, int channel) {
  int n = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action == action && tapped[i].channel == channel &&
        (note < 0 || tapped[i].note == note)) {
      n++;
    }
  }
  return n;
}

// VEL is VOICE LEAD on the drones: the notes a chord shares with the last are
// held, and the rest move the shortest way.
// Notes sent on a drone's voice channels, where a voice-led drone plays.
static int count_voices(int action, int note, int endpoint) {
  int n = 0;
  for (int k = 0; k < VOICE_CHANNELS_PER_DRONE; k++) {
    n += count_tapped(action, note, voice_channel_base(endpoint) + k);
  }
  return n;
}

// VEL is VOICE LEAD on the drones: the notes a chord shares with the last are
// held, and the rest move the shortest way, each voice on a channel of its
// own.
static void test_voice_lead() {
  full_reset();
  midi_tap = tap_midi;
  CHECK(strcmp(key_current_label(key_for_cap("del")), "VOICE\nLEAD") == 0,
        "delete should be VOICE LEAD");
  select_ep("O");
  CHECK(key_current_label(key_for_cap(".")) == NULL,
        ". should be dead on a drone, which has no velocity to follow");

  press("del");
  CHECK(voice_lead_on && lit("del") && !lit("."), "delete didn't turn it on");
  press("F5");  // the feet choose the chord
  n_tapped = 0;
  press("O");
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 26 &&
        led_notes[ENDPOINT_DRONE_CHORD][1] == 33,
        "D should come in as it would without voice leading");
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 2 &&
        count_tapped(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0,
        "a voice-led drone's notes should be on its voice channels");

  // To the IV: the D is common, and the A steps down to the G.  On the drone
  // they sound three octaves up -- two for a chord, one for an organ.
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_PEDAL_4, 100);
  CHECK(chord_note == 31, "pedal 4 should be the IV");
  CHECK(count_tapped(MIDI_CC, 123, ENDPOINT_DRONE_CHORD) == 0,
        "voice leading shouldn't stop every note");
  CHECK(count_voices(MIDI_OFF, 26 + 36, ENDPOINT_DRONE_CHORD) == 0 &&
        count_voices(MIDI_ON, 26 + 36, ENDPOINT_DRONE_CHORD) == 0,
        "the common D should be held, not struck again");
  CHECK(count_voices(MIDI_OFF, 33 + 36, ENDPOINT_DRONE_CHORD) == 1 &&
        count_voices(MIDI_ON, 31 + 36, ENDPOINT_DRONE_CHORD) == 1,
        "the A should have stepped down to the G");
  CHECK(count_tapped(MIDI_ON, 31 + 36, voice_channel_base(ENDPOINT_DRONE_CHORD)
                     + 1) == 1, "the G should take the A's channel");

  // Without it, the whole chord is struck afresh.
  press("del");
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_PEDAL_3, 100);
  CHECK(count_tapped(MIDI_CC, 123, ENDPOINT_DRONE_CHORD) >= 1,
        "without voice leading the chord should be let go of");
  press("del");

  // A spoken number: the voice-led drone starts gliding the moment it's
  // heard, arriving on the beat it's due, and when the chord is made there
  // it already has it.
  press("F3");
  CHECK(speech_chooses_notes, "F3 should have speech choosing");
  nashville_picks_chord(1);
  // Struck afresh, so its voices are in a known order.
  drone_endpoint_off(ENDPOINT_DRONE_CHORD);
  update_bass(/*force_refresh=*/true);
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 26 &&
        led_notes[ENDPOINT_DRONE_CHORD][1] == 33, "the I should be D and A");
  // Heard ahead of its beat: nothing moves until the half beat before it,
  // and then the glide ends on the beat, as the chord is made.
  n_tapped = 0;
  uint64_t beat_due = now() + 60 * NS_PER_SEC / 116;
  nashville_leads(5, beat_due);
  advance_lead_schedule();
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 26 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][0] == 0 &&
        led_scheduled_number == 5,
        "the glide shouldn't start until half a beat before the chord");
  update_bass(/*force_refresh=*/true);
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 26 &&
        count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0,
        "until the glide starts the drone is on the old chord");
  led_scheduled_start = now();  // as if its time had come
  advance_lead_schedule();
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 28 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][0] == beat_due &&
        led_scheduled_number == 0,
        "the glide should end on the chord's beat");
  nashville_picks_chord(5);
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0 &&
        led_pending_number == 0, "the chord made there, nothing struck");
  nashville_picks_chord(1);
  drone_endpoint_off(ENDPOINT_DRONE_CHORD);
  update_bass(/*force_refresh=*/true);

  // Glides over half a beat, at 116 BPM with no pedals going, when there's
  // no beat to wait for.
  n_tapped = 0;
  uint64_t heard = now();
  nashville_leads(5, 0);  // from the I's D and A, the D glides up to the E
  uint64_t glide_ns = (60 * NS_PER_SEC / 116) / 2;
  CHECK(chord_note == 26, "hearing it shouldn't make the chord yet");
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0 &&
        count_voices(MIDI_OFF, -1, ENDPOINT_DRONE_CHORD) == 0,
        "gliding voices shouldn't be struck again");
  uint64_t end = led_glide_end[ENDPOINT_DRONE_CHORD][0];
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 28 &&
        led_notes[ENDPOINT_DRONE_CHORD][1] == 33 &&
        end >= heard + glide_ns && end < now() + glide_ns + 1000000 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][1] == 0,
        "the D should be gliding to the E over half a beat, the A staying");

  // Something striking the drones again mid-glide -- a breath, Pulse --
  // mustn't pull it back to the old chord.
  update_bass(/*force_refresh=*/true);
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 28 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][0] == end &&
        count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0,
        "a refresh mid-glide pulled the drone back");

  // The chord made on its beat, mid-glide: nothing struck, and it glides on.
  nashville_picks_chord(5);
  CHECK(chord_note == 33 && led_pending_number == 0,
        "the V should be made on the beat");
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0 &&
        count_voices(MIDI_OFF, -1, ENDPOINT_DRONE_CHORD) == 0 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][0] == end,
        "the drone, on its way, shouldn't be struck again");

  // Made with no warning -- no beat to wait for -- it glides all the same.
  nashville_picks_chord(4);  // the IV: E to D, A to G
  CHECK(led_notes[ENDPOINT_DRONE_CHORD][0] == 26 &&
        led_notes[ENDPOINT_DRONE_CHORD][1] == 31 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][0] > now() &&
        led_glide_end[ENDPOINT_DRONE_CHORD][1] > now() &&
        count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0,
        "a chord made at once should glide too");

  // And the glide gets there.
  led_glide_end[ENDPOINT_DRONE_CHORD][0] = now() + 20 * 1000000ULL;
  led_glide_end[ENDPOINT_DRONE_CHORD][1] = now() + 20 * 1000000ULL;
  advance_glides();
  double early = led_bend[ENDPOINT_DRONE_CHORD][1];
  while (now() < led_glide_end[ENDPOINT_DRONE_CHORD][1] + 1000000) {
    advance_glides();
    usleep(1000);
  }
  advance_glides();
  CHECK(early > -2 && led_bend[ENDPOINT_DRONE_CHORD][1] == -2 &&
        led_glide_end[ENDPOINT_DRONE_CHORD][1] == 0,
        "the A should end two semitones down, at the G: %.2f",
        led_bend[ENDPOINT_DRONE_CHORD][1]);
  midi_tap = NULL;
  full_reset();
}

// Dc on, then VOICE LEAD, then number recognition: the first number glides,
// not just the ones after it.  And it's every drone or none.
static void test_voice_lead_first_number() {
  full_reset();
  midi_tap = tap_midi;
  press("O");                 // the drone chord, sounding, not voice-led
  CHECK(current_note[ENDPOINT_DRONE_CHORD] != -1, "Dc should be sounding");
  n_tapped = 0;
  press("del");               // VOICE LEAD
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 2 &&
        led_notes[ENDPOINT_DRONE_CHORD][0] != -1,
        "switching VOICE LEAD on should move the chord to the voice channels");
  CHECK(led_notes[ENDPOINT_DRONE_CHORD_2][0] == -1,
        "a drone that's off has nothing to move");
  press("F3");
  n_tapped = 0;
  nashville_leads(4, 0);
  CHECK(count_voices(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 0 &&
        (led_glide_end[ENDPOINT_DRONE_CHORD][0] > now() ||
         led_glide_end[ENDPOINT_DRONE_CHORD][1] > now()),
        "the first number should glide");

  // And off again, it's back on its own channel.
  n_tapped = 0;
  press("del");
  CHECK(count_tapped(MIDI_ON, -1, ENDPOINT_DRONE_CHORD) == 2 &&
        led_notes[ENDPOINT_DRONE_CHORD][0] == -1,
        "switching VOICE LEAD off should put the chord back on Dc's channel");
  midi_tap = NULL;
  full_reset();
}

// A voice-led drone's voice channels have to sound like the drone: its
// program, its volume, its fade.  A real synth, since the rest of the tests
// don't have one.
static void test_voice_channels_reach_the_synth() {
  if (access("FluidR3_GM.sf2", R_OK) != 0) {
    printf("no soundfont here, skipping the voice channel check\n");
    return;
  }
  fl_settings = new_fluid_settings();
  fluid_settings_setint(fl_settings, "synth.midi-channels", 32);
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");
  fl_synth = new_fluid_synth(fl_settings);
  fl_sfont_id = fluid_synth_sfload(fl_synth, "FluidR3_GM.sf2", 1);
  if (fl_sfont_id == FLUID_FAILED) return;

  full_reset();
  select_ep("O");
  press("C");  // Halo Pad
  press("del");
  press("-");
  int base = voice_channel_base(ENDPOINT_DRONE_CHORD);
  int sfont, bank, program, drone_program, volume, drone_volume;
  fluid_synth_get_program(fl_synth, ENDPOINT_DRONE_CHORD, &sfont, &bank,
                          &drone_program);
  fluid_synth_get_cc(fl_synth, ENDPOINT_DRONE_CHORD, CC_07, &drone_volume);
  for (int k = 0; k < VOICE_CHANNELS_PER_DRONE; k++) {
    fluid_synth_get_program(fl_synth, base + k, &sfont, &bank, &program);
    fluid_synth_get_cc(fl_synth, base + k, CC_07, &volume);
    CHECK(program == drone_program && drone_program == 94,
          "voice channel %d is on program %d, not the drone's %d", base + k,
          program, drone_program);
    CHECK(volume == drone_volume,
          "voice channel %d is at volume %d, not the drone's %d", base + k,
          volume, drone_volume);
  }

  // And it's heard.
  press("O");
  static float left[4800], right[4800];
  fluid_synth_write_float(fl_synth, 4800, left, 0, 1, right, 0, 1);
  double sum = 0;
  for (int i = 0; i < 4800; i++) sum += left[i] * left[i];
  CHECK(sqrt(sum / 4800) > 1e-4, "a voice-led drone made no sound");

  full_reset();
  delete_fluid_synth(fl_synth);
  delete_fluid_settings(fl_settings);
  fl_synth = NULL;
  fl_settings = NULL;
}

// A drone's II and Q are its trance gate.
static MusicState heard_music;
static void record_music(const MusicState* m) { heard_music = *m; }

static void test_trance_gate() {
  full_reset();
  music_hook = record_music;
  select_ep("O");
  publish_music();
  CHECK(heard_music.trance_gate[ENDPOINT_DRONE_CHORD] == TRANCE_GATE_NONE,
        "no gate to start with");
  press("P");
  publish_music();
  CHECK(heard_music.trance_gate[ENDPOINT_DRONE_CHORD] == TRANCE_GATE_8THS,
        "II should gate in 8ths");
  press("[");
  publish_music();
  CHECK(heard_music.trance_gate[ENDPOINT_DRONE_CHORD] ==
        TRANCE_GATE_SYNCOPATED, "II and Q should gate 1 . 3 4");
  press("P");
  publish_music();
  CHECK(heard_music.trance_gate[ENDPOINT_DRONE_CHORD] == TRANCE_GATE_16THS,
        "Q should gate in 16ths");
  select_ep("W");
  press("P");
  publish_music();
  CHECK(heard_music.trance_gate[ENDPOINT_FOOTBASS] == TRANCE_GATE_NONE,
        "only the drones gate");
  CHECK(heard_music.bass_note == active_note(), "the bass note wasn't told");

  // Straight: the foot bass's grid, 16ths at 0, 18, 35 and 54 of 72.
  const unsigned char* st = heard_music.gate_steps;
  CHECK(heard_music.n_gate_steps == 4 && st[0] == 0 && st[1] == 18 &&
        st[2] == 35 && st[3] == 54,
        "the straight gate isn't on the bass's grid");
  int ns = heard_music.n_gate_steps;
#define GATE(p, at) trance_gate_open(p, (at) / 72.0, st, ns)
  CHECK(GATE(TRANCE_GATE_8THS, 5) && !GATE(TRANCE_GATE_8THS, 20) &&
        GATE(TRANCE_GATE_8THS, 36) && !GATE(TRANCE_GATE_8THS, 60), "8ths");
  CHECK(!GATE(TRANCE_GATE_8THS, 34.5) && GATE(TRANCE_GATE_8THS, 35.5),
        "the gate should open on the upbeat when the foot bass plays it");
  CHECK(GATE(TRANCE_GATE_16THS, 3) && !GATE(TRANCE_GATE_16THS, 15), "16ths");
  CHECK(GATE(TRANCE_GATE_SYNCOPATED, 5) && !GATE(TRANCE_GATE_SYNCOPATED, 20) &&
        GATE(TRANCE_GATE_SYNCOPATED, 38) && GATE(TRANCE_GATE_SYNCOPATED, 57) &&
        !GATE(TRANCE_GATE_SYNCOPATED, 70), "1 . 3 4");
  CHECK(trance_gate_open(TRANCE_GATE_16THS, -1, st, ns),
        "outside a beat the pad should just hold");

  // In jig time, the three 8ths where the foot bass plays them -- 0, 21 and
  // 45, not an even 0, 24, 48 -- and six 16ths and 1 . 3 4 . 6 on those.
  press("0");
  publish_music();
  CHECK(heard_music.jig, "jig time wasn't told");
  CHECK(heard_music.n_gate_steps == 6 && st[0] == 0 && st[2] == 21 &&
        st[4] == 45, "the jig gate isn't on the bass's grid");
  ns = heard_music.n_gate_steps;
  for (int s = 0; s < N_SUBBEATS; s++) {
    bool bass = s == 0 || preup(s) || upbeat(s);
    bool opens = GATE(TRANCE_GATE_8THS, s + 0.5) &&
                 (s == 0 || !GATE(TRANCE_GATE_8THS, s - 0.5));
    CHECK(bass == opens, "jig 8ths: subbeat %d, %s", s,
          bass ? "the bass plays but the gate doesn't open"
               : "the gate opens where the bass doesn't play");
  }
  const bool JIG_SYNC[6] = {true, false, true, true, false, true};
  for (int step = 0; step < 6; step++) {
    CHECK(GATE(TRANCE_GATE_SYNCOPATED, st[step] + 0.5) == JIG_SYNC[step],
          "jig 1 . 3 4 . 6, step %d", step + 1);
  }
#undef GATE
  press("0");

  // Outside the beat the last pedal started is outside any beat.
  atomic_store(&audio_beat_start_ns, 1000);
  atomic_store(&audio_beat_ns, 500);
  CHECK(fabs(audio_beat_phase(1250) - 0.5) < 1e-9 &&
        audio_beat_phase(1600) < 0 && audio_beat_phase(900) < 0,
        "the gate should only run for the beat the pedal started");
  atomic_store(&audio_beat_start_ns, 0);
  atomic_store(&audio_beat_ns, 0);
  music_hook = NULL;
  full_reset();
}

// The breath voices breathe for the breath controller: what the audio hears
// reaches everything the breath drives, a change at a time, and stopping
// leaves nothing breathing.
static void test_whistle_breath_tick() {
  full_reset();
  breath = 0;
  whistle_breath_sent = -1;
  atomic_store(&whistle_breath_cc, 70);
  whistle_breath_tick();
  CHECK(breath == 70, "the breath is %d, not the breath voice's 70", breath);
  CHECK(flex_breath > 0, "the breath voice didn't reach Flex");
  atomic_store(&whistle_breath_cc, 30);
  whistle_breath_tick();
  CHECK(breath == 30, "the breath didn't follow the breath voice down");
  // A real breath controller in between isn't fought over tick after tick.
  handle_cc(CC_BREATH, 90);
  whistle_breath_tick();
  CHECK(breath == 90, "an unchanged breath voice overrode the controller");
  atomic_store(&whistle_breath_cc, -1);
  whistle_breath_tick();
  CHECK(breath == 0, "the breath should come to rest when it stops");
  whistle_breath_tick();
  CHECK(breath == 0, "and stay there");
}

// The detectors behind them, which answer at once.  Blow Noise breathes
// for noise and not for a tone -- a whistle, or a held vowel.  Whistle Breath
// follows the pitch detector's say-so.
static double blow_for(BreathMic* m, int kind, double level,
                       double seconds) {
  static uint32_t r = 1;
  double out = 0, phase = 0;
  for (int i = 0; i < (int)(48000 * seconds); i++) {
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    double noise = (double)r / 2147483648.0 - 1;
    // A vowel at 150Hz: its first twenty harmonics, falling off 12dB an
    // octave the way a voice's do.
    phase += 150.0 / 48000;
    if (phase >= 1) phase -= 1;
    double vowel = 0;
    for (int h = 1; h <= 20; h++) {
      vowel += sin(2 * M_PI * h * phase) / (h * h);
    }
    double x = kind == 0 ? 0 : kind == 1 ? noise * 1.7 : vowel;
    float in = (float)(x * level);
    out = bm_blow_noise(m, in, 0.003, 0.3);
  }
  return out;
}

static void test_breath_mic() {
  BreathMic m;
  bm_init(&m, 48000);
  blow_for(&m, 0, 0, 0.5);
  int soon = bm_cc(blow_for(&m, 1, 0.1, 0.02));
  CHECK(soon > 40, "Blow Noise only reached %d 20ms into blowing", soon);
  int blown = bm_cc(blow_for(&m, 1, 0.1, 0.5));
  CHECK(bm_cc(blow_for(&m, 1, 0.3, 0.3)) > blown,
        "blowing harder should breathe harder");
  CHECK(bm_cc(blow_for(&m, 0, 0, 1.0)) == 0,
        "stopping should stop the breath");
  int vowel = bm_cc(blow_for(&m, 2, 0.3, 1.0));
  CHECK(vowel == 0, "Blow Noise breathes %d for a held vowel", vowel);

  bm_init(&m, 48000);
  double whistled = 0;
  for (int i = 0; i < 4800; i++) whistled = bm_whistle(&m, true, 0.08, 0.1);
  CHECK(bm_cc(whistled) > 90, "a strong whistle only reached %d",
        bm_cc(whistled));
  for (int i = 0; i < 48000; i++) whistled = bm_whistle(&m, false, 0.08, 0.1);
  CHECK(bm_cc(whistled) == 0, "no whistle, no breath");
}

// With the kit on and playing the downbeat, every kick on the pedal plays
// the kit's kick: one on the beat, which starts a beat, and an extra one
// between, which doesn't fit the tempo and so starts none.
static void play_kick_after(double offset_beats) {
  // Four kicks already, a 116 BPM beat apart, ending offset_beats of a beat
  // before now.
  uint64_t beat = 60 * NS_PER_SEC / 116, t = now();
  for (int i = 0; i < KICK_TIMES_LENGTH; i++) kick_times[i] = 0;
  for (int k = 0; k < 4; k++) {
    kick_times[k] = t - (uint64_t)((k + offset_beats) * beat);
  }
  kick_times_index = 4;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
}

static void test_every_kick_plays() {
  full_reset();
  midi_tap = tap_midi;
  c->on[ENDPOINT_DRUM] = true;
  c->downbeat[ENDPOINT_DRUM] = true;
  int kick = KITS[c->drum_voice].kick;
  int channel = KITS[c->drum_voice].kick_program == NO_PITCHED_KICK
    ? CHANNEL_KICK : CHANNEL_PITCHED_KICK;

  n_tapped = 0;
  play_kick_after(1);
  CHECK(current_beat_ns > 0, "a kick on the beat should keep the tempo");
  CHECK(count_tapped(MIDI_ON, kick, channel) == 1,
        "a kick on the beat played %d kicks", count_tapped(MIDI_ON, kick,
                                                           channel));
  n_tapped = 0;
  play_kick_after(0.5);  // an extra, half way between
  CHECK(count_tapped(MIDI_ON, kick, channel) == 1,
        "an extra kick between beats played %d kicks, not one",
        count_tapped(MIDI_ON, kick, channel));

  // Without DOWNBEAT the kit leaves the kick alone, extra or not.
  c->downbeat[ENDPOINT_DRUM] = false;
  n_tapped = 0;
  play_kick_after(0.5);
  CHECK(count_tapped(MIDI_ON, kick, channel) == 0,
        "without DOWNBEAT the pedal shouldn't play the kit's kick");
  midi_tap = NULL;
  full_reset();
}

// Tambourines sent, with velocity in [lo, hi]; and the loudest.
static int tamb_taps(int lo, int hi) {
  int n = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action == MIDI_ON && tapped[i].channel == CHANNEL_DRUM &&
        tapped[i].note == MIDI_DRUM_OUT_TAMBOURINE &&
        tapped[i].velocity >= lo && tapped[i].velocity <= hi) {
      n++;
    }
  }
  return n;
}
static int tamb_loudest(void) {
  int loudest = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].note == MIDI_DRUM_OUT_TAMBOURINE &&
        tapped[i].velocity > loudest) {
      loudest = tapped[i].velocity;
    }
  }
  return loudest;
}
// Hats sent, with velocity in [lo, hi]; and the loudest.
static int hat_taps(int lo, int hi) {
  int n = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action == MIDI_ON && tapped[i].channel == CHANNEL_HAT &&
        tapped[i].note == MIDI_HAT && tapped[i].velocity >= lo &&
        tapped[i].velocity <= hi) {
      n++;
    }
  }
  return n;
}
static int hat_loudest(void) {
  int loudest = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].channel == CHANNEL_HAT && tapped[i].velocity > loudest) {
      loudest = tapped[i].velocity;
    }
  }
  return loudest;
}

// The Grid Hat playing for `ms`, ticked as jammer does.
static void hat_for(int ms) {
  uint64_t until = now() + ms * 1000000ULL;
  while (now() < until) {
    breath_hat_tick();
    usleep(1000);
  }
}

// The jaw harp's voice keys are its own: the four that work kept where they
// were, and the rest what suits it at its pitch, each at its own volume.
static void test_jawharp_voices() {
  full_reset();
  midi_tap = tap_midi;
  select_ep("Q");
  const char* keep[] = {"S", "V", "B", "N"};
  const int keep_programs[] = {38, 67, 81, 87};
  for (int i = 0; i < 4; i++) {
    press(keep[i]);
    CHECK(c->voices[ENDPOINT_JAWHARP] == keep_programs[i] && lit(keep[i]),
          "%s should still be program %d on the jaw harp", keep[i],
          keep_programs[i]);
  }
  press("A");
  CHECK(c->voices[ENDPOINT_JAWHARP] == 84 && lit("A") && !lit("N") &&
        strcmp(key_current_label(key_for_cap("A")), "Charang") == 0,
        "A should be Charang on the jaw harp");
  // At its own volume, not voices.h's default.
  int volume = -1;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action == MIDI_CC && tapped[i].note == CC_07 &&
        tapped[i].channel == ENDPOINT_JAWHARP) {
      volume = tapped[i].velocity;
    }
  }
  CHECK(volume == 109, "Charang should be at volume 109, not %d", volume);
  press("M");
  CHECK(c->voices[ENDPOINT_JAWHARP] == 86, "M should be Fifths Lead");

  // Most sit higher, as if OCT+ had been pressed: SynBass 1 once, which lifts
  // an E1 (28) an octave and leaves a G1 (31) be, and Fifths Lead five times.
  CHECK(endpoint_note(28, ENDPOINT_JAWHARP) == 28 + 24 + 12 &&
        endpoint_note(31, ENDPOINT_JAWHARP) == 31 + 24,
        "Fifths Lead should be five OCT+ presses up, not %d and %d",
        endpoint_note(28, ENDPOINT_JAWHARP), endpoint_note(31,
                                                          ENDPOINT_JAWHARP));
  press("S");
  CHECK(endpoint_note(28, ENDPOINT_JAWHARP) == 40 &&
        endpoint_note(31, ENDPOINT_JAWHARP) == 31,
        "SynBass 1 should be one OCT+ press up");
  // on top of the key's own presses,
  press("]");
  CHECK(endpoint_note(28, ENDPOINT_JAWHARP) == 40 &&
        endpoint_note(31, ENDPOINT_JAWHARP) == 43,
        "OCT+ on SynBass 1 should make two presses in all");
  press("\\");
  // and CHORD takes each up its own way: SynBass 1 an octave, Saw Lead not
  // at all, and Bari Sax two, as it always has.
  c->chord[ENDPOINT_JAWHARP] = true;
  CHECK(endpoint_note(31, ENDPOINT_JAWHARP) == 31 + 12,
        "CHORD should move SynBass 1 up an octave");
  press("B");
  CHECK(endpoint_note(31, ENDPOINT_JAWHARP) == 31,
        "CHORD shouldn't move Saw Lead up");
  press("V");
  CHECK(endpoint_note(31, ENDPOINT_JAWHARP) == 31 + 24,
        "CHORD should still move Bari Sax up, as it did");
  c->chord[ENDPOINT_JAWHARP] = false;

  // D and Z are empty on the jaw harp: they do nothing, and draw dead.
  press("D");
  CHECK(c->voices[ENDPOINT_JAWHARP] == 67 && key_is_dead(key_for_cap("D")) &&
        key_is_dead(key_for_cap("Z")),
        "D and Z should be empty on the jaw harp");

  // Every other endpoint keeps the usual voices.
  select_ep("W");
  press("A");
  CHECK(c->voices[ENDPOINT_FOOTBASS] == 39 &&
        strcmp(key_current_label(key_for_cap("A")), "SynBass\n2") == 0,
        "A should still be SynBass 2 on the foot bass");
  midi_tap = NULL;
  full_reset();
}

// The shakers on the Breath Gate's J and K: the Brushes and the Tamb Shake.
// And the Grid Hat, which was on its L and is now a drum kit, on F.
static void test_tambourines() {
  full_reset();
  midi_tap = tap_midi;
  breath_hook = record_breath;
  press("`");  // starting with the Brushes on
  const char* caps[] = {"J", "K"};
  const char* labels[] = {"Brushes", "Tamb\nShake"};
  for (int i = 0; i < 2; i++) {
    const Key* k = key_for_cap(caps[i]);
    CHECK(!key_is_dead(k) && strcmp(key_current_label(k), labels[i]) == 0,
          "%s should be %s on the Breath Gate", caps[i], labels[i]);
  }
  CHECK(key_is_dead(key_for_cap("L")), "L should be nothing on the Breath "
        "Gate, the Grid Hat being the drum's now");
  const char* spoken[] = {"press", "brushes"};
  CHECK(strcmp(phrase(spoken, 2, true), "press J") == 0, "'press brushes'");

  // J: brushes.  Blowing stirs them, which is the Mac's own sound (see
  // test_brush_swish) and no notes at all, however hard.
  CHECK(lit("J") && (c->breath_layers & BREATH_LAYER_BRUSHES) &&
        (told_fx & BREATH_FX_BRUSH), "the Brushes should start on");
  handle_cc(CC_BREATH, 20);
  n_tapped = 0;
  for (int w = 0; w < 12; w++) {
    handle_cc(CC_BREATH, w % 2 ? 16 : 30);
    usleep(30000);
  }
  handle_cc(CC_BREATH, 90);
  handle_cc(CC_BREATH, 127);
  handle_cc(CC_BREATH, 60);
  handle_cc(CC_BREATH, 127);
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_BRUSH) == 0,
        "blowing, however hard, should stir the brushes, not play a note");
  CHECK(tamb_taps(0, 127) == 0, "the brushes shouldn't play the tambourine");
  handle_cc(CC_BREATH, 0);
  press("J");

  // K: the Tamb Shake, the Mac's own (see test_tamb_shake): no notes.
  press("K");
  CHECK(lit("K") && (told_fx & BREATH_FX_TAMB), "K should switch the Tamb "
        "Shake on");
  n_tapped = 0;
  handle_cc(CC_BREATH, 60);
  hat_for(300);
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_DRUM) == 0 &&
        count_tapped(MIDI_ON, -1, CHANNEL_HAT) == 0,
        "the Tamb Shake shouldn't play any notes");
  press("K");
  CHECK(!(told_fx & BREATH_FX_TAMB), "K again should switch it off");

  // The Grid Hat, the drum's F.  At rest, nothing; blowing, 16ths at 116
  // BPM with no pedals, one every 129ms, soft blowing gently and hard
  // blowing medium; blowing hard, 32nds; three a beat in jig time, and six
  // blowing hard; and it stops with the breath.
  press("`");  // the Breath Gate off: it's the drum's now
  press("L");
  handle_cc(CC_BREATH, 60);
  n_tapped = 0;
  hat_for(300);
  CHECK(hat_taps(0, 127) == 0, "L on the Breath Gate shouldn't play it");
  handle_cc(CC_BREATH, 0);
  press("tab");
  select_ep("tab");
  const Key* f = key_for_cap("F");
  CHECK(strcmp(key_current_label(f), "Grid\nHat") == 0,
        "F should be the Grid Hat on the drum");
  const char* spoken_hat[] = {"press", "grid", "hat"};
  CHECK(strcmp(phrase(spoken_hat, 3, true), "press F") == 0,
        "'press grid hat'");
  press("F");
  CHECK(c->drum_grid_hat && lit("F") && c->drum_voice == KIT_RIM &&
        lit("A"), "F should switch the Grid Hat on, over the kit");
  handle_cc(CC_BREATH, 0);
  n_tapped = 0;
  hat_for(150);
  CHECK(hat_taps(0, 127) == 0, "a breath at rest shouldn't play it");
  handle_cc(CC_BREATH, 20);
  hat_for(1000);
  int gentle = hat_taps(0, 127), gentle_loudest = hat_loudest();
  CHECK(gentle >= 7 && gentle <= 9, "%d hats in a second of 16ths",
        gentle);
  CHECK(gentle_loudest <= 30, "a gentle breath should play it softly, "
        "not at %d", gentle_loudest);
  n_tapped = 0;
  handle_cc(CC_BREATH, BREATH_FLOOR + 35);  // under where the 32nds come in
  hat_for(1000);
  int medium = hat_taps(0, 127);
  CHECK(medium >= 7 && medium <= 9, "%d hats in a second, blowing medium",
        medium);
  CHECK(hat_loudest() > gentle_loudest + 15, "blowing medium should play it "
        "harder, not at %d", hat_loudest());
  n_tapped = 0;
  handle_cc(CC_BREATH, BREATH_FLOOR + 55);  // the 32nds fading in
  hat_for(1000);
  int fading = hat_taps(0, 127), fading_soft = hat_taps(0, 40);
  CHECK(fading >= 14 && fading <= 17 && fading_soft >= 7,
        "%d hats in a second, %d soft, as the 32nds fade in", fading,
        fading_soft);
  n_tapped = 0;
  handle_cc(CC_BREATH, 104);
  hat_for(1000);
  int hard = hat_taps(0, 127), hard_soft = hat_taps(0, 50);
  CHECK(hard >= 14 && hard <= 17 && hard_soft == 0,
        "%d hats in a second blowing hard, %d of them soft", hard, hard_soft);
  n_tapped = 0;
  jig_time = true;
  hat_for(1000);
  int jig = hat_taps(0, 127);
  CHECK(jig >= 10 && jig <= 13, "%d hats in a second of jig time, hard",
        jig);
  n_tapped = 0;
  handle_cc(CC_BREATH, 20);
  hat_for(1000);
  jig = hat_taps(0, 127);
  CHECK(jig >= 5 && jig <= 7, "%d hats in a second of jig time, gently",
        jig);
  jig_time = false;
  handle_cc(CC_BREATH, 0);
  n_tapped = 0;
  hat_for(300);
  CHECK(hat_taps(0, 127) == 0, "it should stop with the breath");
  // A layer, over whichever kit: picking another leaves it on, and the kit
  // plays as ever under it.
  handle_cc(CC_BREATH, 60);
  press("Z");
  CHECK(c->drum_voice == KIT_808_A && lit("Z") && lit("F"),
        "picking a kit should leave the Grid Hat on");
  n_tapped = 0;
  hat_for(300);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(hat_taps(0, 127) > 0 &&
        count_tapped(MIDI_ON, MIDI_DRUM_OUT_RIM, CHANNEL_DRUM) == 1,
        "the Grid Hat and the kit should play together");
  // F again, or the drum off, stops it.
  press("F");
  CHECK(!c->drum_grid_hat && !lit("F"), "F again should switch it off");
  n_tapped = 0;
  hat_for(300);
  CHECK(hat_taps(0, 127) == 0, "F again should stop it");
  press("D");
  press("tab");
  n_tapped = 0;
  hat_for(300);
  CHECK(hat_taps(0, 127) == 0, "the drum off should stop it");
  handle_cc(CC_BREATH, 0);

  // Away from the Breath Gate, J and K are DOWNBEAT and UPBEAT as ever.
  select_ep("W");
  CHECK(strcmp(key_current_label(key_for_cap("J")), "DOWN\nBEAT") == 0,
        "J should be DOWNBEAT on the foot bass");
  CHECK(strcmp(key_current_label(key_for_cap("L")), "UP\nHIGH") == 0,
        "L should be UP HIGH on the foot bass");
  breath_hook = NULL;
  midi_tap = NULL;
  full_reset();
}

// With the pedals keeping time, the Grid Hat plays where the foot bass
// does, on the beat's 72 subbeats: 16ths blowing gently, with its upbeat a
// subbeat early, and halfway between them too blowing hard -- or in jig time
// its three, and six.  Three beats of it, a kick starting each, `late_ms`
// after the beat before would have ended: which subbeats it played on in the
// last, as a string, and how many hats in all.
static int hat_subbeats(int breath, bool jig, int late_ms, char* out) {
  jig_time = jig;
  handle_cc(CC_BREATH, 0);
  breath_hat_tick();
  handle_cc(CC_BREATH, breath);
  uint64_t beat = 60 * NS_PER_SEC / 116;
  current_beat_ns = beat;
  last_downbeat_ns = now();
  int kicks = 0, from = hat_taps(0, 127), was = n_tapped;
  out[0] = '\0';
  while (true) {
    uint64_t t = now();
    if (t - last_downbeat_ns >= beat + late_ms * 1000000ULL) {
      if (++kicks == 3) break;
      last_downbeat_ns = t;  // a kick: the pedals' grid starts again
      out[0] = '\0';
    }
    breath_hat_tick();
    int subbeat = (int)((now() - last_downbeat_ns) * 72 / beat);
    if (n_tapped != was) {
      sprintf(out + strlen(out), "%s%d", out[0] ? " " : "", subbeat);
    }
    was = n_tapped;
    usleep(500);
  }
  int hats = hat_taps(0, 127) - from;
  handle_cc(CC_BREATH, 0);
  breath_hat_tick();
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  jig_time = false;
  return hats;
}

// Whether `got` is `want`, each subbeat or one after it, for scheduling.
static bool subbeats_match(const char* got, const int* want, int n) {
  const char* p = got;
  for (int i = 0; i < n; i++) {
    char* end;
    long v = strtol(p, &end, 10);
    if (end == p || v < want[i] || v > want[i] + 1) return false;
    p = end;
  }
  while (*p == ' ') p++;
  return *p == '\0';
}

static void test_grid_hat_on_the_pedals() {
  full_reset();
  midi_tap = tap_midi;
  press("tab");
  select_ep("tab");
  press("F");
  char got[256];
  hat_subbeats(BREATH_FLOOR + 20, false, 0, got);
  const int straight[] = {0, 18, 35, 54};
  CHECK(subbeats_match(got, straight, 4), "gently, the hat should play the "
        "foot bass's 16ths, 0 18 35 54, beat after beat, not %s", got);
  hat_subbeats(104, false, 0, got);
  const int straight_hard[] = {0, 9, 18, 26, 35, 44, 54, 63};
  CHECK(subbeats_match(got, straight_hard, 8), "hard, the hat should play "
        "0 9 18 26 35 44 54 63, beat after beat, not %s", got);
  hat_subbeats(BREATH_FLOOR + 20, true, 0, got);
  const int jig[] = {0, 21, 45};
  CHECK(subbeats_match(got, jig, 3), "gently in jig time, the hat should "
        "play the foot bass's 0 21 45, beat after beat, not %s", got);
  hat_subbeats(104, true, 0, got);
  const int jig_hard[] = {0, 10, 21, 33, 45, 58};
  CHECK(subbeats_match(got, jig_hard, 6), "hard in jig time, the hat should "
        "play 0 10 21 33 45 58, beat after beat, not %s", got);
  // Kicks a little late: the hat's played the downbeat already, when it was
  // due, and doesn't again.
  int hats = hat_subbeats(BREATH_FLOOR + 20, false, 15, got);
  CHECK(hats >= 11 && hats <= 13, "%d hats in three beats of 16ths with the "
        "kicks late", hats);
  midi_tap = NULL;
  full_reset();
}

// The Feet's steps, as feet_hook hears them.
static struct {
  int kind;
  double hard, level;
  int subbeat;
} feet_heard[64];
static int n_feet_heard;
static uint64_t feet_beat_ns;

static void feet_record(int kind, double hard, double level, bool on_grid) {
  (void)on_grid;
  if (n_feet_heard >= 64) return;
  feet_heard[n_feet_heard].kind = kind;
  feet_heard[n_feet_heard].hard = hard;
  feet_heard[n_feet_heard].level = level;
  feet_heard[n_feet_heard].subbeat =
    (int)((now() - last_downbeat_ns) * 72 / feet_beat_ns);
  n_feet_heard++;
}

// Three beats of the pedals at 116 BPM, `late_ms` late each, with the breath
// at `breath`, ticked as jammer does; the subbeats of the last beat's steps,
// the thump as "T", into `out`, and how many steps there were in all.
static int feet_subbeats(int breath, bool jig, int late_ms, bool pedals,
                         char* out) {
  jig_time = jig;
  handle_cc(CC_BREATH, breath);
  feet_beat_ns = 60 * NS_PER_SEC / 116;
  current_beat_ns = pedals ? feet_beat_ns : 0;
  last_downbeat_ns = now();
  n_feet_heard = 0;
  if (pedals) feet_pedal(MIDI_DRUM_IN_KICK, 100, last_downbeat_ns);
  int kicks = 0, from = 0;
  while (true) {
    uint64_t t = now();
    if (t - last_downbeat_ns >= feet_beat_ns + late_ms * 1000000ULL) {
      if (++kicks == 3) break;
      last_downbeat_ns = t;  // a kick that starts a beat
      from = n_feet_heard;
      if (pedals) feet_pedal(MIDI_DRUM_IN_KICK, 100, t);
    }
    feet_tick();
    usleep(500);
  }
  out[0] = '\0';
  for (int i = from; i < n_feet_heard; i++) {
    if (feet_heard[i].kind == FEET_THUMP) {
      sprintf(out + strlen(out), "%sT", out[0] ? " " : "");
    } else {
      sprintf(out + strlen(out), "%s%d", out[0] ? " " : "",
              feet_heard[i].subbeat);
    }
  }
  handle_cc(CC_BREATH, 0);
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  jig_time = false;
  return n_feet_heard;
}

// Feet: "thump . tap tap" on the pedals, the thump the pedal's own; the gap
// filled and every step harder the harder it's blown; and nothing at all
// without the pedals keeping a beat.
static void test_feet() {
  // With the Mac's sound for them, the Feet are the drum's kit to start with.
  feet_hook = feet_record;
  full_reset();
  CHECK(c->drum_voice == KIT_FEET, "the Feet should be the drum's first kit");
  press("tab");
  CHECK(c->on[ENDPOINT_DRUM] && c->selected_endpoint == ENDPOINT_DRUM &&
        lit("S"), "tab should switch the drum on, the Feet lit on S");
  char got[256];
  feet_subbeats(0, false, 0, true, got);
  CHECK(strncmp(got, "T 3", 3) == 0 && strstr(got, " 5") &&
        strlen(got) <= 8, "at rest the Feet should be thump . tap tap, "
        "T 35 54, not %s", got);
  bool gentle = true;
  for (int i = 0; i < n_feet_heard; i++) {
    if (feet_heard[i].hard > 0.01) gentle = false;
  }
  CHECK(gentle, "at rest every step should be gentle");
  // The last beat's: the thump, the upbeat's bright tap, the predown's dull.
  CHECK(n_feet_heard >= 3 &&
        feet_heard[n_feet_heard - 3].kind == FEET_THUMP &&
        feet_heard[n_feet_heard - 2].kind == FEET_TAP &&
        feet_heard[n_feet_heard - 1].kind == FEET_TAP_SOFT,
        "the Feet should be thump, bright tap, dull tap");
  feet_subbeats(104, false, 0, true, got);
  CHECK(strncmp(got, "T 18", 4) == 0 || strncmp(got, "T 19", 4) == 0,
        "blowing should fill in the gap, T 18 35 54, not %s", got);
  feet_subbeats(0, true, 0, true, got);
  CHECK(strncmp(got, "T 45", 4) == 0 || strncmp(got, "T 46", 4) == 0,
        "in jig time at rest, T 45, not %s", got);
  feet_subbeats(104, true, 0, true, got);
  CHECK(strncmp(got, "T 21", 4) == 0 || strncmp(got, "T 22", 4) == 0,
        "in jig time blowing, T 21 45, not %s", got);
  feet_subbeats(BREATH_FULL, false, 0, true, got);
  bool stomps = n_feet_heard > 0;
  for (int i = 0; i < n_feet_heard; i++) {
    if (feet_heard[i].hard < 0.99 || feet_heard[i].level < 0.99) {
      stomps = false;
    }
  }
  CHECK(stomps, "blowing all the way every step should be a stomp");
  // Kicks late: one thump each, on the kick, and the grid doesn't play one.
  feet_subbeats(0, false, 40, true, got);
  int thumps = 0;
  for (int i = 0; i < n_feet_heard; i++) {
    thumps += feet_heard[i].kind == FEET_THUMP;
  }
  CHECK(thumps == 3, "%d thumps for three late kicks", thumps);
  // Stopping: the last kicked beat's taps are the last.
  feet_beat_ns = 60 * NS_PER_SEC / 116;
  current_beat_ns = feet_beat_ns;
  last_downbeat_ns = now();
  feet_pedal(MIDI_DRUM_IN_KICK, 100, last_downbeat_ns);
  n_feet_heard = 0;
  uint64_t stopped = now();
  while (now() - stopped < 3 * feet_beat_ns) {
    feet_tick();
    usleep(500);
  }
  CHECK(n_feet_heard == 2, "after the last kick, its two taps and no more, "
        "not %d steps", n_feet_heard);
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  CHECK(feet_subbeats(BREATH_FULL, false, 0, false, got) == 0,
        "without the pedals the Feet should be silent, not %s", got);
  // The hat pedal, since the kick, has the beat's taps: the
  // grid leaves them to it.
  drum_kick_pedal_ns = 1;
  drum_pedal_ns = 2;
  feet_subbeats(0, false, 0, true, got);
  CHECK(strcmp(got, "T") == 0,
        "after another pedal the Feet should leave the taps to it, not %s",
        got);
  drum_pedal_ns = 0;
  press("tab");
  CHECK(feet_subbeats(0, false, 0, true, got) == 0,
        "with the drum off the Feet should be silent");
  // And on another kit: none of the Feet, and that kit's own sounds.
  select_ep("tab");
  press("tab");
  press("A");
  CHECK(feet_subbeats(0, false, 0, true, got) == 0,
        "on the rim kit the Feet should be silent");
  select_ep("tab");
  press("S");
  CHECK(c->drum_voice == KIT_FEET && lit("S"), "S should pick the Feet");
  // Each pedal a step of its own, beat or no beat.
  CHECK(c->on[ENDPOINT_DRUM], "the drum should still be on");
  current_beat_ns = 0;
  const int pedals[4] = {MIDI_DRUM_IN_KICK, MIDI_DRUM_IN_SNARE,
                         MIDI_DRUM_IN_HIHAT, MIDI_DRUM_IN_CRASH};
  const int kinds[4] = {FEET_THUMP, FEET_TAP_LOW, FEET_TAP, FEET_TAP_LOWER};
  for (int p = 0; p < 4; p++) {
    n_feet_heard = 0;
    handle_feet(MIDI_ON, pedals[p], 100);
    CHECK(n_feet_heard == 1 && feet_heard[0].kind == kinds[p],
          "pedal %d should be a step of kind %d with no beat, not %d steps",
          p, kinds[p], n_feet_heard);
  }
  // The Stompy Feet: the same, but the kick on a board.
  press("D");
  CHECK(c->drum_voice == KIT_STOMPY_FEET && lit("D") && !lit("S"),
        "D should pick the Stompy Feet");
  const int stompy_kinds[4] = {FEET_BOARD, FEET_TAP_LOW, FEET_TAP,
                               FEET_TAP_LOWER};
  for (int p = 0; p < 4; p++) {
    n_feet_heard = 0;
    handle_feet(MIDI_ON, pedals[p], 100);
    CHECK(n_feet_heard == 1 && feet_heard[0].kind == stompy_kinds[p],
          "on the Stompy Feet pedal %d should be a step of kind %d",
          p, stompy_kinds[p]);
  }
  press("S");
  // As hard as it's hit: soft quieter, firm at the grid's level, and
  // hardest louder and harder.
  const int vels[3] = {40, 100, 127};
  double heard_level[3], heard_hard[3];
  for (int v = 0; v < 3; v++) {
    n_feet_heard = 0;
    handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, vels[v]);
    heard_level[v] = n_feet_heard ? feet_heard[0].level : -1;
    heard_hard[v] = n_feet_heard ? feet_heard[0].hard : -1;
  }
  CHECK(heard_level[0] > 0 && heard_level[0] < 0.4 &&
        fabs(heard_level[1] - 1) < 1e-9 && heard_level[2] > 1.2,
        "a pedal's level should follow its velocity (%.2f %.2f %.2f)",
        heard_level[0], heard_level[1], heard_level[2]);
  CHECK(heard_hard[0] == 0 && heard_hard[1] == 0 && heard_hard[2] > 0.3,
        "only the hardest hits should land harder (%.2f %.2f %.2f)",
        heard_hard[0], heard_hard[1], heard_hard[2]);
  // And the grid's taps go by the last four kicks, on average.
  for (int k = 0; k < 4; k++) handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 60);
  memset(kick_times, 0, sizeof(kick_times));
  char soft_got[256];
  feet_subbeats(0, false, 0, true, soft_got);  // its kicks at 100: see below
  double tap_level = -1;
  for (int i = 0; i < n_feet_heard; i++) {
    if (feet_heard[i].kind != FEET_THUMP) tap_level = feet_heard[i].level;
  }
  // Four at 60, then feet_subbeats' three at 100: the last four, 60 and
  // three 100s, average 90.
  CHECK(fabs(tap_level - pow(0.9, 1.5)) < 1e-6,
        "the taps should follow the last four kicks (%.3f, not %.3f)",
        tap_level, pow(0.9, 1.5));
  // The drum's -/+ reach them, as they would a kit's channel volume.
  n_feet_heard = 0;
  feet_pedal(MIDI_DRUM_IN_HIHAT, 100, now());
  press("=");
  feet_pedal(MIDI_DRUM_IN_HIHAT, 100, now());
  press("-");
  press("-");
  feet_pedal(MIDI_DRUM_IN_HIHAT, 100, now());
  press("=");
  CHECK(n_feet_heard == 3 && feet_heard[1].level > feet_heard[0].level &&
        feet_heard[2].level < feet_heard[0].level,
        "+ should make the Feet louder and - quieter");
  press("tab");
  n_feet_heard = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
  CHECK(n_feet_heard == 0, "with the drum off, a pedal shouldn't step");
  memset(kick_times, 0, sizeof(kick_times));
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  feet_hook = NULL;
  full_reset();
}

// The last velocity sent for `note` on `channel`, or 0.
static int tapped_velocity(int note, int channel) {
  int v = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action == MIDI_ON && tapped[i].channel == channel &&
        tapped[i].note == note) {
      v = tapped[i].velocity;
    }
  }
  return v;
}

// fluidsynth's kits, as the Feet: each pedal its own sound, beat or no beat;
// the modifier keys the pattern, starting on "kick . hat hat"; and blowing
// filling in what they leave out and hitting everything harder.
static void test_kits_and_the_breath() {
  full_reset();
  midi_tap = tap_midi;
  CHECK(c->downbeat[ENDPOINT_DRUM] && c->upbeat[ENDPOINT_DRUM] &&
        c->pre_unique[ENDPOINT_DRUM] && !c->doubled[ENDPOINT_DRUM] &&
        !c->upbeat_high[ENDPOINT_DRUM],
        "the drum should start on DOWNBEAT, UPBEAT and PRE UNIQ");
  press("tab");
  select_ep("tab");
  select_drum_kit(KIT_RIM);
  const DrumKit* kit = &KITS[KIT_RIM];
  c->vel[ENDPOINT_DRUM] = false;  // it starts on, but first, without

  // Each pedal, with no beat: the kick 2, the rim 1, the hat 4, the ride 3.
  current_beat_ns = 0;
  const int pedals[4] = {MIDI_DRUM_IN_KICK, MIDI_DRUM_IN_SNARE,
                         MIDI_DRUM_IN_HIHAT, MIDI_DRUM_IN_CRASH};
  const int notes[4] = {kit->kick, MIDI_DRUM_OUT_RIM, kit->hihat,
                        MIDI_DRUM_OUT_RIDE};
  const int channels[4] = {CHANNEL_KICK, CHANNEL_DRUM, CHANNEL_DRUM,
                           CHANNEL_DRUM};
  for (int p = 0; p < 4; p++) {
    memset(kick_times, 0, sizeof(kick_times));
    n_tapped = 0;
    handle_feet(MIDI_ON, pedals[p], 100);
    CHECK(count_tapped(MIDI_ON, notes[p], channels[p]) == 1 &&
          count_tapped(MIDI_ON, -1, CHANNEL_DRUM) +
          count_tapped(MIDI_ON, -1, CHANNEL_KICK) == 1,
          "pedal %d should play note %d and nothing else", p, notes[p]);
  }
  // At the kit's levels: the rim and ride pedals at 90 without VEL.
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 40);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_CRASH, 40);
  CHECK(tapped_velocity(MIDI_DRUM_OUT_RIM, CHANNEL_DRUM) ==
        (int)(90 * kit->rim_vel) &&
        tapped_velocity(MIDI_DRUM_OUT_RIDE, CHANNEL_DRUM) ==
        (int)(90 * kit->ride_vel),
        "without VEL the pedals should be at 90 by the kit's scales");
  // And with VEL, as hard as they're hit.
  c->vel[ENDPOINT_DRUM] = true;
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 40);
  CHECK(tapped_velocity(MIDI_DRUM_OUT_RIM, CHANNEL_DRUM) ==
        (int)(40 * kit->rim_vel), "with VEL the rim should follow the pedal");
  c->vel[ENDPOINT_DRUM] = false;
  // Harder the harder it's blown.
  handle_cc(CC_BREATH, BREATH_FULL);
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(tapped_velocity(MIDI_DRUM_OUT_RIM, CHANNEL_DRUM) >
        (int)(90 * kit->rim_vel * 1.3),
        "blowing should have the rim hit harder, not %d",
        tapped_velocity(MIDI_DRUM_OUT_RIM, CHANNEL_DRUM));
  handle_cc(CC_BREATH, 0);
  // Without DOWNBEAT, the kick pedal's the pedal's alone; the rest still
  // play.
  press("J");
  CHECK(!c->downbeat[ENDPOINT_DRUM], "J should switch DOWNBEAT off");
  memset(kick_times, 0, sizeof(kick_times));
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_HIHAT, 100);
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_KICK) == 0 &&
        count_tapped(MIDI_ON, kit->hihat, CHANNEL_DRUM) == 1,
        "without DOWNBEAT the kick pedal shouldn't play the kick");
  press("J");
  // With the drum off, nothing.
  press("tab");
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_DRUM) == 0,
        "with the drum off a pedal shouldn't play the kit");
  press("tab");

  // The grid: at rest the upbeat's and predown's hats, and no preup's.
  // (A kick after the pedals above, so the beat's the kit's.)
  drum_kick_pedal_ns = now();
  const int subbeats[3] = {preup_subbeat(), upbeat_subbeat(), 3 * 72 / 4};
  int rest[3], blown[3], filled[3];
  for (int i = 0; i < 3; i++) {
    n_tapped = 0;
    arpeggiate_drum(subbeats[i], now());
    rest[i] = tapped_velocity(kit->hihat, CHANNEL_DRUM);
  }
  CHECK(rest[0] == 0 && rest[1] > 0 && rest[2] > 0,
        "at rest the kit should play . hat hat (%d %d %d)", rest[0], rest[1],
        rest[2]);
  // The rim and ride pedals leave the kit's hats be.
  usleep(1000);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_CRASH, 100);
  n_tapped = 0;
  arpeggiate_drum(upbeat_subbeat(), now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) > 0,
        "the rim and ride pedals shouldn't stop the kit's hats");
  // A hat pedal just after the kit's own hat plays nothing more: it's
  // that hat, a little late.
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_HIHAT, 100);
  CHECK(count_tapped(MIDI_ON, kit->hihat, CHANNEL_DRUM) == 0,
        "a hat pedal just after the kit's hat shouldn't play another");
  usleep((AUTO_HAT_PEDAL_MS + 5) * 1000);
  // But the hat pedal, any time after the beat's kick, has the rest of the
  // beat's: the kit leaves the hats to it, until the next.
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_HIHAT, 100);
  CHECK(count_tapped(MIDI_ON, kit->hihat, CHANNEL_DRUM) == 1,
        "a hat pedal well after the kit's hat should play");
  n_tapped = 0;
  arpeggiate_drum(upbeat_subbeat(), now());
  arpeggiate_drum(3 * 72 / 4, now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) == 0,
        "after a hat pedal the kit shouldn't play its own hats");
  usleep(1000);
  drum_kick_pedal_ns = now();
  n_tapped = 0;
  arpeggiate_drum(upbeat_subbeat(), now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) > 0,
        "the next beat's kick should hand the hats back to the kit");
  // Blowing fills the preup in, and hits them all harder.
  handle_cc(CC_BREATH, BREATH_FULL);
  for (int i = 0; i < 3; i++) {
    n_tapped = 0;
    arpeggiate_drum(subbeats[i], now());
    blown[i] = tapped_velocity(kit->hihat, CHANNEL_DRUM);
  }
  CHECK(blown[0] > 0 && blown[1] > rest[1] * 1.3 && blown[2] > rest[2] * 1.3,
        "blowing should fill in the preup and play them all harder "
        "(%d %d %d)", blown[0], blown[1], blown[2]);
  // Half way to full, a little: the fill fades in.
  handle_cc(CC_BREATH, BREATH_FLOOR + 30);
  n_tapped = 0;
  arpeggiate_drum(subbeats[0], now());
  filled[0] = tapped_velocity(kit->hihat, CHANNEL_DRUM);
  CHECK(filled[0] > 0 && filled[0] < blown[0],
        "a middling breath should fill the preup in a little, not %d",
        filled[0]);
  // BREATH FILL, the drum's CHORD, on to start with: off, blowing fills
  // nothing in, but still hits harder.
  const Key* comma = key_for_cap(",");
  CHECK(lit(",") && !key_is_dead(comma) &&
        strcmp(key_current_label(comma), "BREATH\nFILL") == 0,
        "comma should be BREATH FILL on the drum, and start on");
  press(",");
  CHECK(!lit(","), "comma should switch BREATH FILL off");
  handle_cc(CC_BREATH, BREATH_FULL);
  n_tapped = 0;
  arpeggiate_drum(subbeats[0], now());
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_DRUM) == 0,
        "without BREATH FILL blowing shouldn't fill in the preup");
  n_tapped = 0;
  arpeggiate_drum(subbeats[1], now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) == blown[1],
        "without BREATH FILL blowing should still hit harder");
  press(",");
  // BREATH HARD, on M next to it, on to start with: off, blowing still
  // fills in, but hits no harder.
  const Key* m = key_for_cap("M");
  CHECK(lit("M") && strcmp(key_current_label(m), "BREATH\nHARD") == 0,
        "M should be BREATH HARD on the drum, and start on");
  press("M");
  CHECK(!lit("M") && c->drum_voice == KIT_RIM,
        "M should switch BREATH HARD off, and leave the kit be");
  n_tapped = 0;
  arpeggiate_drum(subbeats[1], now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) == rest[1],
        "without BREATH HARD blowing shouldn't hit harder, not %d",
        tapped_velocity(kit->hihat, CHANNEL_DRUM));
  n_tapped = 0;
  arpeggiate_drum(subbeats[0], now());
  CHECK(tapped_velocity(kit->hihat, CHANNEL_DRUM) > 0 &&
        tapped_velocity(kit->hihat, CHANNEL_DRUM) < blown[0],
        "without BREATH HARD blowing should still fill in, softer");
  n_tapped = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(tapped_velocity(MIDI_DRUM_OUT_RIM, CHANNEL_DRUM) ==
        (int)(90 * kit->rim_vel),
        "without BREATH HARD blowing shouldn't have the pedals hit harder");
  press("M");
  handle_cc(CC_BREATH, 0);
  // The keys set it: UPBEAT off leaves the upbeat for the breath to fill,
  // and DOUBLED puts the preup in without it.
  press("K");
  press("P");
  usleep(1000);
  drum_kick_pedal_ns = now();  // past the snare pedal above
  n_tapped = 0;
  arpeggiate_drum(subbeats[0], now());
  arpeggiate_drum(subbeats[1], now());
  CHECK(count_tapped(MIDI_ON, kit->hihat, CHANNEL_DRUM) == 1 &&
        tapped_velocity(kit->hihat, CHANNEL_DRUM) > 0,
        "DOUBLED on and UPBEAT off should be the preup's hat alone");
  press("K");
  press("P");
  midi_tap = NULL;
  memset(kick_times, 0, sizeof(kick_times));
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  full_reset();

  // The Feet follow the keys too.
  feet_hook = feet_record;
  full_reset();
  press("tab");
  select_ep("tab");
  char got[256];
  press("K");  // UPBEAT off: at rest, "thump . . tap"
  feet_subbeats(0, false, 0, true, got);
  CHECK(strcmp(got, "T 54") == 0 || strcmp(got, "T 55") == 0,
        "without UPBEAT the Feet at rest should be T 54, not %s", got);
  feet_subbeats(104, false, 0, true, got);
  CHECK(strncmp(got, "T 18", 4) == 0 || strncmp(got, "T 19", 4) == 0,
        "blowing should fill both in, T 18 35 54, not %s", got);
  press(",");  // BREATH FILL off: blowing fills in neither
  feet_subbeats(104, false, 0, true, got);
  CHECK(strcmp(got, "T 54") == 0 || strcmp(got, "T 55") == 0,
        "without BREATH FILL blowing shouldn't fill the Feet in, not %s",
        got);
  press(",");
  press("M");  // BREATH HARD off: blowing fills in, but no stomps
  feet_subbeats(BREATH_FULL, false, 0, true, got);
  bool gentle = n_feet_heard > 0;
  for (int i = 0; i < n_feet_heard; i++) {
    if (feet_heard[i].hard > 0.01) gentle = false;
  }
  CHECK(gentle && (strncmp(got, "T 18", 4) == 0 ||
                   strncmp(got, "T 19", 4) == 0),
        "without BREATH HARD blowing should fill the Feet in gently, not %s",
        got);
  press("M");
  press("K");
  press("P");  // DOUBLED: the preup at rest
  feet_subbeats(0, false, 0, true, got);
  CHECK(strncmp(got, "T 18", 4) == 0 || strncmp(got, "T 19", 4) == 0,
        "with DOUBLED the Feet at rest should be T 18 35 54, not %s", got);
  press("P");
  press("J");  // DOWNBEAT off: no thump
  feet_subbeats(0, false, 0, true, got);
  CHECK(got[0] != 'T' && !strchr(got, 'T'),
        "without DOWNBEAT the kick shouldn't thump, not %s", got);
  n_feet_heard = 0;
  handle_feet(MIDI_ON, MIDI_DRUM_IN_HIHAT, 100);
  CHECK(n_feet_heard == 1, "without DOWNBEAT the other pedals should step");
  memset(kick_times, 0, sizeof(kick_times));
  current_beat_ns = 0;
  last_downbeat_ns = 0;
  feet_hook = NULL;
  full_reset();
}

// A step through the Feet's sound, rendered: its RMS over half a second;
// how much of it is under 200Hz and over 1kHz; and how much of it is still
// sounding after the first 100ms, against how much there was in them.
static double feet_render(int kind, double hard, double* low, double* high,
                          double* after) {
  static float l[24000], r[24000];
  memset(l, 0, sizeof(l));
  memset(r, 0, sizeof(r));
  atomic_store(&audio_breath_fx, 0);
  feet_hit(kind, hard, 1, false);
  for (int b = 0; b < 50; b++) {
    play_breath_instruments(l + 480 * b, r + 480 * b, 480, 48000);
  }
  double sum = 0, lo = 0, hi = 0, first = 0, rest = 0, lp1 = 0, lp2 = 0;
  double k_lo = 1 - exp(-2 * M_PI * 200 / 48000);
  double k_hi = 1 - exp(-2 * M_PI * 1000 / 48000);
  for (int i = 0; i < 24000; i++) {
    double x = l[i];
    lp1 += (x - lp1) * k_lo;
    lp2 += (x - lp2) * k_hi;
    sum += x * x;
    lo += lp1 * lp1;
    hi += (x - lp2) * (x - lp2);
    if (i < 4800) first += x * x; else rest += x * x;
  }
  *low = sqrt(lo / sum);
  *high = sqrt(hi / sum);
  *after = sqrt(rest / first);
  return sqrt(sum / 24000);
}

static void test_feet_sound() {
  double thump_low, thump_high, thump_after, tap_low, tap_high, tap_after;
  double stomp_low, stomp_high, stomp_after;
  double thump = feet_render(FEET_THUMP, 0, &thump_low, &thump_high,
                             &thump_after);
  double tap = feet_render(FEET_TAP, 0, &tap_low, &tap_high, &tap_after);
  double stomp = feet_render(FEET_THUMP, 1, &stomp_low, &stomp_high,
                             &stomp_after);
  CHECK(thump > 1e-3 && tap > 1e-4, "the Feet made nothing (%.4f %.4f)",
        thump, tap);
  // As recorded, a tap has nearly as much low as a thump, but a thump's much
  // less bright for it.
  CHECK(thump_low / thump_high > 2 * (tap_low / tap_high),
        "a thump should be deeper than a tap (%.2f %.2f under 200Hz, "
        "%.2f %.2f over 1kHz)", thump_low, tap_low, thump_high, tap_high);
  CHECK(stomp > 2 * thump && stomp * stomp_low > 2 * thump * thump_low,
        "a stomp should be louder and deeper than a gentle thump "
        "(%.4f %.4f)", stomp, thump);
  // A wooden floor doesn't ring: each step's mostly over within 100ms, with
  // only the room after, as quiet as recorded thumps' (0.09-0.11).
  CHECK(thump_after < 0.15 && tap_after < 0.15 && stomp_after < 0.15,
        "the Feet shouldn't ring on (%.4f %.4f %.4f after 100ms)",
        thump_after, tap_after, stomp_after);
  // The snare's tap lower than the hihat's, and the ride's lower still:
  // less and less bright for how low they go.
  double low_low, low_high, low_after, lower_low, lower_high, lower_after;
  double low = feet_render(FEET_TAP_LOW, 0, &low_low, &low_high, &low_after);
  double lower = feet_render(FEET_TAP_LOWER, 0, &lower_low, &lower_high,
                             &lower_after);
  CHECK(low > 1e-4 && lower > 1e-4 &&
        tap_high / tap_low > low_high / low_low &&
        low_high / low_low > lower_high / lower_low,
        "each tap should be lower than the last (%.2f %.2f %.2f)",
        tap_high / tap_low, low_high / low_low, lower_high / lower_low);
  CHECK(low_after < 0.15 && lower_after < 0.15,
        "the lower taps shouldn't ring on (%.3f %.3f)", low_after,
        lower_after);
  printf("feet: low tap %.1fdB, lower tap %.1fdB RMS\n", 20 * log10(low),
         20 * log10(lower));
  // No two steps quite alike, but only a little different.
  double same[8];
  double lo_same = 1, hi_same = 0;
  for (int k = 0; k < 8; k++) {
    double a, b, c2;
    same[k] = feet_render(FEET_TAP, 0.3, &a, &b, &c2);
    lo_same = fmin(lo_same, same[k]);
    hi_same = fmax(hi_same, same[k]);
  }
  CHECK(hi_same > 1.02 * lo_same && hi_same < 1.6 * lo_same,
        "the same step eight times should vary a little (%.4f to %.4f)",
        lo_same, hi_same);
  // The Stompy Feet's board: deeper than the thump, but no louder to the
  // ear, and it doesn't ring on either -- a short boom, not a drum's.
  double board_low, board_high, board_after;
  memset(feet_voices, 0, sizeof(feet_voices));
  double board = feet_render(FEET_BOARD, 0, &board_low, &board_high,
                             &board_after);
  CHECK(board > 1e-3 && board_low > thump_low + 0.1 &&
        board_high < thump_high,
        "the board should be bassier than the thump (%.2f %.2f under 200Hz, "
        "%.2f %.2f over 1kHz)", board_low, thump_low, board_high,
        thump_high);
  CHECK(board_after < 0.15, "the board shouldn't ring on (%.3f)",
        board_after);
  // The Audio Output menu's drum volume reaches them.
  set_drum_gain(0);
  double muted = feet_render(FEET_THUMP, 0, &board_low, &board_high,
                             &board_after);
  set_drum_gain(1);
  CHECK(muted == 0, "drum volume at 0 should silence the Feet (%.5f)", muted);
  printf("feet: gentle thump %.1fdB, tap %.1fdB, stomp %.1fdB RMS\n",
         20 * log10(thump), 20 * log10(tap), 20 * log10(stomp));
  memset(feet_voices, 0, sizeof(feet_voices));  // nothing left ringing
}

// The Brushes' stir, rendered: silence with the breath at rest, a soft stir
// blowing gently, and a louder one blowing harder, lifting off once the
// breath stops.  Held at `breath` for `settle` seconds, then measured for
// `seconds`: the loudness, and how far it wavers from 50ms to 50ms, as a
// fraction of it.
static double brush_stir(int breath, double settle, double seconds,
                         double* waver) {
  const double sr = 48000;
  float l[2400], r[2400];
  double windows[200];
  int n = 0;
  double sum = 0;
  atomic_store(&audio_breath_fx, BREATH_FX_BRUSH);
  atomic_store(&audio_breath, breath);
  for (int b = 0; b < (int)(settle * 20); b++) {
    memset(l, 0, sizeof(l));
    memset(r, 0, sizeof(r));
    play_breath_instruments(l, r, 2400, sr);
  }
  for (; n < (int)(seconds * 20) && n < 200; n++) {
    memset(l, 0, sizeof(l));
    memset(r, 0, sizeof(r));
    play_breath_instruments(l, r, 2400, sr);
    double w = 0;
    for (int i = 0; i < 2400; i++) w += (double)l[i] * l[i] + r[i] * r[i];
    windows[n] = sqrt(w / 4800);
    sum += w;
  }
  double rms = sqrt(sum / (n * 4800.0));
  if (waver) {
    double most = 0;
    for (int i = 0; i < n; i++) most = fmax(most, fabs(windows[i] - rms));
    *waver = most / rms;
  }
  return rms;
}

static void test_brush_swish() {
  int at_rest = BREATH_FLOOR;
  int gentle = BREATH_FLOOR + (int)(0.2 * (BREATH_FULL - BREATH_FLOOR));
  int hard = BREATH_FLOOR + (int)(0.72 * (BREATH_FULL - BREATH_FLOOR));
  double slow_waver, fast_waver;
  double still = brush_stir(at_rest, 0.5, 0.5, NULL);
  double slow = brush_stir(gentle, 0.5, 2.0, &slow_waver);
  double fast = brush_stir(hard, 0.5, 2.0, &fast_waver);
  double held = brush_stir(BREATH_FULL, 0.5, 1.0, NULL);
  double after = brush_stir(at_rest, 0.5, 0.5, NULL);
  CHECK(slow > 1e-4, "a gentle breath should stir: %.5f", slow);
  CHECK(still < slow * 0.01, "a breath at rest shouldn't stir: %.5f", still);
  CHECK(fast > slow * 1.5, "blowing harder should stir louder: %.5f vs "
        "%.5f", fast, slow);
  CHECK(slow_waver < 0.2 && fast_waver < 0.2,
        "a steady breath should stir steadily, not swell: %.2f, %.2f",
        slow_waver, fast_waver);
  CHECK(held > fast * 0.9, "past the slap it should keep stirring: %.5f",
        held);
  CHECK(after < 0.01 * fast, "it should lift off once the breath stops: "
        "%.5f", after);
  atomic_store(&audio_breath_fx, 0);
  atomic_store(&audio_breath, 0);
}

// The Tamb Shake, rendered off a clock of its own, `seconds` at `breath`
// after `settle`: how loud, and, if `pulse` isn't NULL, how strongly its
// loudness 5ms at a time pulses at 32nds and at six a beat, at 116 BPM.
static uint64_t tamb_clock = 1000000000ULL;
static double tamb_shake(int breath, double settle, double seconds,
                         double* pulse) {
  const double sr = 48000;
  float l[240], r[240];
  double windows[1000];
  int n = 0;
  double sum = 0;
  atomic_store(&audio_breath_fx, BREATH_FX_TAMB);
  atomic_store(&audio_breath, breath);
  for (int b = 0; b < (int)(settle * 200) + (int)(seconds * 200); b++) {
    memset(l, 0, sizeof(l));
    memset(r, 0, sizeof(r));
    audio_block_ns = tamb_clock;
    play_breath_instruments(l, r, 240, sr);
    tamb_clock += 5000000;
    if (b < (int)(settle * 200) || n >= 1000) continue;
    double w = 0;
    for (int i = 0; i < 240; i++) w += (double)l[i] * l[i];
    windows[n++] = sqrt(w / 240);
    sum += w;
  }
  double rms = sqrt(sum / (n * 240.0));
  for (int k = 0; pulse && k < 2; k++) {
    double hz = 116.0 / 60 * (k ? 6 : 8), re = 0, im = 0;
    for (int i = 0; i < n; i++) {
      re += windows[i] * cos(2 * M_PI * hz * i * 0.005);
      im += windows[i] * sin(2 * M_PI * hz * i * 0.005);
    }
    pulse[k] = sqrt(re * re + im * im) / n;
  }
  return rms;
}

static void test_tamb_shake() {
  int at_rest = BREATH_FLOOR;
  int gentle = BREATH_FLOOR + (int)(0.25 * (BREATH_FULL - BREATH_FLOOR));
  int medium = BREATH_FLOOR + (int)(0.5 * (BREATH_FULL - BREATH_FLOOR));
  int hard = BREATH_FLOOR + (int)(0.9 * (BREATH_FULL - BREATH_FLOOR));
  double straight[2], jig[2];
  double still = tamb_shake(at_rest, 0.3, 0.5, NULL);
  double soft = tamb_shake(gentle, 0.3, 3.0, NULL);
  double mid = tamb_shake(medium, 0.3, 3.0, straight);
  double loud = tamb_shake(hard, 0.3, 3.0, NULL);
  atomic_store(&audio_jig, true);
  tamb_shake(medium, 0.3, 3.0, jig);
  atomic_store(&audio_jig, false);
  double after = tamb_shake(at_rest, 0.5, 0.5, NULL);
  CHECK(soft > 1e-5, "a gentle breath should shake it: %.6f", soft);
  CHECK(still < soft * 0.01, "a breath at rest shouldn't: %.6f", still);
  CHECK(mid > soft * 2 && loud > mid * 2, "blowing harder should shake it "
        "harder: %.6f, %.6f, %.6f", soft, mid, loud);
  CHECK(straight[0] > 2 * straight[1], "it should shake in 32nds: %.6f at "
        "32nds, %.6f at six a beat", straight[0], straight[1]);
  CHECK(jig[1] > 2 * jig[0], "it should shake six a beat in jig time: "
        "%.6f at 32nds, %.6f at six a beat", jig[0], jig[1]);
  CHECK(after < loud * 0.01, "it should stop with the breath: %.6f", after);
  atomic_store(&audio_breath_fx, 0);
  atomic_store(&audio_breath, 0);
}

// The Breath Gate's synthesized voices, rendered off a simulated clock: each
// sounds while you blow and is gone within a moment of stopping.
static double sim_ns = 1e9;
static float build_l[64], build_r[64];

static double render_builds(unsigned fx, int breath, double seconds) {
  const double sr = 48000;
  const int len = 64;
  breath_set(breath, fx);
  double sum = 0;
  long n = 0;
  int blocks = (int)(seconds * sr / len);
  for (int b = 0; b < blocks; b++) {
    audio_block_ns = (uint64_t)sim_ns;
    memset(build_l, 0, sizeof(build_l));
    memset(build_r, 0, sizeof(build_r));
    build_follow_breath(breath_blown(breath));
    play_breath_instruments(build_l, build_r, len, sr);
    for (int i = 0; i < len; i++) {
      if (!isfinite(build_l[i])) return NAN;
      sum += build_l[i] * build_l[i];
      n++;
    }
    sim_ns += len * 1e9 / sr;
  }
  return n ? sqrt(sum / n) : 0;
}

static void test_build_voices() {
  const struct { unsigned fx; const char* name; } VOICES[] = {
    {BREATH_FX_RISER, "riser"}, {BREATH_FX_WOBBLE, "wobble"},
  };
  for (int v = 0; v < 2; v++) {
    render_builds(VOICES[v].fx, 0, 0.1);
    double on = render_builds(VOICES[v].fx, 80, 0.5);
    CHECK(on > 0.005 && on < 1, "%s: %.4f while blowing", VOICES[v].name, on);
    render_builds(VOICES[v].fx, 0, 0.05);
    double off = render_builds(VOICES[v].fx, 0, 0.1);
    CHECK(off < 1e-4, "%s: %.5f after the breath stopped", VOICES[v].name,
          off);
  }
  // Both at once.
  double both = render_builds(BREATH_FX_RISER | BREATH_FX_WOBBLE, 80, 0.5);
  CHECK(both > 0.005 && both < 1, "riser and wobble together: %.4f", both);
  render_builds(0, 0, 0.1);
  breath_set(0, 0);
}

// The Breath Gate's own sounds play on one channel, the left, and all on the
// right instead with CH: the right goes to a talkbox, so nothing should
// reach it unless asked.
static void render_sides(unsigned fx, double seconds, double* left,
                         double* right) {
  const double sr = 48000;
  const int len = 64;
  int breath = BREATH_FLOOR + (int)(0.6 * (BREATH_FULL - BREATH_FLOOR));
  breath_set(breath, fx);
  double l2 = 0, r2 = 0;
  long n = 0;
  for (int b = 0; b < (int)(seconds * sr / len); b++) {
    audio_block_ns = (uint64_t)sim_ns;
    memset(build_l, 0, sizeof(build_l));
    memset(build_r, 0, sizeof(build_r));
    build_follow_breath(breath_blown(breath));
    play_breath_instruments(build_l, build_r, len, sr);
    for (int i = 0; i < len; i++) {
      l2 += build_l[i] * build_l[i];
      r2 += build_r[i] * build_r[i];
      n++;
    }
    sim_ns += len * 1e9 / sr;
  }
  *left = sqrt(l2 / n);
  *right = sqrt(r2 / n);
}

static void test_breath_sounds_follow_ch() {
  const struct { unsigned fx; const char* name; } VOICES[] = {
    {BREATH_FX_GUIRO, "guiro"}, {BREATH_FX_WASHBOARD, "washboard"},
    {BREATH_FX_GUIRA, "guira"}, {BREATH_FX_RISER, "riser"},
    {BREATH_FX_WOBBLE, "wobble"}, {BREATH_FX_BRUSH, "brushes"},
    {BREATH_FX_TAMB, "tamb shake"},
  };
  for (int v = 0; v < 7; v++) {
    double l, r;
    render_sides(VOICES[v].fx, 0.5, &l, &r);
    CHECK(l > 1e-5 && r == 0, "%s should be on the left alone: %.6f, %.6f",
          VOICES[v].name, l, r);
    render_sides(0, 0.5, &l, &r);  // let it stop
    render_sides(VOICES[v].fx | BREATH_FX_RIGHT, 0.5, &l, &r);
    CHECK(r > 1e-5 && l == 0, "with CH, %s should be on the right alone: "
          "%.6f, %.6f", VOICES[v].name, l, r);
    render_sides(0, 0.5, &l, &r);
  }
  breath_set(0, 0);

  // And CH on the Breath Gate is what says so.
  full_reset();
  breath_hook = record_breath;
  press("`");
  CHECK(!(told_fx & BREATH_FX_RIGHT), "the Breath Gate should start left");
  press("F2");
  CHECK(told_fx & BREATH_FX_RIGHT, "CH should move the Breath Gate right");
  press("F2");
  CHECK(!(told_fx & BREATH_FX_RIGHT), "CH again should move it back");
  // And its -/+ are what say how loud they are.
  breath_gain_hook = record_breath_gain;
  press("=");
  CHECK(told_breath_gain > 1, "+ should make the Breath Gate's own louder");
  press("-");
  press("-");
  CHECK(told_breath_gain < 1, "- should make the Breath Gate's own quieter");
  breath_gain_hook = NULL;
  breath_hook = NULL;
  full_reset();
}

// The vocoder: the chord, shaped by the microphone, and nothing when the
// microphone is quiet.
static void test_vocoder() {
  vocoder_prepare(&vocoder, 48000);
  double hz[VOCODER_VOICES], weight[VOCODER_VOICES];
  int n = vocoder_chord(hz, weight);
  CHECK(n >= 2 * VOCODER_OCTAVES, "the vocoder's chord has %d notes", n);

  // Shepard: a higher chord leans on its lower octaves, so where the weight
  // sits in pitch doesn't follow the chord up.
  double centre[2];
  for (int k = 0; k < 2; k++) {
    atomic_store(&audio_chord_root, k == 0 ? 24 : 35);  // C, then B
    int m = vocoder_chord(hz, weight);
    double sum = 0, total = 0;
    for (int i = 0; i < m; i++) {
      sum += weight[i] * log2(hz[i]);
      total += weight[i];
    }
    CHECK(fabs(total - 1) < 1e-9, "the vocoder's weights sum to %f", total);
    centre[k] = sum;
  }
  CHECK(fabs(centre[1] - centre[0]) < 0.15,
        "C to B moved the vocoder's register %.2f octaves",
        centre[1] - centre[0]);
  atomic_store(&audio_chord_root, 26);
  n = vocoder_chord(hz, weight);
  double quiet = 0, loud = 0;
  for (int i = 0; i < 48000; i++) {
    float out = vocoder_process(&vocoder, 0, hz, weight, n, 1);
    if (i > 24000) quiet += out * out;
  }
  // A held note, which shouldn't be taken for the room however long it goes.
  for (int i = 0; i < 48000 * 4; i++) {
    float in = (float)(0.1 * sin(2 * M_PI * 440 * i / 48000.0));
    float out = vocoder_process(&vocoder, in, hz, weight, n, 1);
    CHECK(isfinite(out), "the vocoder blew up");
    if (i > 48000 * 3) loud += out * out;
  }
  CHECK(quiet == 0, "the vocoder made a sound from silence");
  CHECK(sqrt(loud / 48000) > 0.01,
        "the vocoder was too quiet four seconds into a note: %.4f",
        sqrt(loud / 48000));
}

// The drum's defaults and its kit's reach: VEL on, on the right, the Grid
// Hat playing the kit's own hat, or with no kit the Standard set's, and the
// Mac turning the kit up the harder it's blown with BREATH HARD.
static double told_kit_gain = 1;
static void record_kit_gain(double gain) {
  told_kit_gain = gain;
}

static void test_drum_kit_reach() {
  full_reset();
  midi_tap = tap_midi;
  breath_hook = record_breath;
  kit_gain_hook = record_kit_gain;
  update_breath_fx();
  CHECK(c->vel[ENDPOINT_DRUM], "the drum should start with VEL on");
  CHECK(c->pans[ENDPOINT_DRUM] && told_drum_right,
        "the drum, and the Feet, should start on the right");
  select_ep("tab");
  press("F2");
  CHECK(!c->pans[ENDPOINT_DRUM] && !told_drum_right,
        "CH should move the drum left");
  press("F2");

  // The Grid Hat on the kit's hat: the Ride kit's is its ride.
  press("tab");
  select_drum_kit(KIT_RIDE);
  press("F");
  handle_cc(CC_BREATH, 60);
  n_tapped = 0;
  hat_for(300);
  int ride = 0, other = 0;
  for (int i = 0; i < n_tapped; i++) {
    if (tapped[i].action != MIDI_ON || tapped[i].channel != CHANNEL_HAT) {
      continue;
    }
    if (tapped[i].note == MIDI_DRUM_OUT_RIDE) ride++; else other++;
  }
  CHECK(ride > 0 && other == 0,
        "the Grid Hat should play the kit's hat (%d ride, %d other)", ride,
        other);
  // Over the Feet and the Stompy Feet, the Feet's hat: a toe's tap.
  feet_hook = feet_record;
  for (int k = 0; k < 2; k++) {
    select_drum_kit(k ? KIT_STOMPY_FEET : KIT_FEET);
    n_tapped = 0;
    n_feet_heard = 0;
    hat_for(300);
    bool taps = n_feet_heard > 0;
    for (int i = 0; i < n_feet_heard; i++) {
      if (feet_heard[i].kind != FEET_TAP) taps = false;
    }
    CHECK(taps && hat_taps(0, 127) == 0,
          "over the %s the Grid Hat should be their taps (%d taps, %d hats)",
          k ? "Stompy Feet" : "Feet", n_feet_heard, hat_taps(0, 127));
  }
  feet_hook = NULL;
  // With no kit, just the Grid Hat, on the Standard set's closed hat, and
  // the pedals silent.
  select_drum_kit(KIT_RIM);
  press("A");
  CHECK(c->drum_voice == KIT_NONE, "A on Rim should switch the kit off");
  n_tapped = 0;
  hat_for(300);
  CHECK(hat_taps(0, 127) > 0, "with no kit the Grid Hat should still play");
  handle_feet(MIDI_ON, MIDI_DRUM_IN_KICK, 100);
  handle_feet(MIDI_ON, MIDI_DRUM_IN_SNARE, 100);
  CHECK(count_tapped(MIDI_ON, -1, CHANNEL_DRUM) +
        count_tapped(MIDI_ON, -1, CHANNEL_KICK) == 0,
        "with no kit the pedals should play nothing");
  CHECK(told_kit_gain == 1, "with no kit there's nothing to turn up");

  // BREATH HARD turns the kit up on the Mac, blowing; not without it.
  press("A");
  handle_cc(CC_BREATH, BREATH_FULL);
  CHECK(told_kit_gain > 1.8, "blowing should turn the kit up (%.2f)",
        told_kit_gain);
  press("M");
  CHECK(told_kit_gain == 1, "without BREATH HARD it shouldn't (%.2f)",
        told_kit_gain);
  press("M");
  handle_cc(CC_BREATH, 0);
  CHECK(told_kit_gain == 1, "at rest the kit's as it was (%.2f)",
        told_kit_gain);

  kit_gain_hook = NULL;
  breath_hook = NULL;
  midi_tap = NULL;
  full_reset();
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
  test_ignored_flags_are_dead();
  test_octave_and_volume();
  test_musical_mode();
  test_drum_picks_notes_defaults();
  test_globals();
  test_kick_duck();
  test_breath_fx();
  test_whistle_breath_tick();
  test_breath_mic();
  test_breath_gate();
  test_voice_lead();
  test_voice_lead_first_number();
  test_voice_channels_reach_the_synth();
  test_trance_gate();
  test_every_kick_plays();
  test_tambourines();
  test_brush_swish();
  test_breath_sounds_follow_ch();
  test_grid_hat_on_the_pedals();
  test_drum_kit_reach();
  test_feet();
  test_kits_and_the_breath();
  test_feet_sound();
  test_tamb_shake();
  test_jawharp_voices();
  test_build_voices();
  test_vocoder();
  test_percussion_bank_reaches_the_synth();
  test_extra_footbasses();
  test_drones();
  test_whistle();
  test_mandolin();
  test_mandolin_sound();
  test_mandolin_effects();
  test_mandolin_chord_effects();
  test_mandolin_recording();
  test_vocoder_holds();
  test_voice_fx();
  test_fx_gate();
  test_whistle_guard();
  test_speech_picks();
  test_speech_commands();
  test_nashville_numbers();
  test_spoken_presses();
  test_nashville_chords();
  test_speech_dictionary();
  test_speech_gate();
  test_numrec_unusual();

  if (failures) {
    printf("\n%d failure(s)\n", failures);
    return 1;
  }
  printf("all keypad tests passed (%d keys in the layout)\n", N_KEYS);
  return 0;
}
