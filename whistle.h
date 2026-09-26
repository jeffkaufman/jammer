#ifndef JML_WHISTLE_H
#define JML_WHISTLE_H

// The whistle bass: jammer's front end onto the whistle-synth engine.
//
// `pitch.c`, `synth.c` and `engine.c` come from ~/code/whistle-synth and are
// compiled unchanged -- this is the third front end onto them, after that
// repo's command-line build and its Mac app.  The engine knows nothing about
// jammer: it takes a microphone sample and hands back a stereo pair.
//
//   microphone -> whistleinput.h -> ring -> whistle_mix() -> fluidsynth's
//                                                            output buffers
//
// There is no second audio device and no second output stream.  fluidsynth
// already hands jammer a render callback (see jammer_audio_render in
// macapi.h); the whistle is summed into those same buffers, so it comes out
// of whatever the Audio Output menu picked, at whatever latency that device
// is already running -- on its left channel only, where fluidsynth's
// endpoints are panned.
//
// The whistle is an instrument in the UI, but it is not an endpoint: endpoints
// are fluidsynth MIDI channels (common.h), and this one makes its own sound
// and has no channel.  So the selection that the voice, octave and volume keys
// follow lives here rather than in c->selected_endpoint, and jammermidilib.h
// -- shared with the Pi -- is untouched.
//
// Include after common.h.

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "engine.h"   // from whistle-synth
#include "breathmic.h"

// ---------------------------------------------------------------------------
// The voices
//
// Ten of whistle-synth's presets, on the ten voice keys that the drum kits
// don't use, and on N and M the breath voices, which play nothing and
// breathe for the breath controller instead (see breathmic.h): Whistle
// Breath, a voice of its own, and Blow Noise, which is a layer rather than a
// voice -- M switches it on and off over whichever voice is playing, so a
// whistled bass line can go on while you blow.  B is Pass Through: the
// microphone itself, 6dB down, so it's less likely to feed back.  The
// vocoder, and the vocal effects beside it, are layers over these rather
// than voices, on J K L: see WHISTLE_FX.
//
// Looked up by name at startup rather than by index: the presets table has
// had entries come and go, and an index that silently shifted would put a
// different instrument under every key.
// ---------------------------------------------------------------------------

#define N_WHISTLE_VOICES 13

typedef struct {
  int note;            // the voice key's pseudo-note, as KEYS[] sends it
  const char* preset;  // what synth.c calls it, or NULL for jammer's own
  const char* label;   // what to draw on the key; "\n" splits lines
  int own;             // for jammer's own: one of those below
} WhistleVoice;

// Not whistle-synth's: jammer's own breath voices.
#define WHISTLE_BREATH_WHISTLE (-3)
#define WHISTLE_BREATH_BLOW (-4)
// And the microphone as it comes, the engine's voice 0.
#define WHISTLE_PASS_THROUGH (-5)
// How far down, against the voices.
#define WHISTLE_PASS_THROUGH_GAIN 0.5

static const WhistleVoice WHISTLE_VOICES[N_WHISTLE_VOICES] = {
  {'A', "bass",           "Bass"},
  {'S', "octaveless",     "Octave\nless"},
  {'D', "reese",          "Reese"},
  {'F', "eight-oh-eight", "808"},
  {'G', "fm",             "FM"},
  {'H', "fm-sub",         "Sub\nFM"},
  {'Z', "square",         "Square"},
  {'X', "drawbar",        "Draw\nbar"},
  {'C', "drawbar-hi",     "High\nDrawbar"},
  {'V', "accordion",      "Accor\ndion"},
  {'N', NULL,             "Whistle\nBreath", WHISTLE_BREATH_WHISTLE},
  {'M', NULL,             "Blow\nNoise", WHISTLE_BREATH_BLOW},
  {'B', NULL,             "Pass\nThrough", WHISTLE_PASS_THROUGH},
};

// The vocal effects over the voice: the vocoder and those beside it
// (voicefx.h), on J K L ; ' -- row keys the whistle has no other use for
// -- or from the Vocal FX menu.  Any of them at once, each its key on and
// off, summed side by side.
enum {
  VFX_NONE,
  VFX_VOCODER,
  VFX_ROBOT,
  VFX_BASS,
  VFX_SAW,
  VFX_WAH,
  N_VFX,
};

typedef struct {
  int note;           // the key's pseudo-note, as KEYS[] sends it
  const char* cap;    // and the key, as the Vocal FX menu names it
  const char* label;  // what to draw on the key; "\n" splits lines
  const char* name;   // what the status row calls it
} WhistleFx;

static const WhistleFx WHISTLE_FX[N_VFX] = {
  [VFX_VOCODER] = {'J', "J", "Vocoder", "voc"},
  [VFX_ROBOT] = {'K', "K", "Robot", "robot"},
  [VFX_BASS] = {'L', "L", "Voice\nBass", "vbass"},
  [VFX_SAW] = {';', ";", "Saw\nBass", "sawbass"},
  [VFX_WAH] = {'\'', "'", "Wah\nBass", "wah"},
};

#define VFX_BIT(fx) (1u << (fx))

// The effect on a modifier key, or VFX_NONE.
static int whistle_fx_for_note(int note) {
  for (int fx = VFX_VOCODER; fx < N_VFX; fx++) {
    if (WHISTLE_FX[fx].note == note) return fx;
  }
  return VFX_NONE;
}

static unsigned whistle_fx;  // the effects that are on, VFX_BIT each

// Which input the vocal effects hear: 0 for the first, the whistle's, or 1
// for the second, so a vocal mic in input 2 can go through them while the
// whistle mic stays on the bass.  F2 while the whistle's selected, and only
// if the device has a second input: whistle_input_count, which
// whistleinput.h sets when it opens one.
static int whistle_fx_mic;
static _Atomic int whistle_input_count;

static bool whistle_has_second_mic(void) {
  return atomic_load_explicit(&whistle_input_count, memory_order_relaxed) >= 2;
}
// The voice silenced under the vocoder, so it plays alone: the lit voice key
// again while the vocoder's on.  Any voice key brings a voice back.
// Switching the effects off doesn't: silenced is what you asked for, and
// with them off too the whistle plays nothing until a voice key's pressed.
static bool whistle_voice_muted;

// Blow Noise, on and off over the voice: M.  Whistle Breath, if that's the
// voice, has the breath controller instead, since there's only one.
static bool whistle_blow_on;

// What a voice's key says.
static const char* whistle_voice_label(int index) {
  return WHISTLE_VOICES[index].label;
}

// What the status row calls a voice.
static const char* whistle_voice_name(int index) {
  switch (WHISTLE_VOICES[index].own) {
  case WHISTLE_BREATH_WHISTLE: return "whistle-breath";
  case WHISTLE_BREATH_BLOW:    return "blow-noise";
  case WHISTLE_PASS_THROUGH:   return "pass-through";
  }
  return WHISTLE_VOICES[index].preset;
}

// What engine_set_voice() takes for each of the ten, filled in by
// whistle_resolve_voices(), or one of jammer's own.  Voice 0 in the engine is the
// raw input passed through, so presets start at 1.
static int whistle_engine_voice[N_WHISTLE_VOICES];

// -1 until the engine exists, so the UI can say "no whistle" rather than
// drawing controls that do nothing.
static int whistle_voice_for_note(int note) {
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    if (WHISTLE_VOICES[i].note == note) return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// State the UI owns
//
// Read and written on the main thread under jammer_lock, published to the
// audio thread through the atomics below.  Kept as plain values rather than
// behind accessors because the drawing code reads all of them every frame.
// ---------------------------------------------------------------------------

static bool whistle_available;   // the engine is built and the input is open
static bool whistle_on;          // the instrument is switched on
static bool whistle_selected;    // the voice/octave/volume keys act on it
static int whistle_voice;        // index into WHISTLE_VOICES
static int whistle_octave;       // -SYNTH_OCTAVE_SHIFT..+SYNTH_OCTAVE_SHIFT
static int whistle_volume;       // 0-9, the engine's own volume knob
static int whistle_gate = 5;     // 0-9, how far above the room a note must be

// The gate knob as a margin over the room noise: 2dB a step, higher numbers
// gating less, as whistle-synth's own knob (engine_set_gate) -- but five
// steps further up, so its 0, 11.9x the room, is 5 here, and 0 here is
// 37.7x.  Its range ran out on a loud stage.  Past its 0, so set on the
// detector directly rather than through engine_set_gate, which clamps to
// its own knob.
#define WHISTLE_GATE_SHIFT 5
static double whistle_gate_margin(int step) {
  return 1.5 * pow(10.0, 0.1 * (9 + WHISTLE_GATE_SHIFT - step));
}

// 0-9, what counts as blowing full tilt.  whistle-synth's own app starts this
// at 5, which is 0.22 -- a vocal mic at the lip.  Against fluidsynth that
// costs 7dB even with a good microphone, because the voice spends the
// player's breath on loudness and a level_full it never reaches leaves the
// voice permanently dark and quiet.  Measured (./whistlelevels), the bass
// voice against the foot bass as jml_setup leaves it, by perceived loudness
// at stage volume:
//
//     input peak      step 0   step 2   step 5
//     0.300            +3.9     +3.9     +3.5
//     0.100            +3.9     +3.0     -4.9
//     0.030            -0.8     -6.2    -14.6
//
// 2 rather than 0 because the bottom of the knob is not free: level_full is
// what the voice's dynamics are measured against, so setting it under what
// the player actually delivers leaves them permanently maxed out with no
// dynamics left.  2 matches a strong whistle into a decent microphone.  It is
// a per-rig number either way -- set it just above what the status row shows
// while you whistle hard.
static int whistle_level_full = 2;
// And the same for the Blows, which have their own: blowing straight into a
// microphone runs far hotter than whistling at it.  Where to start is a guess
// until it's been tried -- set it, like the other, just above what the status
// row shows while you blow hard.
static int whistle_blow_level_full = 3;
// And where they start: see bm_blow_gate_for_step.  Low, so a soft breath
// breathes; the room is what sets how low it can go.
static int whistle_blow_gate = 3;

// Whistle Breath's own gate and full-blow level, apart from the whistle
// bass's.  The bass wants a strict gate, so the room never plays a note, and
// a full level it reaches easily, so the voices sound full.  A breath wants
// the opposite: to start on the quietest whistle, and to leave room above
// it for whistling harder.  The engine only listens while Whistle Breath is
// on, so it takes these then.
static int whistle_breath_gate = 8;
static int whistle_breath_level_full = 5;
static int whistle_low_note;     // MIDI note: the lowest that counts as a note
static int whistle_high_note;    // MIDI note: the highest

// Voice 0 in the engine is the raw input passed straight through, which is
// how you check that the microphone is live and the gate is set before
// trusting any of it.  A setup control rather than an eleventh voice: it
// belongs with the gate and the level in the menu, not on a key.
static bool whistle_passthrough;

// The whistle's own master level, beside fluidsynth's in the Audio Output
// menu rather than folded into it: this is a second synthesis engine, and the
// balance between the two is a thing you set once and then leave alone while
// the global volume moves.
#define MAX_WHISTLE_GAIN 2.0
static double whistle_gain = 1.0;

// And the vocal effects', the vocoder's and the rest, in the Vocal FX menu:
// they're a different kind of sound from the whistle's voices, and sit
// against the rig at a level of their own.
static double vocoder_gain = 1.0;

// Where the volume knob sits when nothing has moved it, so that VOL-/VOL+ can
// light up the way the endpoint volume keys do -- lit means "moved".
//
// The top of the knob, not the middle of it, which is the opposite of what
// whistle-synth's own app does and deliberately so.  There the knob is the
// master and the player turns it up; here the whistle has to sit against
// fluidsynth, and the engine's step 5 is 0.198 -- 14dB down before it ever
// reaches the mix, which is audibly buried under the rest of the rig.
//
// There is also nothing above the top to default to: the engine clips at +/-1
// *after* its own volume, so step 9 is its full scale and the only way to be
// louder than that is the master slider, which would be clipping to do it.
// So the trim runs downwards from unity and the slider does the balance.
#define WHISTLE_VOLUME_DEFAULT 9

// ---------------------------------------------------------------------------
// UI -> audio thread
//
// The audio thread never locks and never allocates.  Each control is one
// atomic int that the audio thread compares against what it last applied, so
// a control that hasn't moved costs a load and a branch per block.  Same
// shape as whistle-synth's own mac/core/whistle.c, for the same reasons.
// ---------------------------------------------------------------------------

static _Atomic int whistle_pub_voice = -1;
static _Atomic int whistle_pub_volume = WHISTLE_VOLUME_DEFAULT;
static _Atomic int whistle_pub_octave = 0;
static _Atomic int whistle_pub_gate = 5;
static _Atomic int whistle_pub_level_full = 5;
static _Atomic int whistle_pub_low_note = 0;
static _Atomic int whistle_pub_high_note = 0;
// Gain and on/off arrive as one number: the audio thread ramps towards it, so
// switching the instrument off is a fade rather than a click, and dragging the
// slider doesn't zipper.  Scaled by 1000 to keep it an int.
static _Atomic int whistle_pub_target_gain = 0;
// And the vocoder's, the same way, since it sounds beside the whistle.
static _Atomic int whistle_pub_vocoder_gain = 0;

// Counters and meters, written by the audio thread and read by the UI.
static _Atomic int whistle_dropouts;
static _Atomic int whistle_meter_level;   // playing level, x10000
static _Atomic int whistle_meter_freq;    // last detected pitch in Hz, x100
static _Atomic int whistle_meter_voiced;

// A frequency as the nearest MIDI note, and the two ends of a range as the
// notes just inside it -- rounding outwards would offer a note the detector
// cannot actually find.
static int whistle_hz_to_note(double hz) {
  return (int)lround(69.0 + 12.0 * log2(hz / 440.0));
}
static int whistle_lowest_note(void) {
  double exact = 69.0 + 12.0 * log2(ENGINE_LOWEST_HZ / 440.0);
  return (int)ceil(exact);
}
static int whistle_highest_note(void) {
  double exact = 69.0 + 12.0 * log2(ENGINE_HIGHEST_HZ / 440.0);
  return (int)floor(exact);
}
// The range to start in and come back to: the ordinary whistle range, which
// is what the detector costs the least to find and what nearly every player
// stays inside.  The ends above are what has ever been *recorded*, and
// reaching for them is a decision -- at the bottom it is paid for in
// detection lag -- so they are offered rather than assumed.
static int whistle_default_low_note(void) {
  return (int)ceil(69.0 + 12.0 * log2(ENGINE_MIN_HZ / 440.0));
}
static int whistle_default_high_note(void) {
  return (int)floor(69.0 + 12.0 * log2(ENGINE_MAX_HZ / 440.0));
}

static _Atomic unsigned whistle_pub_fx;
// Whether there's a whistle-controlled voice for whistle_guard to keep the
// effects clear of.
static _Atomic int whistle_pub_guard;
static _Atomic int whistle_pub_fx_mic;
// The vocal effects' own gate, in dBFS peak: see RoomGate.
#define VFX_GATE_DEFAULT_DB -54   // 0.002, where the gate was fixed before
#define VFX_GATE_MIN_DB -70
#define VFX_GATE_MAX_DB -10
static int whistle_fx_gate_db = VFX_GATE_DEFAULT_DB;
static _Atomic int whistle_pub_fx_gate_db = VFX_GATE_DEFAULT_DB;
// The loudest the effects' input has been since the UI last looked, x10000,
// for setting the gate against.
static _Atomic int whistle_meter_fx_level;
// WHISTLE_BREATH_WHISTLE, _BLOW or _VOLUME while one of them is on and
// breathing, or 0.
static _Atomic int whistle_pub_breath;
// The full-blow step for whichever breath voice is on.
static _Atomic int whistle_pub_breath_level_full = 3;
static _Atomic int whistle_pub_blow_gate = 3;
// What the breath voices would have the breath controller say, 0-127, or -1
// while neither is breathing.  Written by the audio thread once a block;
// whistle_breath_tick() passes it on.
static _Atomic int whistle_breath_cc = -1;

// Whether the voice itself makes a sound: the breath voices don't.
static bool whistle_voice_sounds(int index) {
  if (whistle_voice_muted) return false;
  int own = WHISTLE_VOICES[index].own;
  return own != WHISTLE_BREATH_WHISTLE;
}

// The level the status row shows for what's sounding: the whistle's, or the
// vocoder's when it's all there is.  Caller must hold the lock.
static double whistle_current_gain(void) {
  return whistle_fx && !whistle_passthrough &&
    !whistle_voice_sounds(whistle_voice) ? vocoder_gain : whistle_gain;
}

static void whistle_publish(void) {
  // The effects aren't the engine's: they're summed in beside whatever the
  // engine's playing, and apart from the whistle's own level.
  unsigned fx = whistle_passthrough ? 0 : whistle_fx;
  bool vocoder = fx != 0;
  atomic_store_explicit(&whistle_pub_fx, fx, memory_order_relaxed);
  // Whistling only needs keeping out of the effects while it's playing
  // something: a voice, or Whistle Breath.  Not with the whistle off, or its
  // voice silenced under them, or passing the input through.
  atomic_store_explicit(&whistle_pub_guard,
                        whistle_on && !whistle_passthrough &&
                          !whistle_voice_muted,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_fx_gate_db, whistle_fx_gate_db,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_fx_mic,
                        whistle_fx_mic && whistle_has_second_mic(),
                        memory_order_relaxed);
  int own = whistle_engine_voice[whistle_voice] == WHISTLE_BREATH_WHISTLE
    ? WHISTLE_BREATH_WHISTLE
    : whistle_blow_on ? WHISTLE_BREATH_BLOW : 0;
  bool breath = whistle_on && !whistle_passthrough && own;
  atomic_store_explicit(&whistle_pub_breath, breath ? own : 0,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_breath_level_full,
                        own == WHISTLE_BREATH_WHISTLE
                          ? whistle_breath_level_full : whistle_blow_level_full,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_blow_gate, whistle_blow_gate,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_voice,
                        whistle_passthrough
                          ? 0 : whistle_engine_voice[whistle_voice],
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_volume, whistle_volume,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_octave, whistle_octave,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_gate,
                        breath && own == WHISTLE_BREATH_WHISTLE
                          ? whistle_breath_gate : whistle_gate,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_level_full, whistle_level_full,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_low_note, whistle_low_note,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_high_note, whistle_high_note,
                        memory_order_relaxed);
  bool sounds = whistle_passthrough || whistle_voice_sounds(whistle_voice);
  double gain = whistle_gain;
  if (!whistle_passthrough &&
      WHISTLE_VOICES[whistle_voice].own == WHISTLE_PASS_THROUGH) {
    gain *= WHISTLE_PASS_THROUGH_GAIN;
  }
  atomic_store_explicit(&whistle_pub_target_gain,
                        whistle_on && sounds ? (int)(gain * 1000 + 0.5) : 0,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_vocoder_gain,
                        whistle_on && vocoder
                          ? (int)(vocoder_gain * 1000 + 0.5) : 0,
                        memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The ring
//
// Single producer (the input device's thread), single consumer (the thread
// fluidsynth's output driver calls us on).  Two devices means two clocks even
// when they are the same interface, because the two streams are separate
// clients with their own callbacks.
//
// This is only tolerable because of how the engine is built: the synth
// free-runs and never reads the input, so a resynced sample perturbs the pitch
// detector for one analysis window and never reaches the output directly.
// ---------------------------------------------------------------------------

#define WHISTLE_RING_FRAMES 16384u   // power of two, for the mask

// The first input, which the whistle, the breath voices and speech listen
// to, and the second, if the device has one, which the vocal effects can
// take instead (whistle_fx_mic).  One ring for both, so they stay in step.
static float whistle_ring[WHISTLE_RING_FRAMES];
static float whistle_ring_2[WHISTLE_RING_FRAMES];
static _Atomic unsigned whistle_ring_write;
static _Atomic unsigned whistle_ring_read;
static _Atomic unsigned whistle_ring_target;   // frames to keep buffered

static unsigned whistle_ring_fill(void) {
  unsigned write = atomic_load_explicit(&whistle_ring_write,
                                        memory_order_acquire);
  unsigned read = atomic_load_explicit(&whistle_ring_read,
                                       memory_order_relaxed);
  return write - read;
}

static void whistle_ring_reset(void) {
  atomic_store_explicit(&whistle_ring_write, 0, memory_order_relaxed);
  atomic_store_explicit(&whistle_ring_read, 0, memory_order_relaxed);
  memset(whistle_ring, 0, sizeof(whistle_ring));
  memset(whistle_ring_2, 0, sizeof(whistle_ring_2));
}

// Anything else that wants to hear the microphone -- speech.h, for spoken
// chord numbers -- sets this.  Called on the input device's thread with every
// block, so it must be realtime safe: no locks, no allocation.
static void (*whistle_input_tap)(const float* samples, int frames) = NULL;

// Called on the input device's thread.  `second` is the device's second
// input, or NULL if it has none.
static void whistle_push_input(const float* samples, const float* second,
                               int frames) {
  if (whistle_input_tap) whistle_input_tap(samples, frames);
  unsigned write = atomic_load_explicit(&whistle_ring_write,
                                        memory_order_relaxed);
  unsigned read = atomic_load_explicit(&whistle_ring_read,
                                       memory_order_acquire);
  unsigned space = WHISTLE_RING_FRAMES - (write - read) - 1;
  if ((unsigned)frames > space) {
    // The consumer has stalled.  Drop this block rather than the whole
    // history: the detector recovers within an analysis window.
    atomic_fetch_add_explicit(&whistle_dropouts, 1, memory_order_relaxed);
    return;
  }
  for (int i = 0; i < frames; i++) {
    unsigned at = (write + (unsigned)i) & (WHISTLE_RING_FRAMES - 1);
    whistle_ring[at] = samples[i];
    whistle_ring_2[at] = second ? second[i] : 0;
  }
  atomic_store_explicit(&whistle_ring_write, write + (unsigned)frames,
                        memory_order_release);
}

static void whistle_pop_input(float* samples, float* second, int frames) {
  unsigned read = atomic_load_explicit(&whistle_ring_read,
                                       memory_order_relaxed);
  unsigned write = atomic_load_explicit(&whistle_ring_write,
                                        memory_order_acquire);
  unsigned available = write - read;
  unsigned target = atomic_load_explicit(&whistle_ring_target,
                                         memory_order_relaxed);

  // Two clocks that drift means the backlog wanders.  Left alone it ends up
  // either empty or arbitrarily deep, so pull it back towards the target.
  if (target > 0 && available > target * 3) {
    unsigned skip = available - target;
    read += skip;
    available -= skip;
    atomic_fetch_add_explicit(&whistle_dropouts, 1, memory_order_relaxed);
  }

  for (int i = 0; i < frames; i++) {
    unsigned at = (read + (unsigned)i) & (WHISTLE_RING_FRAMES - 1);
    bool there = (unsigned)i < available;  // or the input hasn't caught up
    samples[i] = there ? whistle_ring[at] : 0;
    second[i] = there ? whistle_ring_2[at] : 0;
  }
  if ((unsigned)frames > available) {
    atomic_fetch_add_explicit(&whistle_dropouts, 1, memory_order_relaxed);
    read += available;
  } else {
    read += (unsigned)frames;
  }
  atomic_store_explicit(&whistle_ring_read, read, memory_order_release);
}

// ---------------------------------------------------------------------------
// The engine, on the audio thread
// ---------------------------------------------------------------------------

// Touched by the audio thread once running, and by the setup thread when it
// isn't.  whistle_engine_live is what says which of those is the case -- and
// whistle_mix_busy is what makes "isn't" true rather than merely likely:
// clearing the flag doesn't help if the audio thread is already past the
// check and inside the mix, so the setup thread waits for it to come out
// before touching anything the mix reads.
static struct Engine whistle_engine;
static _Atomic int whistle_engine_live;
static _Atomic int whistle_mix_busy;

// Stop the mix and wait until the audio thread is demonstrably out of it.
// Bounded: a driver that has stopped calling us at all must not hang the UI,
// and the wait is only ever one block long in practice.
static void whistle_engine_silence(void) {
  atomic_store_explicit(&whistle_engine_live, 0, memory_order_release);
  // Nothing's listening any more, so nothing's breathing.
  atomic_store_explicit(&whistle_breath_cc, -1, memory_order_relaxed);
  for (int wait = 0; wait < 200; wait++) {
    if (!atomic_load_explicit(&whistle_mix_busy, memory_order_acquire)) return;
    usleep(1000);
  }
  printf("whistle: the audio thread didn't come out of the mix\n");
}

static int whistle_applied_voice = -1;
static int whistle_applied_volume = -1;
static int whistle_applied_octave = 0x7fffffff;
static int whistle_applied_gate = -1;
static int whistle_applied_level_full = -1;
static int whistle_applied_low = -1;
static int whistle_applied_high = -1;

// The breath voices' detector, and which of them it was last listening for:
// a change starts it afresh, so neither inherits the other's breath.
static BreathMic whistle_breath_mic;
static int whistle_applied_breath;

// A gain the audio thread ramps towards what the UI last asked for.
typedef struct {
  float live;    // the audio thread's own
  float step;    // per-sample slew towards the target
  float target;  // where the slew is heading
} WhistleRamp;

// The whistle's, and each effect's, so each fades in and out on its own.
static WhistleRamp whistle_ramp, fx_ramp[N_VFX];

// The whistle guard: whistled notes don't go through the vocal effects when
// the effects are on the whistle's own input and the whistle is playing a
// voice, or breathing.  While the pitch detector is
// sure enough that it's hearing a whistle -- committed to a note, or its
// confidence over WHISTLE_GUARD_CONFIDENCE, which gets there a median 4ms
// into a note where committing takes 13-22ms -- the effects' input fades out
// over about a millisecond, and stays out WHISTLE_GUARD_HOLD_S after, for a
// note's tail and the confidence's flicker.  And the effects hear their
// input WHISTLE_GUARD_LOOKAHEAD_S late, so the guard is mostly down before a
// note's first sound reaches them -- always, guarding or not, so the guard
// coming and going doesn't jump them.  Speech is over the confidence for under
// 1% of the time it's loud, in the kept number clips.
#define WHISTLE_GUARD_CONFIDENCE 0.8
#define WHISTLE_GUARD_HOLD_S 0.080
#define WHISTLE_GUARD_LOOKAHEAD_S 0.005
#define WHISTLE_GUARD_FRAMES 1024   // the lookahead's line, at up to 192kHz

typedef struct {
  float line[WHISTLE_GUARD_FRAMES];
  int pos, lookahead, hold, held;
  double gain, down, up;
} WhistleGuard;

static WhistleGuard whistle_guard;

static void whistle_guard_init(WhistleGuard* g, double rate) {
  memset(g, 0, sizeof(*g));
  g->lookahead = (int)(rate * WHISTLE_GUARD_LOOKAHEAD_S);
  if (g->lookahead >= WHISTLE_GUARD_FRAMES) {
    g->lookahead = WHISTLE_GUARD_FRAMES - 1;
  }
  g->hold = (int)(rate * WHISTLE_GUARD_HOLD_S);
  g->gain = 1;
  g->down = 1 - exp(-1 / (rate * 0.0003));
  g->up = 1 - exp(-1 / (rate * 0.010));
}

// One sample of the effects' input in, `whistling` if the detector hears a
// whistle now; the input out, lookahead late, and silent around whistles.
static float whistle_guard_run(WhistleGuard* g, float in, bool whistling) {
  if (whistling) {
    g->held = g->hold;
  } else if (g->held > 0) {
    g->held--;
  }
  double target = g->held > 0 ? 0 : 1;
  g->gain += (target - g->gain) * (target < g->gain ? g->down : g->up);
  g->line[g->pos] = in;
  int back = (g->pos - g->lookahead + WHISTLE_GUARD_FRAMES) %
             WHISTLE_GUARD_FRAMES;
  g->pos = (g->pos + 1) % WHISTLE_GUARD_FRAMES;
  return (float)(g->line[back] * g->gain);
}

static void whistle_ramp_to(WhistleRamp* r, float target, float frames) {
  r->target = target;
  if (fabsf(target - r->live) < 1e-6f) {
    r->live = target;
    r->step = 0;
  } else {
    r->step = (target - r->live) / frames;
  }
}

static float whistle_ramp_next(WhistleRamp* r) {
  if (r->step != 0) {
    r->live += r->step;
    // Stop on the target rather than overshooting it, whichever way the
    // ramp is going.
    if ((r->step > 0 && r->live >= r->target) ||
        (r->step < 0 && r->live <= r->target)) {
      r->live = r->target;
      r->step = 0;
    }
  }
  return r->live;
}

// A MIDI note in Hz, moved by `semitones` first.  The move is what makes the
// ends inclusive: half a semitone either way, so the note you named triggers
// whether you land 40 cents under it or over it.  Same as whistle-synth's own
// front end does it.
static float whistle_note_hz(int midi, float semitones) {
  if (midi <= 0) return 0;
  return 440.0f * exp2f(((float)midi - 69.0f + semitones) / 12.0f);
}

// Once per block, not once per sample.
static void whistle_apply_controls(int frames, double sample_rate) {
  int voice = atomic_load_explicit(&whistle_pub_voice, memory_order_relaxed);
  if (voice != whistle_applied_voice && voice >= 0) {
    whistle_applied_voice = voice;
    engine_set_voice(&whistle_engine, voice);
  }
  int volume = atomic_load_explicit(&whistle_pub_volume, memory_order_relaxed);
  if (volume != whistle_applied_volume) {
    whistle_applied_volume = volume;
    engine_set_volume(&whistle_engine, volume);
  }
  int octave = atomic_load_explicit(&whistle_pub_octave, memory_order_relaxed);
  if (octave != whistle_applied_octave) {
    whistle_applied_octave = octave;
    engine_set_octave(&whistle_engine, octave);
  }
  int gate = atomic_load_explicit(&whistle_pub_gate, memory_order_relaxed);
  if (gate != whistle_applied_gate) {
    whistle_applied_gate = gate;
    pitch_set_gate(&whistle_engine.detector,
                   (float)whistle_gate_margin(gate));
  }
  int level_full = atomic_load_explicit(&whistle_pub_level_full,
                                        memory_order_relaxed);
  if (level_full != whistle_applied_level_full) {
    whistle_applied_level_full = level_full;
    engine_set_level_full(&whistle_engine, level_full);
  }
  // The two ends of the range are applied as a pair: half a range is not a
  // state worth having for a block.
  int low = atomic_load_explicit(&whistle_pub_low_note, memory_order_relaxed);
  int high = atomic_load_explicit(&whistle_pub_high_note,
                                  memory_order_relaxed);
  if (low != whistle_applied_low || high != whistle_applied_high) {
    whistle_applied_low = low;
    whistle_applied_high = high;
    engine_set_range(&whistle_engine, whistle_note_hz(low, -0.5f),
                     whistle_note_hz(high, 0.5f));
  }

  // Reach a new gain in about 10ms however big the block is, so switching the
  // instrument off fades rather than clicks and the slider doesn't zipper.
  float ramp_frames = (float)(sample_rate * 0.010);
  if (ramp_frames < 1) ramp_frames = 1;
  whistle_ramp_to(&whistle_ramp, 0.001f * (float)atomic_load_explicit(
                    &whistle_pub_target_gain, memory_order_relaxed),
                  ramp_frames);
  unsigned fx = atomic_load_explicit(&whistle_pub_fx, memory_order_relaxed);
  float fx_gain = 0.001f * (float)atomic_load_explicit(
    &whistle_pub_vocoder_gain, memory_order_relaxed);
  for (int i = VFX_VOCODER; i < N_VFX; i++) {
    whistle_ramp_to(&fx_ramp[i], fx & VFX_BIT(i) ? fx_gain : 0, ramp_frames);
  }
  (void)frames;
}

static void mando_prepare(double rate);  // mandolin.h

// Build the engine for a stream about to start.  Called with no audio
// running, from the thread that drives the whistle's lifecycle.
static void whistle_engine_prepare(double sample_rate) {
  engine_init(&whistle_engine, (float)sample_rate);
  whistle_applied_voice = -1;
  whistle_applied_volume = -1;
  whistle_applied_octave = 0x7fffffff;
  whistle_applied_gate = -1;
  whistle_applied_level_full = -1;
  whistle_applied_low = -1;
  whistle_applied_high = -1;
  memset(&whistle_ramp, 0, sizeof(whistle_ramp));
  memset(fx_ramp, 0, sizeof(fx_ramp));
  whistle_guard_init(&whistle_guard, sample_rate);
  bm_init(&whistle_breath_mic, sample_rate);
  whistle_applied_breath = 0;
  whistle_ring_reset();
  mando_prepare(sample_rate);
}

// ---------------------------------------------------------------------------
// The vocoder
//
// The whistle's B voice: the microphone's spectrum, band by band, imposed on
// the chord the drones are playing -- saws on the root, third (when the feet
// or a voice have said which) and fifth -- with noise in the top bands for
// the consonants.
//
// Each note of the chord is played in six octaves at once, under a fixed
// bell over pitch, the way a Shepard tone is: the higher the chord, the more
// of it comes from the octave below.  So the chord's notes change and its
// register doesn't, and a change of chord never sounds like a jump up or
// down.  Whatever goes into the microphone comes
// out as the chord, the moment it goes in.  A gate that follows the room's
// noise floor keeps the band's own sound, in the microphone, from droning the
// chord on its own.
// ---------------------------------------------------------------------------

#define VOCODER_BANDS 16
#define VOCODER_LOW_HZ 150
#define VOCODER_HIGH_HZ 7000
#define VOCODER_Q 5
#define VOCODER_NOISE_FROM_HZ 4000
#define VOCODER_MAKEUP 1.5  // +6dB on where it was levelled, by ear
#define VOCODER_OCTAVES 6
#define VOCODER_VOICES (3 * VOCODER_OCTAVES)  // root, third, fifth
#define VOCODER_CENTER_HZ 180   // where the bell over pitch peaks
#define VOCODER_WIDTH 1.1       // and its width, in octaves

// The gate in front of the vocoder and the other vocal effects, which keeps
// the band in the microphone from playing them: the input's level against
// the room, which is the quietest it's been in the last ROOM_FLOOR_BLOCKS
// seconds.  Any pause between notes shows the room, a note held for less than
// that is never taken for it, and a room that gets louder is caught up with
// once the quieter seconds have passed.  A floor that crept up towards the
// input instead took a note held for four or five seconds for the room and
// cut it off.
//
// On top of that, a gate of the player's own (the Vocal FX menu): nothing
// quieter than room_gate_threshold opens it, however quiet the room.  That's
// what keeps the band out when it never stops playing, so the room never
// shows.
#define ROOM_FLOOR_BLOCKS 20

// The audio thread's copy, a linear peak level, from whistle_pub_fx_gate:
// where each RoomGate starts.  The whistle's effects follow it; the
// mandolin's have a gate of their own (mandolin.h).
static double room_gate_threshold = 0.002;

typedef struct {
  double rate;
  double level, floor, gate;
  double threshold;  // the player's gate, a linear peak level
  double attack, release;
  double block_min[ROOM_FLOOR_BLOCKS];  // each second's quietest
  double older_min;                     // the quietest of those
  double current_min;                   // and this second's, so far
  int block, block_frames;
} RoomGate;

static void room_gate_init(RoomGate* g, double rate) {
  memset(g, 0, sizeof(*g));
  g->rate = rate;
  g->threshold = room_gate_threshold;
  g->floor = 0.01;
  // Nothing heard yet, so nothing to hold the floor down but what comes in.
  for (int i = 0; i < ROOM_FLOOR_BLOCKS; i++) g->block_min[i] = 1;
  g->older_min = 1;
  g->current_min = 1;
  g->attack = 1 - exp(-1 / (rate * 0.002));
  g->release = 1 - exp(-1 / (rate * 0.025));
}

// One sample in, how open the gate is out, 0-1.  g->level is the input's
// level, followed quickly, for anything that wants to go by it.
static double room_gate_run(RoomGate* g, float in) {
  double level = fabs(in);
  g->level += (level - g->level) *
              (level > g->level ? g->attack : g->release);
  if (g->level < g->current_min) g->current_min = g->level;
  if (++g->block_frames >= (int)g->rate) {
    g->block_min[g->block] = g->current_min;
    g->block = (g->block + 1) % ROOM_FLOOR_BLOCKS;
    g->current_min = g->level;
    g->block_frames = 0;
    g->older_min = 1;
    for (int i = 0; i < ROOM_FLOOR_BLOCKS; i++) {
      if (g->block_min[i] < g->older_min) g->older_min = g->block_min[i];
    }
  }
  g->floor = fmin(g->current_min, g->older_min);
  bool open = g->level > 4 * g->floor && g->level > g->threshold;
  double step = open ? 1000 / (g->rate * 5) : 1000 / (g->rate * 60);
  g->gate = open ? fmin(1, g->gate + step) : fmax(0, g->gate - step);
  return g->gate;
}

typedef struct {
  double rate;
  Bandpass analysis[VOCODER_BANDS], synthesis[VOCODER_BANDS];
  double env[VOCODER_BANDS];
  double noise_mix[VOCODER_BANDS];
  double attack, release;
  double saw[VOCODER_VOICES];
  RoomGate room;
  uint32_t noise;
} Vocoder;

// The whistle's.  The mandolin has one of its own (mandolin.h).
static Vocoder vocoder;

static void vocoder_prepare(Vocoder* v, double sample_rate) {
  memset(v, 0, sizeof(*v));
  v->rate = sample_rate;
  v->noise = 987654321;
  room_gate_init(&v->room, sample_rate);
  for (int b = 0; b < VOCODER_BANDS; b++) {
    double hz = VOCODER_LOW_HZ *
      pow((double)VOCODER_HIGH_HZ / VOCODER_LOW_HZ,
          (double)b / (VOCODER_BANDS - 1));
    bandpass_set(&v->analysis[b], hz, VOCODER_Q, sample_rate);
    bandpass_set(&v->synthesis[b], hz, VOCODER_Q, sample_rate);
    v->noise_mix[b] = hz < VOCODER_NOISE_FROM_HZ ? 0.05 : 0.6;
  }
  v->attack = 1 - exp(-1 / (sample_rate * 0.002));
  v->release = 1 - exp(-1 / (sample_rate * 0.025));
}

static float vocoder_process(Vocoder* v, float in, const double* carrier_hz,
                             const double* carrier_weight, int n,
                             float volume) {
  double rate = v->rate;

  double gate = room_gate_run(&v->room, in);

  // The chord, the weights summing to one.
  double carrier = 0;
  for (int i = 0; i < n; i++) {
    double dt = carrier_hz[i] / rate;
    v->saw[i] += dt;
    if (v->saw[i] >= 1) v->saw[i] -= 1;
    carrier += carrier_weight[i] *
      (2 * v->saw[i] - 1 - poly_blep(v->saw[i], dt));
  }
  uint32_t x = v->noise;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  v->noise = x;
  double noise = (double)x / 2147483648.0 - 1;

  double out = 0;
  for (int b = 0; b < VOCODER_BANDS; b++) {
    double a = fabs(bandpass_run(&v->analysis[b], in));
    v->env[b] += (a - v->env[b]) * (a > v->env[b] ? v->attack : v->release);
    double m = v->noise_mix[b];
    double c = bandpass_run(&v->synthesis[b],
                            (1 - m) * carrier + m * noise);
    out += c * v->env[b];
  }
  // Half way to an even level: out goes as the square root of what comes in,
  // so a quiet microphone still reaches the mix and a loud one doesn't bury
  // it.
  out *= VOCODER_MAKEUP / sqrt(fmax(v->room.level, 0.01)) * gate;
  return (float)(tanh(out) * volume);
}

// The chord to vocode, from the music jammermidilib.h publishes: each of its
// notes in every octave from the bottom one up, weighted by the bell, the
// weights summing to one.  Returns how many.
static int vocoder_chord(double* hz, double* weight) {
  int root = atomic_load_explicit(&audio_chord_root, memory_order_relaxed);
  int third = atomic_load_explicit(&audio_chord_third, memory_order_relaxed);
  int fifth = atomic_load_explicit(&audio_chord_fifth, memory_order_relaxed);
  int notes[3], n_notes = 0;
  notes[n_notes++] = root;
  if (third) notes[n_notes++] = root + third;
  notes[n_notes++] = root + fifth;

  int n = 0;
  double total = 0;
  for (int i = 0; i < n_notes; i++) {
    int bottom = 24 + (notes[i] % 12);  // C1 to B1
    for (int octave = 0; octave < VOCODER_OCTAVES; octave++) {
      hz[n] = midi_hz(bottom + 12 * octave);
      double x = log2(hz[n] / VOCODER_CENTER_HZ) / VOCODER_WIDTH;
      weight[n] = exp(-0.5 * x * x);
      total += weight[n];
      n++;
    }
  }
  for (int i = 0; i < n; i++) weight[i] /= total;
  return n;
}

#include "voicefx.h"
#include "mandolin.h"

// Scratch for one block of input, sized when the stream starts.  Only the
// audio thread touches it while one is running.
static float* whistle_input_block;
static float* whistle_input_block_2;  // the second input's
static int whistle_input_block_frames;

// Sum the whistle into fluidsynth's output buffers.  Runs on the audio
// thread, after fluid_synth_process has filled them.  Realtime safe: no
// locks, no allocation, and it returns immediately when there is no engine.
static void whistle_mix(float** out, int nout, int len, double sample_rate) {
  if (!atomic_load_explicit(&whistle_engine_live, memory_order_acquire)) {
    return;
  }
  atomic_store_explicit(&whistle_mix_busy, 1, memory_order_release);
  // Re-read after claiming it: whistle_engine_silence may have cleared it
  // between the check above and the claim, and it is the claim it waits on.
  if (!atomic_load_explicit(&whistle_engine_live, memory_order_acquire) ||
      len > whistle_input_block_frames || nout < 1) {
    atomic_store_explicit(&whistle_mix_busy, 0, memory_order_release);
    return;
  }

  whistle_apply_controls(len, sample_rate);
  whistle_pop_input(whistle_input_block, whistle_input_block_2, len);
  // The mandolin, on the second input, for mando_add to put on the right.
  mando_process(whistle_input_block_2, len, sample_rate);
  // What the vocal effects hear: the first input, or the second.  On the
  // first, the whistle's, the guard keeps its whistled notes out of them.
  bool fx_on_second =
    atomic_load_explicit(&whistle_pub_fx_mic, memory_order_relaxed);
  bool guarding =
    atomic_load_explicit(&whistle_pub_guard, memory_order_relaxed);
  const float* fx_in = fx_on_second ? whistle_input_block_2
                                    : whistle_input_block;

  // The effects that might be heard: those on, and those still fading out.
  unsigned fx = 0;
  bool listen = false;
  for (int i = VFX_VOCODER; i < N_VFX; i++) {
    if (fx_ramp[i].live > 0 || fx_ramp[i].target > 0) {
      fx |= VFX_BIT(i);
      if (vfx_listens(i)) listen = true;
    }
  }
  if (vfx.rate != sample_rate) vfx_prepare(sample_rate);
  room_gate_threshold = pow(10, atomic_load_explicit(
    &whistle_pub_fx_gate_db, memory_order_relaxed) / 20.0);
  vocoder.room.threshold = room_gate_threshold;
  vfx.room.threshold = room_gate_threshold;
  float fx_peak = 0;
  VfxBlock fx_block;
  if (fx & ~VFX_BIT(VFX_VOCODER)) vfx_block(&fx_block);
  double chord_hz[VOCODER_VOICES], chord_weight[VOCODER_VOICES];
  int chord_notes = vocoder_chord(chord_hz, chord_weight);
  if (vocoder.rate != sample_rate) vocoder_prepare(&vocoder, sample_rate);
  float vocoder_volume = (float)(whistle_applied_volume + 1) / 10;
  int breathing = atomic_load_explicit(&whistle_pub_breath,
                                       memory_order_relaxed);
  if (breathing != whistle_applied_breath) {
    whistle_applied_breath = breathing;
    bm_init(&whistle_breath_mic, sample_rate);
  }
  bool blowing = breathing == WHISTLE_BREATH_BLOW;
  double breath_full = engine_level_full_for_step(
    atomic_load_explicit(&whistle_pub_breath_level_full,
                         memory_order_relaxed));
  double blow_gate = bm_blow_gate_for_step(
    atomic_load_explicit(&whistle_pub_blow_gate, memory_order_relaxed));
  double breath = 0, blow_level = 0;
  for (int i = 0; i < len; i++) {
    float left, right;
    engine_process_stereo(&whistle_engine, whistle_input_block[i],
                          &left, &right);
    if (breathing) {
      const struct PitchHint* h = &whistle_engine.detector.hint;
      float in = whistle_input_block[i];
      breath = breathing == WHISTLE_BREATH_WHISTLE
        ? bm_whistle(&whistle_breath_mic, h->voiced, h->level, breath_full)
        : bm_blow_noise(&whistle_breath_mic, in, blow_gate, breath_full);
      if (whistle_breath_mic.level > blow_level) {
        blow_level = whistle_breath_mic.level;
      }
      // Whistle Breath plays nothing: the whistle is for the breath
      // controller, not the audience.  Blow Noise goes on over a voice that
      // does play, so that plays on.
      if (breathing == WHISTLE_BREATH_WHISTLE) left = right = 0;
    }
    float gain = whistle_ramp_next(&whistle_ramp);
    left *= gain;
    right *= gain;
    // Each effect runs whenever it might be heard, including while it fades
    // out, and they're summed in beside the voice.
    if (fx) {
      if (fabsf(fx_in[i]) > fx_peak) fx_peak = fabsf(fx_in[i]);
      const struct PitchHint* h = &whistle_engine.detector.hint;
      float in = fx_on_second ? fx_in[i] : whistle_guard_run(
        &whistle_guard, fx_in[i],
        guarding &&
          (h->voiced || h->confidence > WHISTLE_GUARD_CONFIDENCE));
      float sum = 0;
      if (fx & VFX_BIT(VFX_VOCODER)) {
        sum += whistle_ramp_next(&fx_ramp[VFX_VOCODER]) *
          vocoder_process(&vocoder, in, chord_hz, chord_weight, chord_notes,
                          vocoder_volume);
      }
      if (fx & ~VFX_BIT(VFX_VOCODER)) {
        VfxInput v = vfx_input(in, listen);
        for (int e = VFX_VOCODER + 1; e < N_VFX; e++) {
          if (!(fx & VFX_BIT(e))) continue;
          sum += whistle_ramp_next(&fx_ramp[e]) * vocoder_volume *
                 vfx_effect(e, v, &fx_block);
        }
      }
      left += sum;
      right += sum;
    }
    // All of it on the left, folded to mono, the way fluidsynth's endpoints
    // are panned unless CHANNEL SWAP moves them.
    out[0][i] += 0.5f * (left + right);
  }
  if (fx) {
    int scaled = (int)(fx_peak * 10000);
    if (scaled > atomic_load_explicit(&whistle_meter_fx_level,
                                      memory_order_relaxed)) {
      atomic_store_explicit(&whistle_meter_fx_level, scaled,
                            memory_order_relaxed);
    }
  }
  atomic_store_explicit(&whistle_breath_cc,
                        breathing ? bm_cc(breath) : -1, memory_order_relaxed);

  // Meters.  The playing level is an RMS over the analysis window while a
  // note was sounding, which is the number level_full is compared against --
  // the one to read when setting that knob.
  // Merged into a running maximum rather than stored: this runs once per
  // audio block, and engine_take_peak_level resets the engine's peak every
  // time it is called, so storing it outright would publish whichever ~10ms
  // block the UI happened to read last.  The UI drains this instead, so what
  // it shows is the peak since it last looked.
  // The Blows' is the microphone's own level, since that's what their
  // full-blow knob is against.
  float peak = engine_take_peak_level(&whistle_engine);
  if (blowing) peak = (float)blow_level;
  if (peak > 0) {
    int scaled = (int)(peak * 10000);
    int seen = atomic_load_explicit(&whistle_meter_level, memory_order_relaxed);
    if (scaled > seen) {
      atomic_store_explicit(&whistle_meter_level, scaled, memory_order_relaxed);
    }
  }
  const struct PitchHint* hint = &whistle_engine.detector.hint;
  atomic_store_explicit(&whistle_meter_voiced, hint->voiced ? 1 : 0,
                        memory_order_relaxed);
  if (hint->voiced) {
    atomic_store_explicit(&whistle_meter_freq, (int)(hint->freq * 100),
                          memory_order_relaxed);
  }

  atomic_store_explicit(&whistle_mix_busy, 0, memory_order_release);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

// Turn the ten preset names into the voice numbers engine_set_voice takes.
// A name that isn't in the soundfont-equivalent -- the presets table -- is
// reported rather than silently mapped to something else, since a key that
// plays the wrong instrument is worse than a key that says it's broken.
static bool whistle_resolve_voices(void) {
  bool all_found = true;
  int count = synth_preset_count();
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    whistle_engine_voice[i] = -1;
    if (WHISTLE_VOICES[i].own == WHISTLE_PASS_THROUGH) {
      whistle_engine_voice[i] = 0;  // the engine's raw input
      continue;
    }
    if (WHISTLE_VOICES[i].own) {
      whistle_engine_voice[i] = WHISTLE_VOICES[i].own;
      continue;
    }
    for (int preset = 0; preset < count; preset++) {
      if (strcmp(synth_preset_name(preset), WHISTLE_VOICES[i].preset) == 0) {
        whistle_engine_voice[i] = preset + 1;   // 0 is the raw input
        break;
      }
    }
    if (whistle_engine_voice[i] < 0) {
      printf("whistle: no preset named \"%s\"; the %s key will be silent\n",
             WHISTLE_VOICES[i].preset, WHISTLE_VOICES[i].label);
      all_found = false;
    }
  }
  // The mandolin plays the whistle's Bass.
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    if (WHISTLE_VOICES[i].preset && whistle_engine_voice[i] > 0 &&
        strcmp(WHISTLE_VOICES[i].preset, "bass") == 0) {
      mando_bass_engine_voice = whistle_engine_voice[i];
    }
  }
  return all_found;
}

// ---------------------------------------------------------------------------
// What the keys do
//
// All of these are called on the main thread with jammer_lock held.
// ---------------------------------------------------------------------------

static void whistle_toggle(void) {
  whistle_on = !whistle_on;
  whistle_publish();
}

static void whistle_select(void) {
  whistle_selected = true;
}

// These effects over the voice, a VFX_BIT each.  A voice silenced under
// them stays silenced when they go.
static void whistle_choose_fx(unsigned fx) {
  whistle_fx = fx;
  whistle_publish();
}

// An effect's key, or its menu item: that effect on, or off, beside the rest.
static void whistle_set_fx(int fx) {
  whistle_choose_fx(whistle_fx ^ VFX_BIT(fx));
}

// What F2 says while the whistle's selected.
static const char* whistle_fx_mic_label(void) {
  return whistle_fx_mic ? "FX MIC\n2" : "FX MIC\n1";
}

// The vocal effects onto input `mic`, 0 or 1, if there is one.
static void whistle_choose_fx_mic(int mic) {
  if (mic && !whistle_has_second_mic()) return;
  whistle_fx_mic = mic;
  whistle_publish();
}

// F2: the vocal effects over to the other input.
static void whistle_swap_fx_mic(void) {
  whistle_choose_fx_mic(!whistle_fx_mic);
}

static void whistle_set_voice(int index) {
  if (index < 0 || index >= N_WHISTLE_VOICES) return;
  // M is Blow Noise's layer, on and off over the voice, not a voice.
  if (WHISTLE_VOICES[index].own == WHISTLE_BREATH_BLOW) {
    whistle_blow_on = !whistle_blow_on;
    whistle_publish();
    return;
  }
  // The lit voice again, under the vocoder, silences it.  Not the breath
  // voices, which are silent already and have M's own second press.
  if (index == whistle_voice && whistle_fx && !whistle_voice_muted &&
      whistle_voice_sounds(index)) {
    whistle_voice_muted = true;
    whistle_publish();
    return;
  }
  whistle_voice_muted = false;
  whistle_voice = index;
  whistle_publish();
}

static void whistle_bump_octave(int delta) {
  whistle_octave += delta;
  if (whistle_octave > SYNTH_OCTAVE_SHIFT) whistle_octave = SYNTH_OCTAVE_SHIFT;
  if (whistle_octave < -SYNTH_OCTAVE_SHIFT) {
    whistle_octave = -SYNTH_OCTAVE_SHIFT;
  }
  whistle_publish();
}

static void whistle_bump_volume(int delta) {
  whistle_volume += delta;
  if (whistle_volume > 9) whistle_volume = 9;
  if (whistle_volume < 0) whistle_volume = 0;
  whistle_publish();
}

// What esc does to the rest of the rig, done to the whistle: back to the
// voice, octave and volume it starts at.  The setup knobs in the menu -- gate,
// level, range, the master level and the input device -- describe the room and
// the microphone rather than the tune, so a reset leaves them alone.
static void whistle_reset(void) {
  whistle_on = false;
  whistle_selected = false;
  whistle_voice = 0;
  whistle_fx = 0;
  whistle_voice_muted = false;
  whistle_blow_on = false;
  whistle_octave = 0;
  whistle_volume = WHISTLE_VOLUME_DEFAULT;
  whistle_publish();
}

// Pass what the breath voices hear on to everything the breath controller
// drives, as if it had come from one: on the tick thread, a change at a time,
// and a last 0 when they stop so that nothing is left breathing.  A real
// breath controller plugged in alongside still works; the two just take
// turns.  Caller must hold the lock.
static int whistle_breath_sent = -1;
static void whistle_breath_tick(void) {
  int cc = atomic_load_explicit(&whistle_breath_cc, memory_order_relaxed);
  if (cc == whistle_breath_sent) return;
  if (cc < 0 && whistle_breath_sent > 0) handle_cc(CC_BREATH, 0);
  if (cc >= 0) handle_cc(CC_BREATH, (unsigned)cc);
  whistle_breath_sent = cc;
}

static void whistle_init_state(void) {
  whistle_voice = 0;
  whistle_fx = 0;
  whistle_voice_muted = false;
  whistle_blow_on = false;
  whistle_volume = WHISTLE_VOLUME_DEFAULT;
  whistle_octave = 0;
  whistle_on = false;
  whistle_selected = false;
  whistle_passthrough = false;
  whistle_low_note = whistle_default_low_note();
  whistle_high_note = whistle_default_high_note();
}

#endif
