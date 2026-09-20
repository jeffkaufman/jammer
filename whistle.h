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
// is already running.
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

// ---------------------------------------------------------------------------
// The voices
//
// Ten of whistle-synth's presets, on the ten voice keys that the drum kits
// don't use.  Looked up by name at startup rather than by index: the presets
// table has had entries come and go, and an index that silently shifted would
// put a different instrument under every key.
// ---------------------------------------------------------------------------

#define N_WHISTLE_VOICES 10

typedef struct {
  int note;            // the voice key's pseudo-note, as KEYS[] sends it
  const char* preset;  // what synth.c calls it
  const char* label;   // what to draw on the key; "\n" splits lines
} WhistleVoice;

static const WhistleVoice WHISTLE_VOICES[N_WHISTLE_VOICES] = {
  {'A', "bass",           "Bass"},
  {'S', "octaveless",     "Octave\nless"},
  {'D', "reese",          "Reese"},
  {'F', "eight-oh-eight", "808"},
  {'G', "fm",             "FM"},
  {'H', "fm-sub",         "FM\nSub"},
  {'Z', "square",         "Square"},
  {'X', "drawbar",        "Draw\nbar"},
  {'C', "drawbar-hi",     "Draw\nbar Hi"},
  {'V', "accordion",      "Accor\ndion"},
};

// What engine_set_voice() takes for each of the ten, filled in by
// whistle_resolve_voices().  Voice 0 in the engine is the raw input passed
// through, so presets start at 1.
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

// 0-9, what counts as blowing full tilt.  whistle-synth's own app starts this
// at 5, which is 0.22 -- a vocal mic at the lip.  Against fluidsynth that
// costs 7dB even with a good microphone, because the voice spends the
// player's breath on loudness and a level_full it never reaches leaves the
// voice permanently dark and quiet.  Measured (./whistlelevels), the bass
// voice against the foot bass as jml_setup leaves it:
//
//     input peak      step 0   step 2   step 5
//     0.300            +1.9     +1.8     +1.4
//     0.100            +1.8     +0.9     -7.1
//     0.030            -3.0     -8.5    -17.0
//
// 2 rather than 0 because the bottom of the knob is not free: level_full is
// what the voice's dynamics are measured against, so setting it under what
// the player actually delivers leaves them permanently maxed out with no
// dynamics left.  2 matches a strong whistle into a decent microphone.  It is
// a per-rig number either way -- set it just above what the status row shows
// while you whistle hard.
static int whistle_level_full = 2;
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

static void whistle_publish(void) {
  atomic_store_explicit(&whistle_pub_voice,
                        whistle_passthrough
                          ? 0 : whistle_engine_voice[whistle_voice],
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_volume, whistle_volume,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_octave, whistle_octave,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_gate, whistle_gate, memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_level_full, whistle_level_full,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_low_note, whistle_low_note,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_high_note, whistle_high_note,
                        memory_order_relaxed);
  atomic_store_explicit(&whistle_pub_target_gain,
                        whistle_on ? (int)(whistle_gain * 1000 + 0.5) : 0,
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

static float whistle_ring[WHISTLE_RING_FRAMES];
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
}

// Called on the input device's thread.
static void whistle_push_input(const float* samples, int frames) {
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
    whistle_ring[(write + (unsigned)i) & (WHISTLE_RING_FRAMES - 1)] =
      samples[i];
  }
  atomic_store_explicit(&whistle_ring_write, write + (unsigned)frames,
                        memory_order_release);
}

static void whistle_pop_input(float* samples, int frames) {
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
    samples[i] = (unsigned)i < available
      ? whistle_ring[(read + (unsigned)i) & (WHISTLE_RING_FRAMES - 1)]
      : 0;   // the input hasn't caught up yet
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

static float whistle_live_gain;    // the audio thread's own, ramped
static float whistle_gain_step;    // per-sample slew towards the target
static float whistle_gain_target;  // where the slew is heading

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
    engine_set_gate(&whistle_engine, gate);
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
  whistle_gain_target = 0.001f * (float)atomic_load_explicit(
      &whistle_pub_target_gain, memory_order_relaxed);
  float ramp_frames = (float)(sample_rate * 0.010);
  if (ramp_frames < 1) ramp_frames = 1;
  if (fabsf(whistle_gain_target - whistle_live_gain) < 1e-6f) {
    whistle_live_gain = whistle_gain_target;
    whistle_gain_step = 0;
  } else {
    whistle_gain_step = (whistle_gain_target - whistle_live_gain) / ramp_frames;
  }
  (void)frames;
}

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
  whistle_live_gain = 0;
  whistle_gain_step = 0;
  whistle_ring_reset();
}

// Scratch for one block of input, sized when the stream starts.  Only the
// audio thread touches it while one is running.
static float* whistle_input_block;
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
  whistle_pop_input(whistle_input_block, len);

  float gain = whistle_live_gain;
  float step = whistle_gain_step;
  float target = whistle_gain_target;
  for (int i = 0; i < len; i++) {
    float left, right;
    engine_process_stereo(&whistle_engine, whistle_input_block[i],
                          &left, &right);
    if (step != 0) {
      gain += step;
      // Stop on the target rather than overshooting it, whichever way the
      // ramp is going.
      if ((step > 0 && gain >= target) || (step < 0 && gain <= target)) {
        gain = target;
        step = 0;
      }
    }
    out[0][i] += left * gain;
    if (nout > 1) out[1][i] += right * gain;
  }
  whistle_live_gain = gain;
  whistle_gain_step = step;

  // Meters.  The playing level is an RMS over the analysis window while a
  // note was sounding, which is the number level_full is compared against --
  // the one to read when setting that knob.
  // Merged into a running maximum rather than stored: this runs once per
  // audio block, and engine_take_peak_level resets the engine's peak every
  // time it is called, so storing it outright would publish whichever ~10ms
  // block the UI happened to read last.  The UI drains this instead, so what
  // it shows is the peak since it last looked.
  float peak = engine_take_peak_level(&whistle_engine);
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

static void whistle_set_voice(int index) {
  if (index < 0 || index >= N_WHISTLE_VOICES) return;
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
  whistle_octave = 0;
  whistle_volume = WHISTLE_VOLUME_DEFAULT;
  whistle_publish();
}

static void whistle_init_state(void) {
  whistle_voice = 0;
  whistle_volume = WHISTLE_VOLUME_DEFAULT;
  whistle_octave = 0;
  whistle_on = false;
  whistle_selected = false;
  whistle_passthrough = false;
  whistle_low_note = whistle_default_low_note();
  whistle_high_note = whistle_default_high_note();
}

#endif
