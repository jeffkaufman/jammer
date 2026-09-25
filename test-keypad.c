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
  CHECK(key_is_dead(key_for_cap(";")), "SHORTISH should be dead on Low");

  select_ep("W");  // the foot bass reads everything
  for (int i = 0; i < N_KEYS; i++) {
    CHECK(!key_is_dead(&KEYS[i]), "%s should be live on the foot bass",
          KEYS[i].cap);
  }

  select_ep("tab");  // the drum: no pitch to move
  CHECK(key_is_dead(key_for_cap(",")), "CHORD should be dead on the drum");
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
static void record_breath(int breath, unsigned fx) {
  told_breath = breath;
  told_fx = fx;
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
        c->voices[ENDPOINT_BREATH] == 94,
        "the Breath Gate should start as a drone chord on Halo Pad");

  press("`");
  CHECK(c->on[ENDPOINT_BREATH] && c->selected_endpoint == ENDPOINT_BREATH,
        "` didn't switch the Breath Gate on and select it");
  CHECK(told_fx == 0, "on a pad there's no percussion to play");

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
  const char* caps[] = {"B", "N", "M", "Z", "G"};
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

  press("Z");
  press("G");
  press("A");
  CHECK(told_fx == (BREATH_FX_RISER | BREATH_FX_WOBBLE) &&
        lit("Z") && lit("G") && lit("A") && lit("C") &&
        c->breath_layers == (BREATH_LAYER_RISER | BREATH_LAYER_WOBBLE |
                             BREATH_LAYER_SNARE_ROLL),
        "the riser, wobble and snare roll should all be on, over the pad");

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
  CHECK(c->voices[ENDPOINT_BREATH] == 90 && lit("X") && lit("Z") &&
        current_note[ENDPOINT_BREATH] != -1,
        "X should bring a pad back under the layers");
  handle_cc(CC_BREATH, 0);

  // The rest of the voice keys are the drones' pads, as on any drone.
  const char* pads[] = {"S", "D", "F", "H", "X", "C", "V"};
  for (int i = 0; i < 7; i++) {
    const Key* k = key_for_cap(pads[i]);
    CHECK(breath_voice_for_note(k->note) < 0 &&
          drone_voice_for_note(k->note) >= 0,
          "%s should pick a pad on the Breath Gate", pads[i]);
  }

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
    float out = vocoder_process(in, chord_hz, chord_weight, n, 1);
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
  vocoder_prepare(48000);
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
           whistle_engine_voice[i] == WHISTLE_VOICES[i].own),
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

  // Every voice key but B is one of the whistle's.
  CHECK(!whistle_key_is_dead(key_for_cap("N")), "N shouldn't draw as dead");
  CHECK(!whistle_key_is_dead(key_for_cap("D")), "D shouldn't");
  CHECK(whistle_key_is_dead(key_for_cap("B")), "B should");
  strike("B", false);
  CHECK(whistle_voice == 2 && c->voices[ENDPOINT_FLEX] == flex_voice,
        "B should do nothing");

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
  // brings one back, and so does switching the vocoder off.
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
  CHECK(atomic_load(&whistle_pub_target_gain) > 0 && lit("D") &&
        !(atomic_load(&whistle_pub_fx) == VFX_BIT(VFX_VOCODER)),
        "switching the vocoder off shouldn't leave the whistle silent");
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
  bool flex_doubled = c->doubled[ENDPOINT_FLEX];
  strike("P", false);
  CHECK(c->doubled[ENDPOINT_FLEX] == flex_doubled,
        "a modifier key reached an endpoint while the whistle was selected");
  CHECK(whistle_key_is_dead(key_for_cap("P")), "P should draw as dead");

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

  // J K L ; are the vocoder and the effects beside it: each its key on and
  // off, and any of them together.
  const char* fx_keys[] = {"J", "K", "L", ";"};
  const char* fx_labels[] = {"Vocoder", "Robot", "Voice\nBass", "Saw\nBass"};
  unsigned all = 0;
  for (int i = 0; i < 4; i++) {
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
  CHECK(whistle_key_is_dead(key_for_cap("'")), "' should be dead");

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
  for (int state = 0; state < 5; state++) {
    static const char* STATES[] = {"flex", "drum", "drone", "whistle",
                                   "breath gate"};
    whistle_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS :
                           state == 4 ? ENDPOINT_BREATH : ENDPOINT_FLEX;
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
  for (int state = 0; state < 5; state++) {
    whistle_reset();
    c->selected_endpoint = state == 1 ? ENDPOINT_DRUM :
                           state == 2 ? ENDPOINT_DRONE_BASS :
                           state == 4 ? ENDPOINT_BREATH : ENDPOINT_FLEX;
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

// The Snare Roll: starts with the breath, speeds up with it, stops with it.
static void test_snare_roll() {
  full_reset();
  midi_tap = tap_midi;
  press("`");
  press("A");
  n_tapped = 0;
  handle_cc(CC_BREATH, 60);
  breath_roll_tick();
  CHECK(count_tapped(MIDI_ON, MIDI_DRUM_OUT_SNARE, CHANNEL_DRUM) == 1,
        "a breath should start the roll at once");
  breath_roll_tick();
  CHECK(count_tapped(MIDI_ON, MIDI_DRUM_OUT_SNARE, CHANNEL_DRUM) == 1,
        "only one snare a slot");

  // Blowing hard: 32nds, at 116 BPM with no pedals, one every 65ms.
  handle_cc(CC_BREATH, 110);
  uint64_t until = now() + 300 * 1000000ULL;
  while (now() < until) {
    breath_roll_tick();
    usleep(1000);
  }
  int hits = count_tapped(MIDI_ON, MIDI_DRUM_OUT_SNARE, CHANNEL_DRUM);
  CHECK(hits >= 5 && hits <= 7, "%d snares in 300ms of 32nds", hits);

  handle_cc(CC_BREATH, 0);
  until = now() + 150 * 1000000ULL;
  while (now() < until) {
    breath_roll_tick();
    usleep(1000);
  }
  CHECK(count_tapped(MIDI_ON, MIDI_DRUM_OUT_SNARE, CHANNEL_DRUM) == hits,
        "the roll should stop with the breath");
  midi_tap = NULL;
  full_reset();
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

// The vocoder: the chord, shaped by the microphone, and nothing when the
// microphone is quiet.
static void test_vocoder() {
  vocoder_prepare(48000);
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
    float out = vocoder_process(0, hz, weight, n, 1);
    if (i > 24000) quiet += out * out;
  }
  // A held note, which shouldn't be taken for the room however long it goes.
  for (int i = 0; i < 48000 * 4; i++) {
    float in = (float)(0.1 * sin(2 * M_PI * 440 * i / 48000.0));
    float out = vocoder_process(in, hz, weight, n, 1);
    CHECK(isfinite(out), "the vocoder blew up");
    if (i > 48000 * 3) loud += out * out;
  }
  CHECK(quiet == 0, "the vocoder made a sound from silence");
  CHECK(sqrt(loud / 48000) > 0.01,
        "the vocoder was too quiet four seconds into a note: %.4f",
        sqrt(loud / 48000));
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
  test_snare_roll();
  test_build_voices();
  test_vocoder();
  test_percussion_bank_reaches_the_synth();
  test_extra_footbasses();
  test_drones();
  test_whistle();
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
