#ifndef JML_MAC_API_H
#define JML_MAC_API_H

// Mac equivalent of linuxapi.h.
//
// On the Pi we send MIDI out over ALSA sequencer ports to a separate
// fluidsynth process.  Here we link libfluidsynth directly and call it
// in-process, so there's one app to launch and no MIDI routing to get wrong.
// Same soundfont, so the same voice/volume tuning applies.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <fluidsynth.h>
#include <stdatomic.h>

// Its own channel here, rather than the drum channel's: see Kick Duck below.
// Mirrors the drum channel's program and controllers, so it sounds the same.
// Past the sixteen channels the endpoints and pitched kick use, so the synth
// has thirty-two (start_synth).
#define CHANNEL_KICK 16
// And the Breath Gate's brushes and the drum's Grid Hat, past the drones'
// voice channels, below.
#define CHANNEL_BRUSH 32
#define CHANNEL_HAT 33

#include "common.h"

// Voice channels: three spare channels for each drone, past the kick's, so a
// voice-led drone (jammermidilib.h's voice_lead) can play each of its notes on
// a channel of its own and bend it on its own -- a pitch bend is a channel's,
// not a note's.  They mirror their drone's program and controllers
// (send_midi, choose_voice) and are mixed as their drone is.  The Pi's
// fluidsynth has only sixteen channels, so there the drones play as they
// always did.
#define VOICE_CHANNELS_PER_DRONE 3
#define VOICE_BEND_RANGE 12  // semitones each way

// The first of a drone's voice channels, or -1 if it isn't a drone.
static int voice_channel_base(int endpoint) {
  switch (endpoint) {
  case ENDPOINT_DRONE_BASS:    return 17;
  case ENDPOINT_DRONE_CHORD:   return 20;
  case ENDPOINT_DRONE_BASS_2:  return 23;
  case ENDPOINT_DRONE_CHORD_2: return 26;
  case ENDPOINT_BREATH:        return 29;
  }
  return -1;
}

// The endpoint a channel belongs to: its own, for the endpoints, or the
// drone's, for a voice channel.
static int channel_endpoint(int channel) {
  static const int DRONES[] = {
    ENDPOINT_DRONE_BASS, ENDPOINT_DRONE_CHORD, ENDPOINT_DRONE_BASS_2,
    ENDPOINT_DRONE_CHORD_2, ENDPOINT_BREATH,
  };
  for (int i = 0; i < 5; i++) {
    int base = voice_channel_base(DRONES[i]);
    if (channel >= base && channel < base + VOICE_CHANNELS_PER_DRONE) {
      return DRONES[i];
    }
  }
  return channel;
}

int attempt(int result, char* errmsg) {
  if (result < 0) {
    perror("");
    die(errmsg);
  }
  return result;
}

uint64_t now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

// Which CoreAudio device to play through.  "default" follows the system
// output, which is usually the wrong thing for a live rig -- on the Pi
// run-fluidsynth.sh picks the USB interface explicitly, and this is the
// equivalent.
#define AUDIO_DEVICE_SETTING "audio.coreaudio.device"
char audio_device[256] = "default";

// The Pi passes fluidsynth -c 2 -z 64: two ALSA periods of 64 frames.  Neither
// translates to CoreAudio.  Setting audio.periods at all makes the driver open
// happily and then never ask us for a single sample -- no error, no sound --
// and it's device-dependent, so it can look fine on one output and be silent
// on the next.  Leave the buffering to fluidsynth unless told otherwise, and
// use audio_is_flowing() to catch it if an override misbehaves.

// Bumped by the render callback, so we can tell "device open" from "device
// actually playing".
static volatile uint64_t audio_frames_rendered = 0;

// Anything else that wants to be heard through jammer's output device, summed
// into fluidsynth's buffers after it has filled them.  This is how the whistle
// bass gets out without a second audio device -- see whistle.h.  NULL in the
// tools that include this header for the synth alone.
//
// Called on the audio thread, so whatever is behind it must not lock or
// allocate.
static void (*audio_mix_hook)(float** out, int nout, int len,
                              double sample_rate) = NULL;

// And one more, after the right channel's level (alt_channel_gain) has been
// applied, for what that level is matched against rather than moved by: the
// mandolin (mandolin.h).  Same rules.
static void (*audio_after_alt_hook)(float** out, int nout, int len) = NULL;

// What the synth is running at, which is also what any mixed-in engine has to
// run at.  Set before start_synth to ask for something other than fluidsynth's
// own default.
double synth_sample_rate = 44100;

// How big a block fluidsynth's driver asks for.  Read by the whistle, which
// has to keep at least one of these buffered to hand it -- see
// whistle_input_start.  Set here because this is the only place that knows.
static volatile int audio_block_frames = 0;

// ---------------------------------------------------------------------------
// The breath
//
// The breath controller and which BREATH_FX_* are on (common.h), from
// jammermidilib.h's breath_hook, under jammer's lock.  Read on the audio
// thread by the breath gate, the sweeps and the breath instruments below.
// ---------------------------------------------------------------------------

static _Atomic int audio_breath;
static _Atomic unsigned audio_breath_fx;
// The Breath Gate's -/+, from breath_gain_hook: its layers' level.
static _Atomic float audio_breath_gain = 1.0f;

static void pan_breath_kits(bool right);

static void breath_gain_set(double gain) {
  atomic_store_explicit(&audio_breath_gain, (float)gain, memory_order_relaxed);
}

static void breath_set(int breath, unsigned fx) {
  atomic_store_explicit(&audio_breath, breath, memory_order_relaxed);
  atomic_store_explicit(&audio_breath_fx, fx, memory_order_relaxed);
  pan_breath_kits(fx & BREATH_FX_RIGHT);
}

// ---------------------------------------------------------------------------
// The music
//
// The bass note, the chord and the beat, from jammermidilib.h's music_hook
// every tick, for the Mac's own sounds that follow them: the trance gate, the
// Breath Gate's wobble and sub drop, and the whistle's vocoder.
// ---------------------------------------------------------------------------

static _Atomic int audio_bass_note = 26;
static _Atomic int audio_chord_root = 26;
static _Atomic int audio_chord_third;
static _Atomic int audio_chord_fifth = 7;
static _Atomic uint64_t audio_beat_start_ns;
static _Atomic uint64_t audio_beat_ns;
static _Atomic bool audio_jig;
static _Atomic unsigned char audio_trance_gate[N_ENDPOINTS];
static _Atomic unsigned char audio_gate_steps[6];
static _Atomic int audio_n_gate_steps;

static void music_set(const MusicState* m) {
  atomic_store_explicit(&audio_bass_note, m->bass_note, memory_order_relaxed);
  atomic_store_explicit(&audio_chord_root, m->chord_root,
                        memory_order_relaxed);
  atomic_store_explicit(&audio_chord_third, m->chord_third,
                        memory_order_relaxed);
  atomic_store_explicit(&audio_chord_fifth, m->chord_fifth,
                        memory_order_relaxed);
  atomic_store_explicit(&audio_beat_start_ns, m->beat_start_ns,
                        memory_order_relaxed);
  atomic_store_explicit(&audio_beat_ns, m->beat_ns, memory_order_relaxed);
  atomic_store_explicit(&audio_jig, m->jig, memory_order_relaxed);
  for (int i = 0; i < m->n_gate_steps; i++) {
    atomic_store_explicit(&audio_gate_steps[i], m->gate_steps[i],
                          memory_order_relaxed);
  }
  atomic_store_explicit(&audio_n_gate_steps, m->n_gate_steps,
                        memory_order_relaxed);
  for (int i = 0; i < N_ENDPOINTS; i++) {
    atomic_store_explicit(&audio_trance_gate[i], m->trance_gate[i],
                          memory_order_relaxed);
  }
}

static double midi_hz(double note) {
  return 440 * pow(2, (note - 69) / 12);
}

// When the block being rendered starts, on now()'s clock.  MIDI that arrives
// now is heard at the start of the next block, so this is the clock the beat
// is on as far as what's rendered goes.
static uint64_t audio_block_ns;

// How far into the beat the last pedal hit started time t is, 0-1, or -1 if
// it's outside it: no tempo, or the feet have stopped.  For what has to stop
// when they do.
static double audio_beat_phase(uint64_t t) {
  uint64_t start = atomic_load_explicit(&audio_beat_start_ns,
                                        memory_order_relaxed);
  uint64_t beat = atomic_load_explicit(&audio_beat_ns, memory_order_relaxed);
  if (beat == 0 || start == 0 || t < start) return -1;
  double phase = (double)(t - start) / beat;
  return phase < 1 ? phase : -1;
}

// For what only goes as long as a breath does, and so can carry on at the
// pedals' tempo after they stop: beats since the pedals' last beat, if they
// kept time within the last couple, and otherwise since `origin` at 116 BPM.
#define AUDIO_DEFAULT_BEAT_NS (60 * 1000000000ULL / 116)
static double audio_grid_beats(uint64_t t, uint64_t origin) {
  uint64_t start = atomic_load_explicit(&audio_beat_start_ns,
                                        memory_order_relaxed);
  uint64_t beat = atomic_load_explicit(&audio_beat_ns, memory_order_relaxed);
  if (beat > 0 && start > 0 && t >= start && t - start < 2 * beat) {
    return (double)(t - start) / beat;
  }
  if (t < origin) return 0;
  return (double)(t - origin) / AUDIO_DEFAULT_BEAT_NS;
}

// ---------------------------------------------------------------------------
// The trance gate
//
// A drone with II or Q on (TRANCE_GATE_*, common.h) is chopped on the beat's
// grid: 8ths, 16ths, or the 1 . 3 4 of the two together -- and in jig time,
// where a beat is three 8ths, those three, six 16ths, or 1 . 3 4 . 6.  The
// grid is the foot bass's, lean and all, not an even one (publish_music), so
// the gate moves with the bass rather than against it.  Only inside the beat
// the last pedal hit started -- once the feet stop, the pad just holds, as a
// held note is allowed to.  Ramped over a couple of milliseconds so it
// doesn't click.
// ---------------------------------------------------------------------------

#define TRANCE_GATE_ATTACK_MS 2
#define TRANCE_GATE_RELEASE_MS 6

// `steps` are where the 16ths start, in 72nds of a beat, `n` of them.
static bool trance_gate_open(int pattern, double phase,
                             const unsigned char* steps, int n) {
  if (pattern == TRANCE_GATE_NONE || phase < 0 || n == 0) return true;
  double at = phase * 72;
  int step = 0;
  while (step + 1 < n && at >= steps[step + 1]) step++;
  double end = step + 1 < n ? steps[step + 1] : 72;
  double into = (at - steps[step]) / (end - steps[step]);
  switch (pattern) {
  case TRANCE_GATE_8THS:  return step % 2 == 0;
  case TRANCE_GATE_16THS: return into < 0.55;
  default:
    // 1 . 3 4, and in jig time 1 . 3 4 . 6
    return step % 3 != 1 && into < 0.7;
  }
}

// ---------------------------------------------------------------------------
// Kick Duck
//
// Everything fluidsynth plays but the kick, turned down on each kick and
// brought back up over the beat: the pump of a sidechained mix, set off by
// each kick as it's played rather than by a beat worked out ahead of time, so
// it follows the feet and a kick left out is a pump left out.  The kick pedal
// counts whether or not the rig's kick is on (jammermidilib.h's
// kick_duck_kick), since the pedals may be playing another synth's drums.  Done on the
// audio rather than with CC11, which the fades, the breath and Pulse already
// share, so it's smooth and lands on the sample.
//
// The rhythm parts that play with the kick aren't ducked either -- the foot
// basses and the arp -- so they hit with it rather than being pushed down by
// it.
//
// The Breath Gate's channel (ENDPOINT_BREATH) is gated here too, before it's
// ducked with the rest: it sounds only while you blow, so pulsing the breath
// chops it into a rhythm.  It opens past BREATH_GATE_OPEN and shuts below
// BREATH_GATE_SHUT (common.h), and in a few milliseconds rather than at once,
// so it doesn't click.  Each breath after a rest starts the chord afresh, so
// it opens on the pad's attack: jammermidilib.h's breath_gate_breath.
//
// For that the synth renders each MIDI channel to a stereo pair of its own
// (start_synth), and the mix happens here: the exempt channels as they are,
// and the rest ducked.  The whistle is summed in after, by audio_mix_hook, so
// it isn't ducked; it isn't fluidsynth's.
// ---------------------------------------------------------------------------

#define SYNTH_CHANNELS 34  // MIDI channels, each rendered to its own pair
#define KICK_DUCK_DEPTH 0.788f   // taken off at the bottom: about -13.5dB
#define KICK_DUCK_ATTACK_MS 5    // down this fast, so it doesn't click
#define KICK_DUCK_RELEASE 0.6    // back up over this much of a beat
#define KICK_DUCK_FRAMES 1024    // rendered this many at a time
#define BREATH_GATE_ATTACK_MS 3
#define BREATH_GATE_RELEASE_MS 25

// Set on each kick while Kick Duck is on, with the length of the beat it's
// in, which is how long the duck takes to come back up.
static _Atomic unsigned kick_duck_hits;
static _Atomic uint64_t kick_duck_beat_ns;

// Called by jammermidilib.h's kick_hook, under jammer's lock.
static void kick_duck_hit(uint64_t beat_ns) {
  atomic_store_explicit(&kick_duck_beat_ns, beat_ns, memory_order_relaxed);
  atomic_fetch_add_explicit(&kick_duck_hits, 1, memory_order_release);
}

// How much louder the drum's kit is for the breath, from jammermidilib.h's
// kit_gain_hook: its channels, the kicks' included, but not the Grid Hat's,
// which follows the breath its own way.  Smoothed, since the breath comes
// in steps.
static _Atomic float kit_gain_target = 1.0f;
static double kit_gain_now = 1;
#define KIT_GAIN_SMOOTH_MS 10

static void kit_gain_set(double gain) {
  atomic_store_explicit(&kit_gain_target, (float)gain, memory_order_relaxed);
}

// The Audio Output menu's drum volume: everything the drum plays -- its
// kit, the kicks, the Grid Hat and the Feet -- on top of the global volume,
// the drum's -/+ and the rest.  Remembered, like the others.  Halfway, 1,
// is DRUM_GAIN_UNITY: where it was set, and stayed, once the drum's own
// levels had settled.  drum_gain is the slider's; drum_level what's heard.
#define MAX_DRUM_GAIN 2.0
#define DRUM_GAIN_UNITY 1.822
static _Atomic float drum_gain = 1.0f;
static _Atomic float drum_level = (float)DRUM_GAIN_UNITY;

void set_drum_gain(double gain) {
  if (gain < 0) gain = 0;
  if (gain > MAX_DRUM_GAIN) gain = MAX_DRUM_GAIN;
  atomic_store_explicit(&drum_gain, (float)gain, memory_order_relaxed);
  atomic_store_explicit(&drum_level, (float)(gain * DRUM_GAIN_UNITY),
                        memory_order_relaxed);
}

static void apply_kit_gain(float** bufs, int n, double sample_rate) {
  double target = atomic_load_explicit(&kit_gain_target,
                                       memory_order_relaxed);
  float drum = atomic_load_explicit(&drum_level, memory_order_relaxed);
  if (drum != 1.0f) {
    for (int i = 0; i < n; i++) {
      bufs[2 * CHANNEL_HAT][i] *= drum;
      bufs[2 * CHANNEL_HAT + 1][i] *= drum;
    }
  }
  if (target == 1 && kit_gain_now == 1 && drum == 1.0f) return;
  static const int CHANNELS[] = {CHANNEL_DRUM, CHANNEL_KICK,
                                 CHANNEL_PITCHED_KICK};
  double k = 1 - exp(-1000 / (sample_rate * KIT_GAIN_SMOOTH_MS));
  double g = kit_gain_now;
  for (int i = 0; i < n; i++) {
    g += (target - g) * k;
    for (int c = 0; c < 3; c++) {
      bufs[2 * CHANNELS[c]][i] *= (float)(g * drum);
      bufs[2 * CHANNELS[c] + 1][i] *= (float)(g * drum);
    }
  }
  kit_gain_now = fabs(g - target) < 1e-4 ? target : g;
}

// The audio thread's own.
static float synth_channel_bufs[2 * SYNTH_CHANNELS][KICK_DUCK_FRAMES];
static unsigned kick_duck_seen;
static double kick_duck_pos = -1;  // frames into the duck, or -1 if none
static double kick_duck_from;      // the gain its attack started from
static double kick_duck_attack, kick_duck_release;  // in frames
static double kick_duck_gain = 1;
static bool breath_gate_open;
static double breath_gate_gain;
static double trance_gate_gain[SYNTH_CHANNELS];
static double trance_gate_phase[KICK_DUCK_FRAMES];

// Chop the drones' channels, and their voice channels, with the trance gate,
// for n frames from t0.
static void apply_trance_gates(float** bufs, int n, uint64_t t0,
                               double sample_rate) {
  bool any = false;
  int pattern[SYNTH_CHANNELS];
  for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
    int ep = channel_endpoint(ch);
    pattern[ch] = ep < N_ENDPOINTS
      ? atomic_load_explicit(&audio_trance_gate[ep], memory_order_relaxed)
      : TRANCE_GATE_NONE;
    if (pattern[ch] != TRANCE_GATE_NONE || trance_gate_gain[ch] < 1) {
      any = true;
    }
  }
  if (!any) return;
  for (int i = 0; i < n; i++) {
    trance_gate_phase[i] =
      audio_beat_phase(t0 + (uint64_t)(i * 1e9 / sample_rate));
  }
  unsigned char steps[6];
  int n_steps = atomic_load_explicit(&audio_n_gate_steps,
                                     memory_order_relaxed);
  if (n_steps > 6) n_steps = 6;
  for (int i = 0; i < n_steps; i++) {
    steps[i] = atomic_load_explicit(&audio_gate_steps[i],
                                    memory_order_relaxed);
  }
  double up = 1000 / (sample_rate * TRANCE_GATE_ATTACK_MS);
  double down = 1000 / (sample_rate * TRANCE_GATE_RELEASE_MS);
  for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
    if (pattern[ch] == TRANCE_GATE_NONE && trance_gate_gain[ch] >= 1) {
      continue;
    }
    double gain = trance_gate_gain[ch];
    for (int i = 0; i < n; i++) {
      double target = trance_gate_open(pattern[ch], trance_gate_phase[i],
                                       steps, n_steps) ? 1 : 0;
      gain = target > gain ? fmin(target, gain + up)
                           : fmax(target, gain - down);
      bufs[2 * ch][i] *= (float)gain;
      bufs[2 * ch + 1][i] *= (float)gain;
    }
    trance_gate_gain[ch] = gain;
  }
}

// Endpoints are channels (common.h), so these are the foot basses and arp.
static bool kick_duck_exempt(int channel) {
  switch (channel) {
  case CHANNEL_KICK:
  case CHANNEL_PITCHED_KICK:
  case ENDPOINT_FOOTBASS:
  case ENDPOINT_FOOTBASS_2:
  case ENDPOINT_FOOTBASS_3:
  case ENDPOINT_ARP:
    return true;
  }
  return false;
}

// The duck's gain for the next frame.
static float kick_duck_step(void) {
  if (kick_duck_pos < 0) return 1;
  double bottom = 1 - KICK_DUCK_DEPTH;
  if (kick_duck_pos < kick_duck_attack) {
    kick_duck_gain = kick_duck_from +
      (bottom - kick_duck_from) * kick_duck_pos / kick_duck_attack;
  } else if (kick_duck_pos < kick_duck_attack + kick_duck_release) {
    // A raised cosine back up, so it leaves the bottom and arrives at the
    // top gently.
    double x = (kick_duck_pos - kick_duck_attack) / kick_duck_release;
    kick_duck_gain = 1 - KICK_DUCK_DEPTH * 0.5 * (1 + cos(M_PI * x));
  } else {
    kick_duck_gain = 1;
    kick_duck_pos = -1;
    return 1;
  }
  kick_duck_pos++;
  return (float)kick_duck_gain;
}

// fluid_synth_process, with every channel but the exempt ones ducked, and
// the Breath Gate's gated.  Up to KICK_DUCK_FRAMES at a time, since that's
// what the channel buffers hold.
static int render_kick_ducked(fluid_synth_t* synth, int len, int nfx,
                              float** fx, int nout, float** out,
                              double sample_rate) {
  unsigned hits = atomic_load_explicit(&kick_duck_hits, memory_order_acquire);
  if (hits != kick_duck_seen) {
    kick_duck_seen = hits;
    double beat_s = (double)atomic_load_explicit(
      &kick_duck_beat_ns, memory_order_relaxed) / 1e9;
    kick_duck_from = kick_duck_gain;
    kick_duck_attack = sample_rate * KICK_DUCK_ATTACK_MS / 1000;
    kick_duck_release = sample_rate * beat_s * KICK_DUCK_RELEASE;
    kick_duck_pos = 0;
  }

  // Where the gate is headed, as the breath says.
  double blown = breath_blown(
    atomic_load_explicit(&audio_breath, memory_order_relaxed));
  if (blown > BREATH_GATE_OPEN) breath_gate_open = true;
  if (blown < BREATH_GATE_SHUT) breath_gate_open = false;
  double gate_target = breath_gate_open ? 1 : 0;
  double gate_up = 1000 / (sample_rate * BREATH_GATE_ATTACK_MS);
  double gate_down = 1000 / (sample_rate * BREATH_GATE_RELEASE_MS);

  float* bufs[2 * SYNTH_CHANNELS];
  float* chunk_fx[nfx > 0 ? nfx : 1];
  int result = FLUID_OK;
  for (int done = 0; done < len; done += KICK_DUCK_FRAMES) {
    int n = len - done < KICK_DUCK_FRAMES ? len - done : KICK_DUCK_FRAMES;
    for (int i = 0; i < 2 * SYNTH_CHANNELS; i++) {
      bufs[i] = synth_channel_bufs[i];
      memset(bufs[i], 0, (size_t)n * sizeof(float));
    }
    for (int i = 0; i < nfx; i++) chunk_fx[i] = fx[i] + done;
    result = fluid_synth_process(synth, n, nfx, chunk_fx,
                                 2 * SYNTH_CHANNELS, bufs);
    if (result != FLUID_OK || nout < 2) continue;
    apply_kit_gain(bufs, n, sample_rate);
    apply_trance_gates(bufs, n,
                       audio_block_ns + (uint64_t)(done * 1e9 / sample_rate),
                       sample_rate);

    float* left = out[0] + done;
    float* right = out[1] + done;
    for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
      if (kick_duck_exempt(ch) || channel_endpoint(ch) == ENDPOINT_BREATH) {
        continue;
      }
      for (int i = 0; i < n; i++) {
        left[i] += bufs[2 * ch][i];
        right[i] += bufs[2 * ch + 1][i];
      }
    }
    for (int i = 0; i < n; i++) {
      breath_gate_gain = gate_target > breath_gate_gain
        ? fmin(gate_target, breath_gate_gain + gate_up)
        : fmax(gate_target, breath_gate_gain - gate_down);
      float gain = (float)breath_gate_gain;
      left[i] += bufs[2 * ENDPOINT_BREATH][i] * gain;
      right[i] += bufs[2 * ENDPOINT_BREATH + 1][i] * gain;
      int base = voice_channel_base(ENDPOINT_BREATH);
      for (int k = 0; k < VOICE_CHANNELS_PER_DRONE; k++) {
        left[i] += bufs[2 * (base + k)][i] * gain;
        right[i] += bufs[2 * (base + k) + 1][i] * gain;
      }
    }
    if (kick_duck_pos >= 0) {
      for (int i = 0; i < n; i++) {
        float gain = kick_duck_step();
        left[i] *= gain;
        right[i] *= gain;
      }
    }
    for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
      if (!kick_duck_exempt(ch)) continue;
      for (int i = 0; i < n; i++) {
        left[i] += bufs[2 * ch][i];
        right[i] += bufs[2 * ch + 1][i];
      }
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Breath sweeps
//
// Filters on everything fluidsynth plays, moved by the breath controller, each
// leaving the mix as it was when you aren't blowing:
//
//   Sweep Bass (4)    a high-pass rising from 10Hz, below anything played, to
//                     1.2kHz: the bass drains out as you blow and comes back
//                     as you stop
//   Sweep Treble (6)  a low-pass closing from 18kHz to 300Hz
//   Sweep Peak (7)    a resonant peak rising from 250Hz to 5kHz and growing
//                     to +12dB, the rest brought down by half that so the
//                     peak doesn't overload the output
//
// In that order, after Kick Duck and before the whistle is summed in, which
// isn't fluidsynth's.  The breath is smoothed over SWEEP_SMOOTH_MS so the
// controller's steps aren't heard.  Switching one on or off crossfades
// between the filtered sound and the dry one over SWEEP_FADE_MS rather than
// cutting between them: even at rest a filter shifts the phase of what it
// passes -- the low notes through the high-pass, the highs through the
// low-pass -- so the two don't meet, and a cut from one to the other clicks.
// ---------------------------------------------------------------------------

#define SWEEP_SMOOTH_MS 30
#define SWEEP_FADE_MS 20

enum { SWEEP_BASS, SWEEP_TREBLE, SWEEP_PEAK, N_SWEEPS };


// The audio thread's own.  Each sweep is a state-variable filter (Andrew
// Simper's, after Zavalishin's topology-preserving transform), with its
// cutoff worked out afresh every sample.  A biquad's coefficients can't be
// moved that fast without it ringing: pulsing the breath had one stepping
// every 16 samples, and it squealed.
typedef struct {
  double ic1[2], ic2[2];  // the two integrators, per side
  double level;  // the smoothed breath, 0-1, or 0 at rest
  double wet;    // how much of the filtered sound is heard, 0-1
  bool live;     // being applied: on, or fading out
} Sweep;
static Sweep sweeps[N_SWEEPS];

// `from` at 0, `to` at 1, evenly in pitch.
static double sweep_hz(double from, double to, double x) {
  return from * pow(to / from, x);
}

// Where a sweep's filter is for one sample, from the breath it's at.
typedef struct {
  double k, a, a1, a2, a3;
} SweepCoeffs;

static SweepCoeffs sweep_coefficients(int which, double x,
                                      double sample_rate) {
  double hz, q = M_SQRT1_2, a = 1;
  switch (which) {
  case SWEEP_BASS:   hz = sweep_hz(10, 1200, x); break;
  case SWEEP_TREBLE: hz = sweep_hz(18000, 300, x); break;
  default:
    hz = sweep_hz(250, 5000, x);
    q = 4;
    // Up to +12dB in the first quarter; `a` is the square root of the gain.
    a = pow(10, 12 * fmin(1, 4 * x) / 40);
    break;
  }
  hz = fmin(hz, 0.45 * sample_rate);
  double g = tan(M_PI * hz / sample_rate);
  SweepCoeffs c;
  c.k = which == SWEEP_PEAK ? 1 / (q * a) : 1 / q;
  c.a = a;
  c.a1 = 1 / (1 + g * (g + c.k));
  c.a2 = g * c.a1;
  c.a3 = g * c.a2;
  return c;
}

static float sweep_run(int which, Sweep* s, const SweepCoeffs* c, int ch,
                       float in) {
  double v3 = in - s->ic2[ch];
  double v1 = c->a1 * s->ic1[ch] + c->a2 * v3;  // band
  double v2 = s->ic2[ch] + c->a2 * s->ic1[ch] + c->a3 * v3;  // low
  s->ic1[ch] = 2 * v1 - s->ic1[ch];
  s->ic2[ch] = 2 * v2 - s->ic2[ch];
  switch (which) {
  case SWEEP_BASS:   return (float)(in - c->k * v1 - v2);
  case SWEEP_TREBLE: return (float)v2;
  default:
    // The peak, and the rest brought down by half of it so it doesn't
    // overload the output.
    return (float)((in + c->k * (c->a * c->a - 1) * v1) / c->a);
  }
}

static void apply_sweeps(float* left, float* right, int len,
                         double sample_rate) {
  double blown = breath_blown(
    atomic_load_explicit(&audio_breath, memory_order_relaxed));
  // BREATH_FX_SWEEP_* are the first bits, in the sweeps' order.
  unsigned on = atomic_load_explicit(&audio_breath_fx, memory_order_relaxed);
  double k = 1 - exp(-1 / (sample_rate * SWEEP_SMOOTH_MS / 1000));
  double fade_step = 1000 / (sample_rate * SWEEP_FADE_MS);

  for (int which = 0; which < N_SWEEPS; which++) {
    Sweep* s = &sweeps[which];
    bool wanted = on & (1 << which);
    if (!wanted && !s->live) continue;
    if (wanted && !s->live) {
      memset(s, 0, sizeof(*s));
      s->live = true;
    }
    double target = wanted ? blown : 0;
    double wet_target = wanted ? 1 : 0;
    for (int i = 0; i < len; i++) {
      s->level += (target - s->level) * k;
      if (s->wet != wet_target) {
        s->wet = wanted ? fmin(1, s->wet + fade_step)
                        : fmax(0, s->wet - fade_step);
      }
      SweepCoeffs c = sweep_coefficients(which, s->level, sample_rate);
      float wet = (float)s->wet;
      float l = sweep_run(which, s, &c, 0, left[i]);
      float r = sweep_run(which, s, &c, 1, right[i]);
      left[i] += (l - left[i]) * wet;
      right[i] += (r - right[i]) * wet;
    }
    // Faded all the way out after being switched off: out of the way.
    if (!wanted && s->wet == 0) s->live = false;
  }
}

// ---------------------------------------------------------------------------
// Breath instruments
//
// The Breath Gate's percussion voices (B, N and M with it selected): played
// by moving the breath rather than by how hard it is, so holding it steady is
// silence and every sound is set off by a movement.
// Their own sound rather than fluidsynth's, like the whistle, and summed in
// after the sweeps and Kick Duck, so neither touches them.
//
//   Guiro         a stick over ridges: every 40th of the breath's range the
//                 breath moves, one click, a tick of noise ringing through
//                 three wooden resonances.  Slow is a ratchet and fast a zip.
//   Washboard     the same, over finer ridges, with a thimble on metal:
//                 longer, higher, inharmonic rings.
//   Guira         the same, a wire brush over a punched metal cylinder: each
//                 ridge a "tsch" of many tines.
//
// All follow the breath through a little slack, BREATH_BACKLASH, so the
// controller wavering by a step while it's held doesn't rattle or click.
// Switched off, what's ringing rings out.
// ---------------------------------------------------------------------------

#define BREATH_BACKLASH 0.012     // of the breath's range: about 1.3 steps
#define BREATH_PLAY_SMOOTH_MS 3   // the controller's steps, smoothed
#define BREATH_PLAY_TAIL_MS 400   // rung out for this long after it's off

#define GUIRO_LEVEL 0.2
#define WASHBOARD_LEVEL 0.25
#define GUIRA_LEVEL 0.08

// A bandpass state-variable filter, normalized to 0dB at its peak.  Mono.
typedef struct {
  double ic1, ic2, g, k, a1, a2;
} Bandpass;

static void bandpass_set(Bandpass* f, double hz, double q,
                         double sample_rate) {
  f->g = tan(M_PI * fmin(hz, 0.45 * sample_rate) / sample_rate);
  f->k = 1 / q;
  f->a1 = 1 / (1 + f->g * (f->g + f->k));
  f->a2 = f->g * f->a1;
}

static double bandpass_run(Bandpass* f, double in) {
  double v1 = f->a1 * f->ic1 + f->a2 * (in - f->ic2);  // band
  double v2 = f->ic2 + f->g * v1;                      // low
  f->ic1 = 2 * v1 - f->ic1;
  f->ic2 = 2 * v2 - f->ic2;
  return f->k * v1;
}

// The same filter's lowpass, 0dB below its corner.
static double lowpass_run(Bandpass* f, double in) {
  double v1 = f->a1 * f->ic1 + f->a2 * (in - f->ic2);
  double v2 = f->ic2 + f->g * v1;
  f->ic1 = 2 * v1 - f->ic1;
  f->ic2 = 2 * v2 - f->ic2;
  return v2;
}

static uint32_t breath_noise_state = 22222;

// White noise, -1 to 1.
static double breath_noise(void) {
  uint32_t x = breath_noise_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  breath_noise_state = x;
  return (double)x / 2147483648.0 - 1;
}

// Following the breath: the audio thread's own, one per instrument.
typedef struct {
  bool live;
  int tail;             // frames left to ring out once it's off
  double level;         // the breath, smoothed
  double stick;         // where it's got to, through the slack
} BreathPlayer;

// Start or keep going, or ring out and stop.  True if it has anything to do.
// Starting afresh clears `state` too, `size` bytes of it.
static bool breath_player_run(BreathPlayer* p, bool wanted, void* state,
                              size_t size, double sample_rate, int len) {
  if (wanted) {
    if (!p->live) {
      memset(p, 0, sizeof(*p));
      memset(state, 0, size);
      p->live = true;
    }
    p->tail = (int)(sample_rate * BREATH_PLAY_TAIL_MS / 1000);
    return true;
  }
  if (!p->live) return false;
  p->tail -= len;
  if (p->tail <= 0) p->live = false;
  return p->live;
}

// How far the breath has moved this frame, through the slack.
static double breath_player_move(BreathPlayer* p, double blown, double k) {
  p->level += (blown - p->level) * k;
  double was = p->stick;
  if (p->level > p->stick + BREATH_BACKLASH) {
    p->stick = p->level - BREATH_BACKLASH;
  } else if (p->level < p->stick - BREATH_BACKLASH) {
    p->stick = p->level + BREATH_BACKLASH;
  }
  return p->stick - was;
}

// The guiro and washboard: a stick, or a thimble, over ridges.
#define SCRAPER_RINGS 4
typedef struct {
  int ridges;
  double tick_ms;               // how long a click's strike lasts
  double hz[SCRAPER_RINGS], q[SCRAPER_RINGS], mix[SCRAPER_RINGS];
  double level;
} ScraperSound;

static const ScraperSound GUIRO_SOUND = {
  40, 0.4, {1300, 2300, 3900, 0}, {6, 10, 8, 1}, {0.6, 1, 0.7, 0},
  GUIRO_LEVEL,
};
static const ScraperSound WASHBOARD_SOUND = {
  60, 0.25, {3100, 4700, 6900, 9300}, {25, 30, 30, 25}, {0.8, 1, 0.8, 0.5},
  WASHBOARD_LEVEL,
};
static const ScraperSound GUIRA_SOUND = {
  50, 1.5, {4200, 6100, 8300, 11000}, {12, 14, 12, 10}, {0.7, 1, 0.8, 0.6},
  GUIRA_LEVEL,
};

typedef struct {
  double travel;  // since the last ridge
  double strike;
  Bandpass rings[SCRAPER_RINGS];
} Scraper;

static BreathPlayer guiro_player, washboard_player, guira_player;
static Scraper guiro, washboard, guira;

static void play_scraper(Scraper* s, BreathPlayer* p, const ScraperSound* sound,
                         float* left, float* right, int len, bool playing,
                         double blown, double sample_rate) {
  double k = 1 - exp(-1 / (sample_rate * BREATH_PLAY_SMOOTH_MS / 1000));
  for (int r = 0; r < SCRAPER_RINGS; r++) {
    bandpass_set(&s->rings[r], sound->hz[r] ? sound->hz[r] : 1000,
                 sound->q[r], sample_rate);
  }
  double ridge = 1.0 / sound->ridges;
  double strike_decay = exp(-1 / (sample_rate * sound->tick_ms / 1000));
  for (int i = 0; i < len; i++) {
    double moved = breath_player_move(p, playing ? blown : 0, k);
    if (playing) s->travel += fabs(moved);
    if (s->travel >= ridge) {
      // A ridge crossed.  A fast scrape can cross a few in one frame; they'd
      // land together, so it's one click and the rest let go.
      s->travel = fmod(s->travel, ridge);
      // The down stroke, the breath falling, a little softer.
      s->strike = moved > 0 ? 1 : 0.75;
    }
    double x = breath_noise() * s->strike;
    s->strike *= strike_decay;
    double y = 0;
    for (int r = 0; r < SCRAPER_RINGS; r++) {
      if (sound->mix[r]) y += sound->mix[r] * bandpass_run(&s->rings[r], x);
    }
    left[i] += (float)(y * sound->level);
    right[i] += (float)(y * sound->level);
  }
}

// The Brushes' stir: brushes circling on a snare head without letting up,
// the jazz drummer's stirring the soup, as fast as you're blowing.  Just past
// the gate it's a slow, soft stir; blown up to BRUSH_FULL, a quick hard
// scrub, and it holds there.  Noise through the head's swish and the
// bristles' hiss, steady for a steady breath: the faster the stir, the
// louder and brighter, and the grittier, the wires catching on the head's
// coating more often.  It drifts a little from side to side as it circles,
// BRUSH_SLOW_HZ to BRUSH_FAST_HZ times a second, and lifts off the head when
// the breath shuts the gate, over BRUSH_LIFT_MS.
#define BRUSH_FULL 0.75   // of the breath's range
#define BRUSH_SLOW_HZ 0.6
#define BRUSH_FAST_HZ 3.0
#define BRUSH_PACE_MS 40    // how quickly the stir speeds up and slows down
#define BRUSH_TOUCH_MS 15   // how quickly it's on the head
#define BRUSH_LIFT_MS 120   // and off it
#define BRUSH_GRIT_HZ 2500  // wires catching, a second, at full speed
#define BRUSH_GRIT_MS 0.12
#define BRUSH_PAN 0.2
// A moderate stir at about -52dB, by perceived loudness: under the Brush
// kit's slap, which is about -45dB.
#define BRUSH_LEVEL 0.015

typedef struct {
  bool down;        // on the head: the breath's past the gate
  double touch;     // 0-1, followed
  double pace;      // 0-1, how fast it's stirring, followed
  double phase;     // 0-1: once round
  double grit;      // a wire's catch, dying away
  Bandpass head, bristles, wires;
} BrushStir;

static BreathPlayer brush_player;
static BrushStir brush;

static void play_brush(float* left, float* right, int len, bool playing,
                       double blown, double sample_rate) {
  if (!playing || blown < BREATH_GATE_SHUT) {
    brush.down = false;
  } else if (blown > BREATH_GATE_OPEN) {
    brush.down = true;
  }
  double pace = (blown - BREATH_GATE_SHUT) / (BRUSH_FULL - BREATH_GATE_SHUT);
  pace = !brush.down ? brush.pace : pace < 0 ? 0 : pace > 1 ? 1 : pace;
  double touch = brush.down ? 1 : 0;
  double k_pace = 1 - exp(-1 / (sample_rate * BRUSH_PACE_MS / 1000));
  double k_touch = 1 - exp(-1 / (sample_rate *
    (touch > brush.touch ? BRUSH_TOUCH_MS : BRUSH_LIFT_MS) / 1000));
  double grit_decay = exp(-1 / (sample_rate * BRUSH_GRIT_MS / 1000));
  // Brighter as it speeds up: the head's band and the bristles' rise.
  bandpass_set(&brush.head, 2400 + 1200 * brush.pace, 0.7, sample_rate);
  bandpass_set(&brush.bristles, 5500 + 2500 * brush.pace, 1.2, sample_rate);
  bandpass_set(&brush.wires, 8500, 2.5, sample_rate);
  for (int i = 0; i < len; i++) {
    brush.pace += (pace - brush.pace) * k_pace;
    brush.touch += (touch - brush.touch) * k_touch;
    double hz = BRUSH_SLOW_HZ + (BRUSH_FAST_HZ - BRUSH_SLOW_HZ) * brush.pace;
    brush.phase = fmod(brush.phase + hz / sample_rate, 1);
    if ((breath_noise() + 1) / 2 < BRUSH_GRIT_HZ * brush.pace / sample_rate) {
      brush.grit = 0.5 + 0.5 * fabs(breath_noise());
    }
    double x = breath_noise();
    double y = 0.8 * bandpass_run(&brush.head, x) +
               (0.2 + 0.5 * brush.pace) * bandpass_run(&brush.bristles, x) +
               1.5 * bandpass_run(&brush.wires, x * brush.grit);
    brush.grit *= grit_decay;
    // A slow stir's a whisper, not silence; faster, louder, to full.
    double level = BRUSH_LEVEL * brush.touch * (0.25 + 0.75 * brush.pace);
    double pan = BRUSH_PAN * cos(2 * M_PI * brush.phase);
    left[i] += (float)(y * level * (1 - pan));
    right[i] += (float)(y * level * (1 + pan));
  }
}

// The Tamb Shake: a tambourine jiggled in the hand, the forearm shaking it
// back and forth on the beat's grid -- 32nds, or six to a beat in jig time
// -- for as long as you blow.  Its jingles, TAMB_JINGLES little metal discs,
// each shake on their own: at each turn of the hand each is thrown against
// its pair a moment after the hand turns, its own moment, and bounces back
// a time or two, and loose as they are, they rattle between the turns too.
// Just past the gate only some of them touch, softly and loosely spread;
// blown harder, all of them, harder and closer together, bouncing more and
// rattling on, until by TAMB_FULL it's a rough, dense shake.  The stroke out
// a little harder than the stroke back.  Each jingle is noise through two
// bands of its own, around 5kHz and 11kHz, as a tambourine's is, and a
// faint ring, dying away over 50-90ms, and little under 3kHz.  On the pedals' grid while they're
// going, and from the start of the breath at 116 BPM when they aren't
// (audio_grid_beats).
#define TAMB_FULL 0.9
#define TAMB_JINGLES 12
#define TAMB_SPREAD_MS 30.0     // how long a turn's clashes take, gently
#define TAMB_TIGHT_MS 12.0      // and hard
#define TAMB_RATTLE_HZ 12.0     // each jingle, between the turns, blowing hard
#define TAMB_BACK 0.8           // the stroke back, against the stroke out
#define TAMB_LEVEL 0.11

typedef struct {
  Bandpass low, high;
  double ring_c1, ring_c2, ring_y1, ring_y2;
  double env, decay;    // how hard it's sounding, and how fast that goes
  double hit_at;        // seconds into the turn it strikes next, or -1
  double hit;           // how hard
} TambJingle;

typedef struct {
  bool open;
  bool made;            // the jingles' sizes picked
  uint64_t origin_ns;
  int64_t slot;         // the turn it's on
  double since_turn;    // seconds
  bool back;            // this turn's the stroke back
  double hard;          // 0-1, followed
  double below;         // what the high-pass takes off
  TambJingle jingles[TAMB_JINGLES];
} Tambourine;

static BreathPlayer tamb_player;
static Tambourine tamb;

static double tamb_random(void) {
  return (breath_noise() + 1) / 2;
}

// Each jingle its own size, and so its own colour and ring.
static void tamb_make(double sample_rate) {
  for (int j = 0; j < TAMB_JINGLES; j++) {
    TambJingle* g = &tamb.jingles[j];
    bandpass_set(&g->low, 4600 + 2000 * tamb_random(), 6, sample_rate);
    bandpass_set(&g->high, 10000 + 2500 * tamb_random(), 7, sample_rate);
    double decay_s = 0.05 + 0.04 * tamb_random();
    g->decay = exp(-1 / (sample_rate * decay_s));
    double w = 2 * M_PI * (6000 + 3000 * tamb_random()) / sample_rate;
    double r = exp(-1 / (sample_rate * decay_s * 0.7));
    g->ring_c1 = 2 * r * cos(w);
    g->ring_c2 = -r * r;
    g->hit_at = -1;
  }
  tamb.made = true;
}

// The turn of the hand: each jingle that's loose enough to be thrown this
// time, when, and how hard.
static void tamb_turn(void) {
  double spread = (TAMB_SPREAD_MS -
    (TAMB_SPREAD_MS - TAMB_TIGHT_MS) * tamb.hard) / 1000;
  for (int j = 0; j < TAMB_JINGLES; j++) {
    TambJingle* g = &tamb.jingles[j];
    if (tamb_random() > 0.35 + 0.65 * tamb.hard) {
      g->hit_at = -1;
      continue;
    }
    g->hit_at = spread * tamb_random() * tamb_random();
    g->hit = (0.03 + 0.97 * pow(tamb.hard, 1.4)) *
      (0.5 + 0.5 * tamb_random()) * (tamb.back ? TAMB_BACK : 1);
  }
}

static void tamb_strike(TambJingle* g, double strength, double sample_rate) {
  g->env += strength;
  g->ring_y1 += 0.15 * strength;
  // A bounce back off its pair, softer, more of them the harder it's
  // shaken; or, between the turns, the odd rattle.
  if (tamb_random() < 0.3 + 0.5 * tamb.hard && strength > 0.02) {
    g->hit_at = tamb.since_turn + 0.006 + 0.012 * tamb_random();
    g->hit = strength * (0.3 + 0.3 * tamb_random());
  } else {
    double rate = TAMB_RATTLE_HZ * tamb.hard * tamb.hard;
    g->hit_at = rate > 0 ? tamb.since_turn - log(tamb_random() + 1e-9) / rate
                         : -1;
    g->hit = (0.1 + 0.3 * tamb_random()) * strength;
  }
  (void)sample_rate;
}

static void play_tamb(float* left, float* right, int len, bool playing,
                      double blown, double sample_rate) {
  if (!tamb.made) tamb_make(sample_rate);
  if (!playing || blown < BREATH_GATE_SHUT) {
    if (tamb.open) {
      for (int j = 0; j < TAMB_JINGLES; j++) tamb.jingles[j].hit_at = -1;
    }
    tamb.open = false;
  } else if (!tamb.open && blown > BREATH_GATE_OPEN) {
    tamb.open = true;
    tamb.origin_ns = audio_block_ns;
    tamb.slot = -1;
  }
  double hard = (blown - BREATH_GATE_SHUT) / (TAMB_FULL - BREATH_GATE_SHUT);
  hard = hard < 0 ? 0 : hard > 1 ? 1 : hard;
  double k_hard = 1 - exp(-1 / (sample_rate * 0.02));
  double k_below = 1 - exp(-2 * M_PI * 3000 / sample_rate);
  int division =
    atomic_load_explicit(&audio_jig, memory_order_relaxed) ? 6 : 8;
  for (int i = 0; i < len; i++) {
    tamb.hard += (hard - tamb.hard) * k_hard;
    if (tamb.open) {
      uint64_t t = audio_block_ns + (uint64_t)(i * 1e9 / sample_rate);
      int64_t slot =
        (int64_t)floor(audio_grid_beats(t, tamb.origin_ns) * division);
      if (slot != tamb.slot) {
        // A turn of the hand, and the first as the breath starts.
        tamb.back = tamb.slot >= 0 && slot % 2;
        tamb.slot = slot;
        tamb.since_turn = 0;
        tamb_turn();
      }
    }
    double y = 0;
    for (int j = 0; j < TAMB_JINGLES; j++) {
      TambJingle* g = &tamb.jingles[j];
      if (g->hit_at >= 0 && tamb.since_turn >= g->hit_at) {
        g->hit_at = -1;
        tamb_strike(g, g->hit, sample_rate);
      }
      if (g->env < 1e-6 && fabs(g->ring_y1) < 1e-6) continue;
      double x = breath_noise() * g->env;
      g->env *= g->decay;
      double ring = g->ring_c1 * g->ring_y1 + g->ring_c2 * g->ring_y2;
      g->ring_y2 = g->ring_y1;
      g->ring_y1 = ring;
      y += bandpass_run(&g->low, x) + bandpass_run(&g->high, x) + ring;
    }
    tamb.since_turn += 1 / sample_rate;
    // Nothing much under 3kHz: the band's skirts, not the jingles.
    tamb.below += (y - tamb.below) * k_below;
    y -= tamb.below;
    left[i] += (float)(y * TAMB_LEVEL);
    right[i] += (float)(y * TAMB_LEVEL);
  }
}

// ---------------------------------------------------------------------------
// The drum's Feet kit: French Canadian foot percussion, leather shoes on a
// well-sprung wooden floor, each step sent by jammermidilib.h's feet_tick
// through feet_hit.  Modelled on recordings of the real
// thing: a wooden floor doesn't ring at a pitch, and a step isn't one hit
// but a few, a few milliseconds apart, each a burst of noise in three broad
// bands, and the floor's low answer swelling up just after:
//
//   click   leather on wood, from 1.2kHz up, over in a few milliseconds
//   body    the boards, 400Hz-1.5kHz, a little longer
//   low     the floor giving, 35-180Hz, swelling over a few milliseconds
//           after each hit and gone within a few tens
//
// and a quiet tail after, the room.  Three kinds:
//
//   thump   the heel landing, heavy in the body and low but not the click,
//           and the ball of the foot slapping down 16ms later, and a
//           little one after that
//   tap     a toe on the upbeat, and the hihat's: one sharp, bright click
//           and the floor's answer, and a small bounce
//   soft    a toe on the predown, and blowing's fill: duller, a few small
//           hits in the first 5ms, hardly any click
//   low     the snare's: the tap, a quarter lower all through, and more of
//           the floor
//   lower   the ride's: lower still, nearly half, and more floor again
//   board   the Stompy Feet's kick, in place of the thump: the same heel
//           and ball of the foot, but on a stomp board -- a nice one, a
//           hardwood top on a hollow box -- and miked, and EQ'd towards
//           a bass drum.  The box gives it a short boom of its own, around
//           85Hz, that the floor doesn't have; then the EQ, as an engineer
//           would on a board: a low shelf up under 110Hz, the box's honk
//           taken out around 450Hz, and the rumble under 45Hz, which the
//           shelf would otherwise bring up with the rest, taken out
//           altogether.  6dB louder than the thump, by perceived
//           loudness, and as much louder again stomped.
//
// And the Bare Feet's, below (bare_start), which are their own.
//
// The harder, towards a stomp, the louder, the heavier in the low, and the
// longer the floor answers; and the hits closer together, the foot coming
// down flatter.  And no two quite alike, as a player's aren't: each a
// little harder or softer, brighter or duller, longer or shorter, its hits
// a little closer or further apart, and a step on the grid a few
// milliseconds behind it, as a player is -- but not a pedal's, which comes
// when it's hit.  Handed from
// the tick to the audio thread through a ring, and heard at the start of
// the next block, as MIDI is.
// ---------------------------------------------------------------------------

#define FEET_IMPACTS 3
#define FEET_VOICES 16  // the Grid Hat's 32nds over the Feet, and the rest
#define FEET_RING 32          // steps waiting for the audio thread
#define FEET_LEVEL 0.9
#define FEET_WAVER 0.12       // how far a step's level wanders, each way
#define FEET_JITTER_MS 1.5    // and its hits' timing
#define FEET_STOMP_LOUDER 1.8 // a stomp, against a gentle step
#define FEET_STOMP_TIGHTER 0.45  // how much closer together its hits come
#define FEET_STOMP_LOWER 0.6  // how much more of the floor, and longer
#define FEET_TAIL_S 0.07
// How much no two are alike: how far each band's level, and each decay,
// wanders, each way; the filters; how hard it lands; and how late a step on
// the grid comes, at most.
#define FEET_VARY 0.1
#define FEET_COLOUR 0.06
#define FEET_HARD_VARY 0.06
#define FEET_LATE_MS 5.0

typedef struct {
  double ms, click, body, low;  // when, and how hard in each band
} FeetImpact;

typedef struct {
  FeetImpact impact[FEET_IMPACTS];
  double click, click_ms, body, body_ms, low, low_ms, low_swell_ms, tail;
  double level;
  double tone;  // its filters, against a tap's: lower is deeper
  // A stomp board's: its box's boom, how much of it and how long (its Q),
  // and the EQ on it, the low shelf's gain and how much of the honk comes
  // out.  None of these for the floor, which is all the rest.
  double boom_hz, boom_q, boom, shelf, honk_cut;
} FeetSound;

#define FEET_SHELF_HZ 110
#define FEET_HONK_HZ 450
#define FEET_HONK_Q 1.2
#define FEET_RUMBLE_HZ 45

static const FeetSound FEET_SOUNDS[N_FEET_KINDS] = {
  [FEET_THUMP] = {{{0, 0.3, 1.6, 1.6}, {16, 1, 1, 1}, {21, 0.4, 0.4, 0.3}},
                  0.55, 3, 0.75, 6, 1.3, 12, 3, 0.06, 1.3, 1},
  [FEET_TAP] = {{{0, 1, 1, 1}, {7, 0.25, 0.25, 0.3}, {0, 0, 0, 0}},
                1.3, 3, 0.4, 4, 1.6, 12, 7, 0.05, 1.0, 1},
  [FEET_TAP_SOFT] = {{{0, 0.6, 0.6, 0.7}, {1, 0.4, 0.4, 0.4},
                      {5.5, 0.5, 0.5, 0.5}},
                     0.35, 2.5, 0.25, 4, 1.4, 11, 5, 0.04, 0.95, 1},
  [FEET_TAP_LOW] = {{{0, 1, 1, 1}, {7, 0.25, 0.25, 0.3}, {0, 0, 0, 0}},
                    1.1, 3.5, 0.45, 5, 1.8, 14, 7, 0.05, 1.05, 0.75},
  [FEET_TAP_LOWER] = {{{0, 1, 1, 1}, {8, 0.25, 0.25, 0.3}, {0, 0, 0, 0}},
                      0.9, 4, 0.5, 6, 2.0, 16, 7, 0.05, 1.3, 0.55},
  [FEET_BOARD] = {{{0, 0.3, 1.6, 1.6}, {16, 1, 1, 1}, {21, 0.4, 0.4, 0.3}},
                  0.55, 3, 0.75, 6, 1.3, 12, 3, 0.06, 1.3, 1,
                  85, 6, 3, 2.2, 0.5},
};

typedef struct {
  int frames;  // left to sound, or 0 when free
  int at;      // frames since it started
  int impact_at[FEET_IMPACTS];
  double impact_click[FEET_IMPACTS], impact_body[FEET_IMPACTS],
    impact_low[FEET_IMPACTS], impact_tail[FEET_IMPACTS];
  double click, click_decay, body, body_decay, tail, tail_decay;
  double low_drive, low_decay, low, low_swell;
  // One-pole filters: the click's highpass, twice, and lowpass; the body's
  // band, two lowpasses less two; the low's, two less one; the tail's.
  double k_click_hp, k_click_lp, k_body_hi, k_body_lo, k_low, k_sub, k_tail;
  double c1, c2, c3, b1, b2, b3, b4, l1, l2, l3, t1, t2;
  // A stomp board's box and EQ, or boom 0 on the floor.
  Bandpass box, shelf_split, honk, rumble;
  double boom, shelf, honk_cut;
} FeetVoice;

typedef struct {
  int kind;
  float hard, level;
  bool on_grid;
} FeetStep;

static FeetStep feet_ring[FEET_RING];
static _Atomic unsigned feet_written, feet_read;
static FeetVoice feet_voices[FEET_VOICES];

// A step, from the tick or a pedal: `hard` 0 gentle to 1 a stomp, `level`
// 0-1, and whether it's on the grid.  Called with the lock held, so only
// ever one at a time.
static void feet_hit(int kind, double hard, double level, bool on_grid) {
  unsigned w = atomic_load_explicit(&feet_written, memory_order_relaxed);
  unsigned r = atomic_load_explicit(&feet_read, memory_order_acquire);
  if (w - r >= FEET_RING) return;  // the audio's stopped: nothing to hear
  feet_ring[w % FEET_RING] =
    (FeetStep){kind, (float)hard, (float)level, on_grid};
  atomic_store_explicit(&feet_written, w + 1, memory_order_release);
}

static double feet_k(double hz, double sample_rate) {
  return 1 - exp(-2 * M_PI * hz / sample_rate);
}

static double feet_decay(double ms, double sample_rate) {
  return exp(-1 / (sample_rate * ms / 1000));
}

// 1, give or take `by`, for this step.
static double feet_vary(double by) {
  return 1 + by * breath_noise();
}

static void feet_start(const FeetStep* step, double sample_rate) {
  FeetVoice* v = &feet_voices[0];
  for (int i = 0; i < FEET_VOICES; i++) {
    if (feet_voices[i].frames < v->frames) v = &feet_voices[i];
  }
  memset(v, 0, sizeof(*v));
  const FeetSound* s = &FEET_SOUNDS[step->kind];
  double hard = fmin(1, fmax(0, step->hard + FEET_HARD_VARY * breath_noise()));
  double level = FEET_LEVEL * s->level * step->level *
    pow(FEET_STOMP_LOUDER, hard) * feet_vary(FEET_WAVER);
  double lower = 1 + FEET_STOMP_LOWER * hard;
  double click = level * s->click * feet_vary(FEET_VARY);
  double body = level * s->body * feet_vary(FEET_VARY);
  double low = level * s->low * lower * feet_vary(FEET_VARY);
  double spread = feet_vary(FEET_VARY);  // its hits closer or further apart
  double late = step->on_grid ? FEET_LATE_MS * (breath_noise() + 1) / 2 : 0;
  for (int i = 0; i < FEET_IMPACTS; i++) {
    const FeetImpact* m = &s->impact[i];
    double ms = m->ms * (1 - FEET_STOMP_TIGHTER * hard) * spread;
    if (i) ms = fmax(0, ms + FEET_JITTER_MS * breath_noise());
    v->impact_at[i] = (int)(sample_rate * (late + ms) / 1000);
    v->impact_click[i] = click * m->click;
    v->impact_body[i] = body * m->body;
    v->impact_low[i] = low * m->low;
    v->impact_tail[i] = level * s->tail * lower *
      fmax(m->click, fmax(m->body, m->low));
  }
  v->click_decay = feet_decay(s->click_ms * feet_vary(FEET_VARY),
                              sample_rate);
  v->body_decay = feet_decay(s->body_ms * feet_vary(FEET_VARY), sample_rate);
  v->low_decay = feet_decay(s->low_ms * lower * feet_vary(FEET_VARY),
                            sample_rate);
  v->low_swell = 1 - feet_decay(s->low_swell_ms, sample_rate);
  v->tail_decay = feet_decay(FEET_TAIL_S * 1000 * lower, sample_rate);
  // Brighter or duller, all of it, and the lower taps lower.
  double colour = feet_vary(FEET_COLOUR) * s->tone;
  v->k_click_hp = feet_k(1200 * colour, sample_rate);
  v->k_click_lp = feet_k(7000 * colour, sample_rate);
  v->k_body_hi = feet_k(1500 * colour, sample_rate);
  v->k_body_lo = feet_k(400 * colour, sample_rate);
  v->k_low = feet_k(180 * feet_vary(FEET_COLOUR) * sqrt(s->tone),
                   sample_rate);
  v->k_sub = feet_k(35, sample_rate);
  v->k_tail = feet_k(1500, sample_rate);
  v->boom = s->boom;
  if (s->boom) {
    // A board's box sounds the same every time, near enough: only a little
    // of the colour.
    bandpass_set(&v->box, s->boom_hz * sqrt(colour / s->tone), s->boom_q,
                 sample_rate);
    bandpass_set(&v->shelf_split, FEET_SHELF_HZ, M_SQRT1_2, sample_rate);
    bandpass_set(&v->honk, FEET_HONK_HZ, FEET_HONK_Q, sample_rate);
    bandpass_set(&v->rumble, FEET_RUMBLE_HZ, M_SQRT1_2, sample_rate);
    v->shelf = s->shelf;
    v->honk_cut = s->honk_cut;
  }
  v->frames = (int)(sample_rate * ((late + 60) / 1000 +
                                   5 * FEET_TAIL_S * lower));
}

// ---------------------------------------------------------------------------
// The drum's Bare Feet kit: barefoot on a wooden floor, fitted to recordings
// of it -- each sound played eight times, some softly and some firmly, and
// the snare harder still.  And the Hand Drums, what the Bare Feet first
// were: the same, but their floor ringing at its modes' pitches, which
// sounded like hand drums, not feet.  Every one of them is the same two
// things:
//
//   slap    skin on the boards: a burst of noise, over in a millisecond or
//           two, with a quieter tail, the room; through five bands, 500Hz
//           to 8kHz, each as loud as the recording had it.  And a softer
//           step's foot comes down in two or three contacts a few
//           milliseconds apart, where a harder one lands all at once.
//   floor   the floor's own answer to the foot's push -- a sharp one for a
//           slap, a slow, heavy one for a kick -- and the push itself, the
//           thud, under 120Hz.  On the Bare Feet, noise, as loud as the
//           push and the room after it, through broad bands from 40 to
//           250Hz: the recordings have no pitch to them, and each hit's
//           strongest peak is only what noise has, somewhere different
//           every time.  On the Hand Drums, the floor's modes, rung: at 62,
//           83, 125 and 153Hz, and 50Hz, which a heel's weight on it pulls
//           the lowest down to, ringing on for a few tens of milliseconds
//           -- the same few pitches, every hit
//
// and what differs is only how much of each, and how quickly:
//
//   kick    the heel, all floor, no slap: soft, the light kick, a little
//           slap and a quick push; firm, a slow heavy push down to 50Hz
//   snare   the flat of the foot slapped down: soft, duller, in contacts;
//           firm, a sharp, bright slap, 500Hz up, and harder still more so
//   hihat   a lighter slap, the toes', mostly the floor's upper modes
//   ride    a slap like the snare's, lower: more floor, less bright
//
// Each recording's a take (BARE_TAKES), where on how hard the foot came
// down it was played (bare_force), and a step anywhere between two takes is
// in between them, all of it -- how loud, how bright, how sharp, how many
// contacts -- and past the hardest or softest, carries on the way the
// softest to the hardest go: at half the pace, and harder no more than
// BARE_LOUDER_DB louder for each 1 harder, so the hardest aren't unbearably
// loud, and softer at least BARE_SOFTER_DB quieter.  The hihat has only
// the one take, firm; its soft one is it with what the ride's soft one
// differs by.  And no two alike, as the recordings aren't: each a little
// harder or softer than it was played, its contacts a little closer or
// further apart, the floor a little higher or lower, and a step on the
// grid a few milliseconds behind it, as the Feet's are.
// ---------------------------------------------------------------------------

#define BARE_BANDS 5
#define BARE_MODES 5
#define BARE_CONTACTS 3       // after the first
#define BARE_VOICES 16
#define BARE_BAND_Q 1.4
#define BARE_FORCE_MAX 1.25
#define BARE_BEYOND 0.5       // past the takes, how fast it carries on
#define BARE_LOUDER_DB 12.0   // and how much louder, at most, per 1 harder
#define BARE_SOFTER_DB 30.0   // or softer, at least, per 1 softer
#define BARE_FADE_DB 30.0     // a part fading out of a take, how far
#define BARE_FORCE_VARY 0.04
#define BARE_LEVEL_VARY 0.1
#define BARE_TUNE_VARY 0.02
#define BARE_JITTER_MS 1.0
#define BARE_FLOOR_NOISE 1.2  // how rough the floor's push is
#define BARE_FLOOR_NOISE_HZ 300
#define BARE_RUMBLE_HZ 35     // and nothing much below the lowest mode
#define BARE_THUD_HZ 120      // the push itself, heard, below this
#define BARE_FLOOR_Q 1.4      // the Bare Feet's floor's bands: broad
// The recordings' level to ours, and each sound's trim (BARE_TAKES) against
// the rest: a firm kick as loud as the Feet's firm thump, and a firm snare
// as loud as it, as a kit's are, and the hihat and ride as loud as each
// other, 6dB under them.  As recorded, the snare was 12dB louder than
// the kick -- but a phone's mic hears little of a heel's 50Hz, and a kit
// wants its kick and snare about even.
#define BARE_LEVEL 1.0

static const double BARE_BAND_HZ[BARE_BANDS] = {500, 1000, 2000, 4000, 8000};
static const double BARE_MODE_HZ[BARE_MODES] = {50, 62, 83, 125, 153};
static const double BARE_FLOOR_HZ[BARE_MODES] = {40, 63, 100, 160, 250};

typedef struct {
  double force;                // how hard it was played (bare_force)
  double band[BARE_BANDS];     // the slap's bands, dB
  // The slap's rise, its burst's decay and how loud its tail is against it,
  // and the tail's decay, ms.
  double s_att, s_burst, s_tail, s_tail_ms;
  double mode[BARE_MODES];     // the floor's modes, dB
  double thud;                 // and the push itself, under them, dB
  // The foot's push on the floor, how quickly it comes and goes, the modes'
  // decay, ms, and how far up or down they're pulled.
  double f_att, f_push, f_ring, f_tune;
  // The foot's later contacts: when, ms, and how hard against the first.
  double at_ms[BARE_CONTACTS], contact[BARE_CONTACTS];
  // The Bare Feet's floor's tail, the room, against the push: f_ring is how
  // long it lasts.
  double f_tail;
} BareTake;

typedef struct {
  double trim;  // dB, against the recording: see BARE_LEVEL
  int n;
  BareTake take[3];
} BareSound;

static const BareSound BARE_TAKES[] = {
  [FEET_BARE_KICK - FEET_BARE_KICK] = {4.5, 2, {
    // light kick: its fit's peak came out 7dB under the recording's, and
    // it's brought up to it
    {0.3, {-83.8, -85.6, -87.4, -89.2, -91.0},
     0.82, 3.21, 0.10, 58.4,
     {-21.5, 2.21, -22.0, -53.1, -84.6}, -101.3,
     2.37, 5.07, 85.3, 1,
     {3.00, 10.0, 0}, {0.80, 0.40, 0}, 0.10},
    // kick
    {0.7, {-33.9, -31.2, -34.6, -41.9, -52.9},
     1.31, 0.50, 0.23, 40.4,
     {-68.6, -71.5, -74.5, -77.4, -80.4}, 6.46,
     25.0, 0.50, 55.5, 1,
     {8.00, 0, 0}, {0.60, 0, 0}, 0.41}}},
  [FEET_BARE_SNARE - FEET_BARE_KICK] = {-7.7, 3, {
    // soft
    {0.3, {-20.9, -10.2, -23.4, -47.4, -72.2},
     0.92, 5.28, 0.12, 31.6,
     {-14.3, 10.5, 3.32, -20.6, -45.5}, -94.1,
     1.13, 2.02, 59.4, 1,
     {4.00, 10.0, 14.0}, {0.90, 0.50, 0.35}, 0.12},
    // firm
    {0.7, {7.05, -5.31, -0.07, -20.1, -42.6},
     0.16, 2.31, 0.20, 46.6,
     {-17.8, 7.52, 13.4, -7.05, -30.1}, -95.1,
     2.18, 1.15, 63.2, 1,
     {13.0, 0, 0}, {0.28, 0, 0}, 0.35},
    // hard
    {0.85, {22.8, 11.6, 8.20, -12.5, -34.3},
     0.62, 0.50, 0.03, 60.9,
     {-13.6, 12.0, 8.84, -7.01, -25.7}, -111.5,
     3.54, 3.38, 52.1, 1,
     {3.00, 13.0, 0}, {0.50, 0.28, 0}, 0.34}}},
  [FEET_BARE_HIHAT - FEET_BARE_KICK] = {1.9, 2, {
    // not recorded: the firm one, less what the ride's soft one lacks
    {0.3, {-65.4, -31.9, -28.1, -53.3, -78.0},
     0.67, 1.16, 0.21, 72.1,
     {-37.0, -11.9, -33.7, -63.8, -93.6}, -102.2,
     0.38, 1.72, 76.3, 1,
     {2.00, 5.00, 13.0}, {1.00, 0.55, 0.50}, 0.31},
    // firm
    {0.7, {-12.0, -2.87, -13.5, -38.5, -63.8},
     1.20, 0.98, 0.07, 43.0,
     {-24.2, 1.26, -6.94, -32.6, -59.3}, -91.9,
     0.58, 3.10, 70.5, 1,
     {4.00, 10.0, 0}, {0.45, 0.30, 0}, 0.34}}},
  [FEET_BARE_RIDE - FEET_BARE_KICK] = {-1.9, 2, {
    // soft
    {0.3, {-45.0, -32.5, -23.9, -39.5, -56.8},
     0.60, 1.97, 0.15, 51.2,
     {-29.3, -4.03, -19.8, -45.6, -72.0}, -108.6,
     3.04, 2.24, 47.9, 1,
     {3.00, 9.00, 13.0}, {1.00, 0.50, 0.50}, 0.23},
    // firm
    {0.7, {8.45, -3.47, -9.31, -24.8, -42.6},
     1.07, 1.66, 0, 30.5,
     {-16.5, 9.11, 7.01, -14.5, -37.6}, -98.3,
     4.66, 4.04, 44.3, 1,
     {5.00, 14.0, 0}, {0.40, 0.25, 0}, 0.26}}},
  // The Hand Drums: the same recordings, fitted with the floor ringing at
  // its modes (bare_start).
  [FEET_HAND_KICK - FEET_BARE_KICK] = {4.0, 2, {
    // light kick
    {0.3, {-21.6, -52.5, -65.4, -60.1, -46.4},
     0.67, 1.75, 0.05, 76.2,
     {-69.0, -34.9, -2.07, -1.26, 12.3}, -33.4,
     7.52, 10.5, 72.3, 0.95,
     {3.00, 10.0, 0}, {0.80, 0.40, 0}},
    // kick
    {0.7, {-54.4, -50.5, -48.4, -55.2, -66.3},
     0.10, 0.50, 0.77, 120.0,
     {-23.0, 8.13, 24.3, 10.5, 18.0}, -7.30,
     16.4, 7.06, 90.0, 0.80,
     {8.00, 0, 0}, {0.60, 0, 0}}}},
  [FEET_HAND_SNARE - FEET_BARE_KICK] = {-7.9, 3, {
    // soft
    {0.3, {-1.00, -4.21, -21.2, -45.1, -69.6},
     2.08, 1.89, 0.06, 45.8,
     {-25.6, -2.66, -2.23, 15.8, 11.4}, -29.2,
     1.75, 3.97, 75.0, 1.00,
     {4.00, 10.0, 14.0}, {0.90, 0.50, 0.35}},
    // firm
    {0.7, {9.89, -4.33, 0.27, -20.0, -42.6},
     0.15, 1.85, 0.19, 51.8,
     {-23.2, -0.50, 0.66, 16.4, 15.8}, -25.2,
     2.17, 7.70, 75.0, 1.00,
     {13.0, 0, 0}, {0.28, 0, 0}},
    // hard
    {0.85, {13.3, 0.18, -3.56, -24.1, -45.9},
     0.10, 1.75, 0.12, 52.8,
     {0.13, 18.5, 16.6, 30.8, 32.0}, -9.77,
     2.81, 0.50, 75.0, 1.00,
     {3.00, 13.0, 0}, {0.50, 0.28, 0}}}},
  [FEET_HAND_HIHAT - FEET_BARE_KICK] = {0.4, 2, {
    // not recorded: the firm one, less what the ride's soft one lacks
    {0.3, {-43.0, -38.2, -41.9, -67.0, -91.4},
     0.08, 5.61, 0.20, 114.1,
     {-52.3, -19.7, -16.7, 1.87, 6.06}, -44.4,
     12.1, 5.37, 75.0, 1.00,
     {2.00, 5.00, 13.0}, {1.00, 0.55, 0.50}},
    // firm
    {0.7, {-7.28, -9.20, -23.4, -47.8, -72.4},
     0.95, 1.89, 0.13, 59.0,
     {-34.5, -8.69, -2.36, 8.53, 10.2}, -32.1,
     2.26, 4.20, 75.0, 1.00,
     {4.00, 10.0, 0}, {0.45, 0.30, 0}}}},
  [FEET_HAND_RIDE - FEET_BARE_KICK] = {-5.9, 2, {
    // soft
    {0.3, {-20.5, -27.7, -25.6, -42.5, -60.4},
     0.15, 1.94, 0.12, 68.7,
     {-38.1, -10.9, -9.41, -0.05, 4.31}, -37.2,
     4.30, 5.21, 75.0, 1.00,
     {3.00, 9.00, 13.0}, {1.00, 0.50, 0.50}},
    // firm
    {0.7, {15.3, 1.37, -7.14, -23.3, -41.5},
     1.80, 0.65, 0.04, 35.5,
     {-20.4, 0.14, 4.96, 6.61, 8.48}, -24.9,
     0.80, 4.08, 75.0, 1.00,
     {5.00, 14.0, 0}, {0.40, 0.25, 0}}}},
};

typedef struct {
  int frames;  // left to sound, or 0 when free
  int at;      // frames since it started
  int contact_at[BARE_CONTACTS + 1];
  double contact[BARE_CONTACTS + 1];
  // The slap: its burst's and tail's drive, decaying, and the envelope
  // rising towards them; its bands.
  double burst, burst_decay, tail, tail_decay, tail_amount, slap, k_att;
  Bandpass band[BARE_BANDS];
  double band_gain[BARE_BANDS];
  // The floor: the push's drive, decaying, and the push rising towards it,
  // roughened by lowpassed noise; its modes.
  double push_drive, push_decay, push, k_push, rough, k_rough;
  double floor_tail, floor_tail_decay, floor_tail_amount;
  bool noisy;  // the Bare Feet's floor, noise, rather than ringing
  Bandpass rumble, mode[BARE_MODES], thud;
  double mode_gain[BARE_MODES], thud_gain;
} BareVoice;

static BareVoice bare_voices[BARE_VOICES];

static double bare_mix(double a, double b, double u, bool log_scale) {
  if (log_scale) return exp(log(a) + (log(b) - log(a)) * u);
  return a + (b - a) * u;
}

// How loud a group of bands or modes is, all together, dB.
static double bare_sum_db(const double* db, int n) {
  double power = 0;
  for (int k = 0; k < n; k++) power += pow(10, db[k] / 10);
  return 10 * log10(power);
}

// The floor all together, its modes or bands and the thud, dB.
static double bare_floor_db(const BareTake* t) {
  double db[BARE_MODES + 1];
  memcpy(db, t->mode, sizeof(t->mode));
  db[BARE_MODES] = t->thud;
  return bare_sum_db(db, BARE_MODES + 1);
}

// A level, dB, between two takes' or past them, each first no more than
// BARE_FADE_DB under the loudest of its group (`a_most`, `b_most`): a part
// one has and the other hasn't so fades across, rather than dipping out of
// both halfway.
static double bare_level(double a, double a_most, double b, double b_most,
                         double u) {
  return bare_mix(fmax(a, a_most - BARE_FADE_DB),
                  fmax(b, b_most - BARE_FADE_DB), u, false);
}

static double bare_most(const double* db, int n) {
  double most = db[0];
  for (int k = 1; k < n; k++) most = fmax(most, db[k]);
  return most;
}

// The take `force` falls on, between the two it's between, or past the
// ends carrying on the way the softest to the hardest go.
static void bare_take(int kind, double force, BareTake* t) {
  const BareSound* s = &BARE_TAKES[kind - FEET_BARE_KICK];
  const BareTake* softest = &s->take[0];
  const BareTake* hardest = &s->take[s->n - 1];
  const BareTake *a = softest, *b = hardest;
  double u;
  if (force < softest->force || force > hardest->force) {
    u = (force - a->force) / (b->force - a->force);
    u = u < 0 ? BARE_BEYOND * u : 1 + BARE_BEYOND * (u - 1);
  } else {
    int i = 0;
    while (i + 2 < s->n && force > s->take[i + 1].force) i++;
    a = &s->take[i];
    b = &s->take[i + 1];
    u = (force - a->force) / (b->force - a->force);
  }
  double a_slap = bare_most(a->band, BARE_BANDS);
  double b_slap = bare_most(b->band, BARE_BANDS);
  double a_floor = fmax(bare_most(a->mode, BARE_MODES), a->thud);
  double b_floor = fmax(bare_most(b->mode, BARE_MODES), b->thud);
  for (int k = 0; k < BARE_BANDS; k++) {
    t->band[k] = bare_level(a->band[k], a_slap, b->band[k], b_slap, u);
  }
  for (int k = 0; k < BARE_MODES; k++) {
    t->mode[k] = bare_level(a->mode[k], a_floor, b->mode[k], b_floor, u);
  }
  t->thud = bare_level(a->thud, a_floor, b->thud, b_floor, u);
  // Past the hardest, the slap and the floor each no more than so much
  // louder, and past the softest, at least as much quieter.
  const BareTake* end = force < softest->force ? softest : hardest;
  double most = force < softest->force
    ? -BARE_SOFTER_DB * (softest->force - force)
    : BARE_LOUDER_DB * (force - hardest->force);
  if (force < softest->force || force > hardest->force) {
    double over = bare_sum_db(t->band, BARE_BANDS) -
      bare_sum_db(end->band, BARE_BANDS) - most;
    for (int k = 0; over > 0 && k < BARE_BANDS; k++) t->band[k] -= over;
    over = bare_floor_db(t) - bare_floor_db(end) - most;
    for (int k = 0; over > 0 && k < BARE_MODES; k++) t->mode[k] -= over;
    if (over > 0) t->thud -= over;
  }
  // How quickly it comes and goes, though, only as far as the softest: any
  // quicker would ring the floor's upper modes harder, and be louder for
  // it.  And on the Bare Feet no further than the hardest either, where
  // their snare's slap, ever shorter, would come to nothing.
  double ut = fmax(0, u);
  if (kind < FEET_HAND_KICK) ut = fmin(1, ut);
  t->s_att = bare_mix(a->s_att, b->s_att, ut, true);
  t->s_burst = bare_mix(a->s_burst, b->s_burst, ut, true);
  t->s_tail = fmax(0, bare_mix(a->s_tail, b->s_tail, ut, false));
  t->s_tail_ms = bare_mix(a->s_tail_ms, b->s_tail_ms, ut, true);
  t->f_att = bare_mix(a->f_att, b->f_att, ut, true);
  t->f_push = bare_mix(a->f_push, b->f_push, ut, true);
  t->f_ring = fmin(90, bare_mix(a->f_ring, b->f_ring, ut, true));
  t->f_tune = fmin(1.2, fmax(0.8, bare_mix(a->f_tune, b->f_tune, ut, false)));
  t->f_tail = fmax(0, bare_mix(a->f_tail, b->f_tail, ut, false));
  for (int k = 0; k < BARE_CONTACTS; k++) {
    t->at_ms[k] = fmax(0, bare_mix(a->at_ms[k], b->at_ms[k], u, false));
    t->contact[k] = fmin(1, fmax(0, bare_mix(a->contact[k], b->contact[k],
                                             u, false)));
  }
}

static double bare_db(double db) {
  return pow(10, db / 20);
}

static void bare_start(const FeetStep* step, double sample_rate) {
  BareVoice* v = &bare_voices[0];
  for (int i = 0; i < BARE_VOICES; i++) {
    if (bare_voices[i].frames < v->frames) v = &bare_voices[i];
  }
  memset(v, 0, sizeof(*v));
  double force = fmin(BARE_FORCE_MAX, fmax(0, step->hard)) +
    BARE_FORCE_VARY * breath_noise();
  BareTake t;
  bare_take(step->kind, force, &t);
  double level = BARE_LEVEL * step->level * feet_vary(BARE_LEVEL_VARY) *
    bare_db(BARE_TAKES[step->kind - FEET_BARE_KICK].trim);
  double late = step->on_grid ? FEET_LATE_MS * (breath_noise() + 1) / 2 : 0;
  v->contact_at[0] = (int)(sample_rate * late / 1000);
  v->contact[0] = 1;
  double last_ms = 0;
  for (int k = 0; k < BARE_CONTACTS; k++) {
    double ms = t.contact[k] > 0
      ? fmax(0.5, t.at_ms[k] * feet_vary(FEET_VARY) +
             BARE_JITTER_MS * breath_noise())
      : 0;
    v->contact_at[k + 1] = (int)(sample_rate * (late + ms) / 1000);
    v->contact[k + 1] = t.contact[k];
    last_ms = fmax(last_ms, ms);
  }
  v->k_att = feet_k(1000 / (2 * M_PI * t.s_att), sample_rate);
  v->burst_decay = feet_decay(t.s_burst, sample_rate);
  v->tail_decay = feet_decay(t.s_tail_ms, sample_rate);
  v->tail_amount = t.s_tail;
  for (int k = 0; k < BARE_BANDS; k++) {
    bandpass_set(&v->band[k], BARE_BAND_HZ[k], BARE_BAND_Q, sample_rate);
    v->band_gain[k] = level * bare_db(t.band[k]);
  }
  v->k_push = feet_k(1000 / (2 * M_PI * t.f_att), sample_rate);
  v->push_decay = feet_decay(t.f_push, sample_rate);
  v->k_rough = feet_k(BARE_FLOOR_NOISE_HZ, sample_rate);
  bandpass_set(&v->rumble, BARE_RUMBLE_HZ, M_SQRT1_2, sample_rate);
  v->noisy = step->kind < FEET_HAND_KICK;
  double tune = t.f_tune * feet_vary(BARE_TUNE_VARY);
  for (int k = 0; k < BARE_MODES; k++) {
    if (v->noisy) {
      // Broad bands of noise, as loud as the push and its tail.
      bandpass_set(&v->mode[k], BARE_FLOOR_HZ[k], BARE_FLOOR_Q, sample_rate);
    } else {
      // Ringing for f_ring: a Q of pi f tau.
      double hz = BARE_MODE_HZ[k] * tune;
      bandpass_set(&v->mode[k], hz, M_PI * hz * t.f_ring / 1000, sample_rate);
    }
    v->mode_gain[k] = level * bare_db(t.mode[k]);
  }
  if (v->noisy) {
    v->floor_tail_decay = feet_decay(t.f_ring, sample_rate);
    v->floor_tail_amount = t.f_tail;
  }
  bandpass_set(&v->thud, BARE_THUD_HZ, M_SQRT1_2, sample_rate);
  v->thud_gain = level * bare_db(t.thud);
  v->frames = (int)(sample_rate * ((late + last_ms) / 1000 +
                                   7 * fmax(t.f_ring, t.s_tail_ms) / 1000));
}

static void play_bare(float* left, float* right, int len,
                      double sample_rate) {
  for (int n = 0; n < BARE_VOICES; n++) {
    BareVoice* v = &bare_voices[n];
    if (!v->frames) continue;
    int run = len < v->frames ? len : v->frames;
    for (int i = 0; i < run; i++, v->at++) {
      for (int m = 0; m <= BARE_CONTACTS; m++) {
        if (v->at != v->contact_at[m] || !v->contact[m]) continue;
        v->burst += v->contact[m];
        v->tail += v->contact[m];
        v->push_drive += v->contact[m];
        v->floor_tail += v->contact[m];
      }
      double x = breath_noise();
      // The slap: noise, rising quickly to the burst and its tail.
      v->slap += (v->burst + v->tail_amount * v->tail - v->slap) * v->k_att;
      double slap = x * v->slap;
      double y = 0;
      for (int k = 0; k < BARE_BANDS; k++) {
        y += v->band_gain[k] * bandpass_run(&v->band[k], slap);
      }
      // The floor: the push, a little rough, ringing its modes -- or for
      // the Bare Feet, the push and the room after it, as loud as noise.
      v->push += (v->push_drive + v->floor_tail_amount * v->floor_tail -
                  v->push) * v->k_push;
      v->rough += (breath_noise() - v->rough) * v->k_rough;
      double push = v->push * (1 + BARE_FLOOR_NOISE * v->rough);
      push -= lowpass_run(&v->rumble, push);  // nothing the floor can't ring
      double floor = v->noisy ? breath_noise() * v->push : push;
      for (int k = 0; k < BARE_MODES; k++) {
        y += v->mode_gain[k] * bandpass_run(&v->mode[k], floor);
      }
      y += v->thud_gain * lowpass_run(&v->thud, push);
      v->burst *= v->burst_decay;
      v->tail *= v->tail_decay;
      v->push_drive *= v->push_decay;
      v->floor_tail *= v->floor_tail_decay;
      left[i] += (float)y;
      right[i] += (float)y;
    }
    v->frames -= run;
  }
}

static void play_feet(float* left, float* right, int len,
                      double sample_rate) {
  unsigned w = atomic_load_explicit(&feet_written, memory_order_acquire);
  unsigned r = atomic_load_explicit(&feet_read, memory_order_relaxed);
  for (; r != w; r++) {
    const FeetStep* step = &feet_ring[r % FEET_RING];
    if (step->kind >= FEET_BARE_KICK) bare_start(step, sample_rate);
    else feet_start(step, sample_rate);
  }
  atomic_store_explicit(&feet_read, r, memory_order_release);
  play_bare(left, right, len, sample_rate);
  for (int n = 0; n < FEET_VOICES; n++) {
    FeetVoice* v = &feet_voices[n];
    if (!v->frames) continue;
    int run = len < v->frames ? len : v->frames;
    for (int i = 0; i < run; i++, v->at++) {
      for (int m = 0; m < FEET_IMPACTS; m++) {
        if (v->at != v->impact_at[m]) continue;
        v->click += v->impact_click[m];
        v->body += v->impact_body[m];
        v->low_drive += v->impact_low[m];
        v->tail += v->impact_tail[m];
      }
      double x = breath_noise();
      // The click: highpassed twice, and the very top taken off.
      double c = x * v->click;
      v->c1 += (c - v->c1) * v->k_click_hp;
      c -= v->c1;
      v->c2 += (c - v->c2) * v->k_click_hp;
      c -= v->c2;
      v->c3 += (c - v->c3) * v->k_click_lp;
      // The body: 1.5kHz and down, less 400Hz and down.
      double b = x * v->body;
      v->b1 += (b - v->b1) * v->k_body_hi;
      v->b2 += (v->b1 - v->b2) * v->k_body_hi;
      v->b3 += (b - v->b3) * v->k_body_lo;
      v->b4 += (v->b3 - v->b4) * v->k_body_lo;
      // The low: swelling after each hit, 180Hz and down, less 35Hz.
      v->low += (v->low_drive - v->low) * v->low_swell;
      double l = x * v->low;
      v->l1 += (l - v->l1) * v->k_low;
      v->l2 += (v->l1 - v->l2) * v->k_low;
      v->l3 += (v->l2 - v->l3) * v->k_sub;
      // The room, after.
      double t = x * v->tail;
      v->t1 += (t - v->t1) * v->k_tail;
      v->t2 += (v->t1 - v->t2) * v->k_tail;
      double y = v->c3 + (v->b2 - v->b4) + 2 * (v->l2 - v->l3) + v->t2;
      if (v->boom) {
        // The box's boom under it, then the EQ: what's under the shelf
        // brought up, the honk taken out, and the rumble under it all.
        y += v->boom * bandpass_run(&v->box, y);
        y += (v->shelf - 1) * lowpass_run(&v->shelf_split, y) -
             v->honk_cut * bandpass_run(&v->honk, y);
        y -= lowpass_run(&v->rumble, y);
      }
      v->click *= v->click_decay;
      v->body *= v->body_decay;
      v->low_drive *= v->low_decay;
      v->tail *= v->tail_decay;
      left[i] += (float)y;
      right[i] += (float)y;
    }
    v->frames -= run;
  }
}

// ---------------------------------------------------------------------------
// Builds and drops
//
// Two more of the Breath Gate's own voices, for getting into and out of a
// big moment rather than keeping time.  Played by how hard you blow, like the
// sweeps, rather than by moving the breath like the scrapers:
//
//   Noise Riser     noise through a band that rises from 300Hz to 12kHz and
//                   gets louder as you blow harder
//   Wobble          two detuned saws and a sub on the bass note through a
//                   resonant low-pass that swings on the beat's grid, once a
//                   beat, then 2, 3 and 4 times as you blow harder
//
// Both stop, within a few milliseconds, when the breath does, so neither can
// carry on past what you're doing.  Summed in with the scrapers, after Kick
// Duck and the sweeps.
// ---------------------------------------------------------------------------

#define RISER_LEVEL 0.08
#define WOBBLE_LEVEL 0.045
#define BUILD_RELEASE_MS 12   // how quickly each lets go when you stop

// Whether the breath has opened, with the Breath Gate's hysteresis.  The
// audio thread's own.
static bool build_open;
static bool build_opened;  // opened this block

static void build_follow_breath(double blown) {
  build_opened = false;
  if (!build_open && blown > BREATH_GATE_OPEN) {
    build_open = true;
    build_opened = true;
  } else if (build_open && blown < BREATH_GATE_SHUT) {
    build_open = false;
  }
}

// One step of a gain towards on or off: fast on, a little slower off.
static double build_gain_step(double gain, bool on, double sample_rate) {
  double up = 1000 / (sample_rate * 3);
  double down = 1000 / (sample_rate * BUILD_RELEASE_MS);
  return on ? fmin(1, gain + up) : fmax(0, gain - down);
}

// One step of a level following the breath: smoothly up, over `rise_ms`, and
// down no slower than all the way in BUILD_RELEASE_MS, so stopping stops it.
static double build_follow(double level, double target, double rise_ms,
                           double sample_rate) {
  if (target > level) {
    return level + (target - level) *
      (1 - exp(-1 / (sample_rate * rise_ms / 1000)));
  }
  return fmax(target, level - 1000 / (sample_rate * BUILD_RELEASE_MS));
}

// The noise riser.
static struct {
  double level;
  double ic1[2], ic2[2];
} riser;

static void play_riser(float* left, float* right, int len, bool on,
                       double blown, double sample_rate) {
  double target = on && build_open ? blown : 0;
  if (target == 0 && riser.level == 0) {
    memset(&riser, 0, sizeof(riser));
    return;
  }
  for (int i = 0; i < len; i++) {
    riser.level = build_follow(riser.level, target, SWEEP_SMOOTH_MS,
                               sample_rate);
    double g = tan(M_PI * fmin(sweep_hz(300, 12000, riser.level),
                               0.45 * sample_rate) / sample_rate);
    double q = 1.4;
    double a1 = 1 / (1 + g * (g + 1 / q)), a2 = g * a1;
    double gain = RISER_LEVEL * pow(riser.level, 1.5);
    // One noise, not a pair: it all ends up on one channel (see
    // play_breath_instruments), and two uncorrelated noises folded together
    // would come out 3dB quieter.
    double v1 = a1 * riser.ic1[0] + a2 * (breath_noise() - riser.ic2[0]);
    double v2 = riser.ic2[0] + g * v1;
    riser.ic1[0] = 2 * v1 - riser.ic1[0];
    riser.ic2[0] = 2 * v2 - riser.ic2[0];
    float y = (float)(gain * (v1 / q + 0.3 * v2));
    left[i] += y;
    right[i] += y;
  }
}

// The wobble bass.
static struct {
  bool live;
  double hz;
  double saw[2];   // phases, 0-1
  double sub;      // phase, 0-1
  double gain;
  double cutoff;   // smoothed, 0-1
  double ic1, ic2;
  uint64_t origin_ns;
} wobble;

// A band-limited saw's correction near its wrap (polyBLEP).
static double poly_blep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1;
  }
  if (t > 1 - dt) {
    t = (t - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

static void play_wobble(float* left, float* right, int len, bool on,
                        double blown, double sample_rate) {
  bool held = on && build_open;
  if (!wobble.live) {
    if (!held) return;
    memset(&wobble, 0, sizeof(wobble));
    wobble.live = true;
    wobble.origin_ns = audio_block_ns;
    wobble.hz = midi_hz(
      atomic_load_explicit(&audio_bass_note, memory_order_relaxed) + 12);
  }
  if (build_opened) wobble.origin_ns = audio_block_ns;
  double to_hz = midi_hz(
    atomic_load_explicit(&audio_bass_note, memory_order_relaxed) + 12);
  double glide = 1 - exp(-1 / (sample_rate * 0.015));
  int rate = blown < 0.35 ? 1 : blown < 0.6 ? 2 : blown < 0.8 ? 3 : 4;
  double smooth = 1 - exp(-1 / (sample_rate * 0.002));
  double q = 3.5;
  for (int i = 0; i < len; i++) {
    wobble.gain = build_gain_step(wobble.gain, held, sample_rate);
    wobble.hz += (to_hz - wobble.hz) * glide;

    uint64_t t = audio_block_ns + (uint64_t)(i * 1e9 / sample_rate);
    double beats = audio_grid_beats(t, wobble.origin_ns);
    double lfo = 0.5 - 0.5 * cos(2 * M_PI * beats * rate);
    wobble.cutoff += (lfo - wobble.cutoff) * smooth;

    double y = 0;
    for (int v = 0; v < 2; v++) {
      double dt = wobble.hz * (v ? 1.004 : 0.996) / sample_rate;
      wobble.saw[v] += dt;
      if (wobble.saw[v] >= 1) wobble.saw[v] -= 1;
      y += 0.5 * (2 * wobble.saw[v] - 1 - poly_blep(wobble.saw[v], dt));
    }
    wobble.sub += wobble.hz / 2 / sample_rate;
    if (wobble.sub >= 1) wobble.sub -= 1;
    double sub = 0.6 * sin(2 * M_PI * wobble.sub);

    double g = tan(M_PI * fmin(sweep_hz(90, 3600, wobble.cutoff),
                               0.45 * sample_rate) / sample_rate);
    double a1 = 1 / (1 + g * (g + 1 / q)), a2 = g * a1, a3 = g * a2;
    double v3 = y - wobble.ic2;
    double v1 = a1 * wobble.ic1 + a2 * v3;
    double v2 = wobble.ic2 + a2 * wobble.ic1 + a3 * v3;
    wobble.ic1 = 2 * v1 - wobble.ic1;
    wobble.ic2 = 2 * v2 - wobble.ic2;

    double out = tanh(2 * (v2 + sub)) / tanh(2);
    float sample = (float)(out * wobble.gain * WOBBLE_LEVEL *
                           (0.6 + 0.4 * blown));
    left[i] += sample;
    right[i] += sample;
  }
  if (!held && wobble.gain == 0) wobble.live = false;
}

static void render_breath_instruments(float* left, float* right, int len,
                                      double sample_rate) {
  unsigned fx = atomic_load_explicit(&audio_breath_fx, memory_order_relaxed);
  double blown = breath_blown(
    atomic_load_explicit(&audio_breath, memory_order_relaxed));

  play_riser(left, right, len, fx & BREATH_FX_RISER, blown, sample_rate);
  play_wobble(left, right, len, fx & BREATH_FX_WOBBLE, blown, sample_rate);

  bool on = fx & BREATH_FX_GUIRO;
  if (breath_player_run(&guiro_player, on, &guiro, sizeof(guiro),
                        sample_rate, len)) {
    play_scraper(&guiro, &guiro_player, &GUIRO_SOUND, left, right, len, on,
                 blown, sample_rate);
  }
  on = fx & BREATH_FX_WASHBOARD;
  if (breath_player_run(&washboard_player, on, &washboard, sizeof(washboard),
                        sample_rate, len)) {
    play_scraper(&washboard, &washboard_player, &WASHBOARD_SOUND, left, right,
                 len, on, blown, sample_rate);
  }
  on = fx & BREATH_FX_GUIRA;
  if (breath_player_run(&guira_player, on, &guira, sizeof(guira),
                        sample_rate, len)) {
    play_scraper(&guira, &guira_player, &GUIRA_SOUND, left, right, len, on,
                 blown, sample_rate);
  }
  on = fx & BREATH_FX_BRUSH;
  if (breath_player_run(&brush_player, on, &brush, sizeof(brush),
                        sample_rate, len)) {
    play_brush(left, right, len, on, blown, sample_rate);
  }
  on = fx & BREATH_FX_TAMB;
  if (breath_player_run(&tamb_player, on, &tamb, sizeof(tamb), sample_rate,
                        len)) {
    play_tamb(left, right, len, on, blown, sample_rate);
  }
  float gain = atomic_load_explicit(&audio_breath_gain, memory_order_relaxed);
  for (int i = 0; i < len; i++) {
    left[i] *= gain;
    right[i] *= gain;
  }
}

// The right channel's level, as a proportion of the global volume.  Voices
// play on the left unless CH swaps them to the right, and the right goes to a
// talkbox shared with a mandolin, so it wants matching to that by ear rather
// than to the rest of the rig.  Scaled after everything is mixed, on the
// audio thread, hence atomic.
#define MAX_ALT_CHANNEL_GAIN 2.0
static _Atomic float alt_channel_gain = 1.0f;

// The Breath Gate's own sounds, on one channel like everything else: the
// left, or the right if its CH is on.  They're made in stereo -- the brushes
// circle from side to side -- so each is folded down to its middle, which is
// what either side of it carries on average.  And the Feet, which are the
// drum's, on its side, and at its level (they come with it).
#define BREATH_CHUNK 512
static void play_breath_instruments(float* left, float* right, int len,
                                    double sample_rate) {
  static float l[BREATH_CHUNK], r[BREATH_CHUNK];
  unsigned fx = atomic_load_explicit(&audio_breath_fx, memory_order_relaxed);
  float* out = (fx & BREATH_FX_RIGHT) ? right : left;
  for (int done = 0; done < len; done += BREATH_CHUNK) {
    int n = len - done < BREATH_CHUNK ? len - done : BREATH_CHUNK;
    memset(l, 0, sizeof(l));
    memset(r, 0, sizeof(r));
    render_breath_instruments(l, r, n, sample_rate);
    for (int i = 0; i < n; i++) out[done + i] += (l[i] + r[i]) / 2;
  }
  float* drum = (fx & BREATH_FX_DRUM_RIGHT) ? right : left;
  float gain = atomic_load_explicit(&drum_level, memory_order_relaxed);
  for (int done = 0; done < len; done += BREATH_CHUNK) {
    int n = len - done < BREATH_CHUNK ? len - done : BREATH_CHUNK;
    memset(l, 0, sizeof(l));
    memset(r, 0, sizeof(r));
    play_feet(l, r, n, sample_rate);
    for (int i = 0; i < n; i++) drum[done + i] += gain * (l[i] + r[i]) / 2;
  }
}

static int jammer_audio_render(void* data, int len, int nfx, float** fx,
                               int nout, float** out) {
  audio_frames_rendered += len;
  if (len > audio_block_frames) audio_block_frames = len;
  // Cleared before the synth writes into them rather than trusted to arrive
  // clean: fluidsynth's docs don't promise either way, and a mix hook that
  // added into a buffer still holding its own last block would run away.
  for (int i = 0; i < nout; i++) {
    memset(out[i], 0, (size_t)len * sizeof(float));
  }
  for (int i = 0; i < nfx; i++) {
    memset(fx[i], 0, (size_t)len * sizeof(float));
  }
  audio_block_ns = now();
  int result = render_kick_ducked((fluid_synth_t*)data, len, nfx, fx, nout,
                                  out, synth_sample_rate);
  if (nout >= 2) {
    build_follow_breath(breath_blown(
      atomic_load_explicit(&audio_breath, memory_order_relaxed)));
    apply_sweeps(out[0], out[1], len, synth_sample_rate);
    play_breath_instruments(out[0], out[1], len, synth_sample_rate);
  }
  if (audio_mix_hook) {
    audio_mix_hook(out, nout, len, synth_sample_rate);
  }
  float alt = atomic_load_explicit(&alt_channel_gain, memory_order_relaxed);
  if (nout >= 2 && alt != 1.0f) {
    for (int i = 0; i < len; i++) out[1][i] *= alt;
  }
  if (audio_after_alt_hook) audio_after_alt_hook(out, nout, len);
  return result;
}

// True if the driver has pulled audio from us recently.  Opening a device
// tells you nothing; this tells you it's running.
bool audio_is_flowing(void) {
  uint64_t before = audio_frames_rendered;
  usleep(300000);
  return audio_frames_rendered > before;
}

// Global output volume, on top of the per-voice levels in voices.h.  1.0 is
// what the Pi's run-fluidsynth.sh uses; the Audio Output menu's slider moves
// it when a room or a PA wants more or less.
#define MAX_SYNTH_GAIN 2.0
double synth_gain = 1.0;

fluid_settings_t* fl_settings = NULL;
fluid_synth_t* fl_synth = NULL;
fluid_audio_driver_t* fl_driver = NULL;
int fl_sfont_id = -1;

// Where to look for FluidR3_GM.sf2, in priority order.  $JAMMER_SOUNDFONT
// wins, then anything bundled next to the binary, then the usual homebrew and
// Linux locations.
const char* SOUNDFONT_PATHS[] = {
  "FluidR3_GM.sf2",
  "soundfonts/FluidR3_GM.sf2",
  "/opt/homebrew/share/soundfonts/FluidR3_GM.sf2",
  "/usr/local/share/soundfonts/FluidR3_GM.sf2",
  "/usr/share/sounds/sf2/FluidR3_GM.sf2",
  NULL
};

// Fills buf with the first soundfont we can read, or returns false.  bundle_dir
// may be NULL; when set it's checked (with each relative path above) first.
bool find_soundfont(const char* bundle_dir, char* buf, size_t buf_len) {
  const char* from_env = getenv("JAMMER_SOUNDFONT");
  if (from_env && access(from_env, R_OK) == 0) {
    snprintf(buf, buf_len, "%s", from_env);
    return true;
  }

  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0 && !bundle_dir) continue;
    for (int i = 0; SOUNDFONT_PATHS[i]; i++) {
      if (pass == 0) {
        snprintf(buf, buf_len, "%s/%s", bundle_dir, SOUNDFONT_PATHS[i]);
      } else if (SOUNDFONT_PATHS[i][0] != '/') {
        continue;  // relative paths only make sense against bundle_dir
      } else {
        snprintf(buf, buf_len, "%s", SOUNDFONT_PATHS[i]);
      }
      if (access(buf, R_OK) == 0) {
        return true;
      }
    }
  }
  return false;
}

// Names of the CoreAudio output devices fluidsynth can see, plus "default".
// Returns how many were written.
struct DeviceCollect { char (*names)[256]; int n; int max; };

static void collect_device_name(void* data, const char* setting,
                                const char* option) {
  struct DeviceCollect* collect = (struct DeviceCollect*)data;
  if (collect->n < collect->max) {
    snprintf(collect->names[collect->n++], 256, "%s", option);
  }
}

int list_audio_devices(char names[][256], int max_names) {
  struct DeviceCollect collect = {names, 0, max_names};

  fluid_settings_t* settings = fl_settings;
  bool temporary = false;
  if (!settings) {
    settings = new_fluid_settings();
    fluid_settings_setstr(settings, "audio.driver", "coreaudio");
    temporary = true;
  }
  fluid_settings_foreach_option(settings, AUDIO_DEVICE_SETTING,
                               &collect, collect_device_name);
  if (temporary) delete_fluid_settings(settings);
  return collect.n;
}

// Accepts an exact device name or any substring of one, so "Scarlett" finds
// "Scarlett 2i2 USB".  Falls back to "default" if nothing matches.
void resolve_audio_device(const char* wanted, char* out, size_t out_len) {
  snprintf(out, out_len, "default");
  if (!wanted || !*wanted) return;

  char names[32][256];
  int n = list_audio_devices(names, 32);
  for (int i = 0; i < n; i++) {
    if (strcmp(names[i], wanted) == 0) {
      snprintf(out, out_len, "%s", names[i]);
      return;
    }
  }
  for (int i = 0; i < n; i++) {
    if (strstr(names[i], wanted) != NULL) {
      snprintf(out, out_len, "%s", names[i]);
      return;
    }
  }
  printf("no audio device matching \"%s\"; using the system default\n", wanted);
}

// Swap the output device without disturbing the synth, so notes that are
// sounding keep their state.  Returns false and falls back to the system
// default if the device won't open.
bool set_audio_device(const char* wanted) {
  char resolved[256];
  resolve_audio_device(wanted, resolved, sizeof(resolved));

  if (fl_driver) {
    delete_fluid_audio_driver(fl_driver);
    fl_driver = NULL;
  }
  fluid_settings_setstr(fl_settings, AUDIO_DEVICE_SETTING, resolved);
  fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                      fl_synth);

  // An open device that never pulls is the failure mode we care about, so
  // check before believing it, and back off to safe buffering if need be.
  if (fl_driver && !audio_is_flowing()) {
    printf("audio device \"%s\" opened but isn't pulling samples; "
           "retrying with fluidsynth's own buffering\n", resolved);
    delete_fluid_audio_driver(fl_driver);

    // Put the buffering back to fluidsynth's defaults -- actually changing
    // them, rather than setting the same values again.
    int default_periods = 0, default_period_size = 0;
    fluid_settings_getint_default(fl_settings, "audio.periods",
                                  &default_periods);
    fluid_settings_getint_default(fl_settings, "audio.period-size",
                                  &default_period_size);
    fluid_settings_setint(fl_settings, "audio.periods", default_periods);
    fluid_settings_setint(fl_settings, "audio.period-size",
                          default_period_size);

    fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                        fl_synth);
    if (fl_driver && !audio_is_flowing()) {
      printf("still no audio from \"%s\" -- try another output device\n",
             resolved);
    }
  }

  if (!fl_driver) {
    printf("couldn't open audio device \"%s\"; falling back to default\n",
           resolved);
    snprintf(resolved, sizeof(resolved), "default");
    fluid_settings_setstr(fl_settings, AUDIO_DEVICE_SETTING, resolved);
    fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                        fl_synth);
    if (!fl_driver) die("couldn't open any audio output");
    snprintf(audio_device, sizeof(audio_device), "%s", resolved);
    return false;
  }

  snprintf(audio_device, sizeof(audio_device), "%s", resolved);
  printf("audio output: %s (%llu frames rendered so far)\n", audio_device,
         (unsigned long long)audio_frames_rendered);
  return true;
}

void set_synth_gain(double gain) {
  if (gain < 0) gain = 0;
  if (gain > MAX_SYNTH_GAIN) gain = MAX_SYNTH_GAIN;
  synth_gain = gain;
  if (fl_synth) fluid_synth_set_gain(fl_synth, (float)gain);
}

void set_alt_channel_gain(double gain) {
  if (gain < 0) gain = 0;
  if (gain > MAX_ALT_CHANNEL_GAIN) gain = MAX_ALT_CHANNEL_GAIN;
  atomic_store_explicit(&alt_channel_gain, (float)gain, memory_order_relaxed);
}

// The Breath Gate's brushes, on their own kit's channel, go where the
// Breath Gate does: left, or right with its CH on.  From breath_set, which
// is often, so only when that changes.  (The Grid Hat's is the drum's now,
// and goes where the drum does, with the rest of its CCs.)
static bool breath_kits_right = false;

static void pan_breath_kits(bool right) {
  if (!fl_synth || right == breath_kits_right) return;
  breath_kits_right = right;
  int value = right ? 127 : 0;
  fluid_synth_cc(fl_synth, CHANNEL_BRUSH, CC_PAN, value);
  fluid_synth_cc(fl_synth, CHANNEL_BRUSH, CC_BALANCE, value);
}

// Mirrors run-fluidsynth.sh: -c 2 -z 64 -g 1.0, stereo, reverb/chorus off.
void start_synth(const char* soundfont_path, const char* device) {
  fl_settings = new_fluid_settings();
  if (!fl_settings) die("couldn't create fluidsynth settings");

  fluid_settings_setstr(fl_settings, "audio.driver", "coreaudio");
  // Only override the buffering if asked; see the note above.
  const char* periods_env = getenv("JAMMER_PERIODS");
  if (periods_env) {
    fluid_settings_setint(fl_settings, "audio.periods", atoi(periods_env));
  }
  const char* period_env = getenv("JAMMER_PERIOD_SIZE");
  if (period_env) {
    fluid_settings_setint(fl_settings, "audio.period-size", atoi(period_env));
  }
  fluid_settings_setnum(fl_settings, "synth.sample-rate", synth_sample_rate);
  fluid_settings_setnum(fl_settings, "synth.gain", 1.0);
  // A stereo pair for each MIDI channel, so Kick Duck can mix them itself.
  // Put back to one before the driver is made, below: the CoreAudio driver
  // reads the same setting as how many channels to open the device with.
  fluid_settings_setint(fl_settings, "synth.audio-channels", SYNTH_CHANNELS);
  fluid_settings_setint(fl_settings, "synth.audio-groups", SYNTH_CHANNELS);
  // In sixteens, as fluidsynth has them.
  fluid_settings_setint(fl_settings, "synth.midi-channels",
                        (SYNTH_CHANNELS + 15) / 16 * 16);
  fluid_settings_setint(fl_settings, "synth.reverb.active", 0);
  fluid_settings_setint(fl_settings, "synth.chorus.active", 0);
  // Channel 9 is percussion, as on the Pi (ENDPOINT_DRUM == CHANNEL_DRUM == 9).
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");

  fl_synth = new_fluid_synth(fl_settings);
  if (!fl_synth) die("couldn't create fluidsynth synth");
  fluid_settings_setint(fl_settings, "synth.audio-channels", 1);
  fluid_synth_set_channel_type(fl_synth, CHANNEL_KICK, CHANNEL_TYPE_DRUM);
  fluid_synth_set_channel_type(fl_synth, CHANNEL_BRUSH, CHANNEL_TYPE_DRUM);
  fluid_synth_set_channel_type(fl_synth, CHANNEL_HAT, CHANNEL_TYPE_DRUM);
  // Everything on the left until CH says otherwise: fluidsynth starts each
  // channel in the middle, which would put a little of anything nothing's
  // panned yet on the right, the alternate channel.
  for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
    fluid_synth_cc(fl_synth, ch, CC_PAN, 0);
    fluid_synth_cc(fl_synth, ch, CC_BALANCE, 0);
  }
  breath_kits_right = false;

  fl_sfont_id = fluid_synth_sfload(fl_synth, soundfont_path, 1);
  if (fl_sfont_id == FLUID_FAILED) {
    printf("failed to load soundfont %s\n", soundfont_path);
    die("couldn't load soundfont");
  }
  printf("loaded soundfont %s\n", soundfont_path);
  fluid_synth_program_select(fl_synth, CHANNEL_BRUSH, fl_sfont_id,
                             PERCUSSION_BANK, BRUSH_KIT);
  fluid_synth_program_select(fl_synth, CHANNEL_HAT, fl_sfont_id,
                             PERCUSSION_BANK, HAT_KIT);

  set_synth_gain(synth_gain);
  set_audio_device(device);
}

void stop_synth() {
  if (fl_driver) delete_fluid_audio_driver(fl_driver);
  if (fl_synth) delete_fluid_synth(fl_synth);
  if (fl_settings) delete_fluid_settings(fl_settings);
  fl_driver = NULL;
  fl_synth = NULL;
  fl_settings = NULL;
}

// Everything sent, for test-keypad.c to watch.  NULL otherwise.
static void (*midi_tap)(int action, int note, int velocity, int channel) = NULL;

void send_midi(int action, int note, int velocity, int endpoint) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;

  if (midi_tap) midi_tap(action, note, velocity, endpoint);

  if (!fl_synth) return;  // no synth yet, or we're shutting down

  int channel = endpoint;

  if (action == MIDI_CC) {
    fluid_synth_cc(fl_synth, channel, note, velocity);
    // The kick's channel is the drum's, split off: same volume, pan, fade.
    // And so is the Grid Hat's.  And the brushes', on their own kit -- all
    // but their pan, which is the Breath Gate's (pan_breath_kits).
    if (channel == CHANNEL_DRUM) {
      fluid_synth_cc(fl_synth, CHANNEL_KICK, note, velocity);
      fluid_synth_cc(fl_synth, CHANNEL_HAT, note, velocity);
      if (note != CC_PAN && note != CC_BALANCE) {
        fluid_synth_cc(fl_synth, CHANNEL_BRUSH, note, velocity);
      }
    }
    // And a drone's voice channels are the drone's.
    int base = voice_channel_base(channel);
    for (int k = 0; base >= 0 && k < VOICE_CHANNELS_PER_DRONE; k++) {
      fluid_synth_cc(fl_synth, base + k, note, velocity);
    }
  } else if (action == MIDI_ON) {
    fluid_synth_noteon(fl_synth, channel, note, velocity);
  } else if (action == MIDI_OFF) {
    // fluidsynth returns FLUID_FAILED for note-offs on notes that aren't
    // sounding, which happens constantly and normally.  Ignore it.
    fluid_synth_noteoff(fl_synth, channel, note);
  } else {
    printf("unknown action %d\n", action);
  }
}

// Bend a voice channel by this many semitones.
void voice_bend(int channel, double semitones) {
  if (!fl_synth) return;
  fluid_synth_pitch_wheel_sens(fl_synth, channel, VOICE_BEND_RANGE);
  int value = 8192 + (int)lround(semitones / VOICE_BEND_RANGE * 8192);
  fluid_synth_pitch_bend(fl_synth, channel,
                         value < 0 ? 0 : value > 16383 ? 16383 : value);
}

void choose_voice(int channel, int bank, int voice) {
  if (bank < 0) bank = 0;
  // PERCUSSION_BANK, not 127: clamping to 127 turned every request for a
  // drum kit into a bank that doesn't exist, and the fallback below then put
  // a grand piano on the drum channel.
  if (bank > PERCUSSION_BANK) bank = PERCUSSION_BANK;
  if (voice < 0) voice = 0;
  if (voice > 127) voice = 127;

  if (!fl_synth) return;

  if (fluid_synth_program_select(fl_synth, channel, fl_sfont_id,
                                 bank, voice) == FLUID_FAILED) {
    // Not every bank/preset combination exists in the soundfont.  Fall back
    // within the same family -- the first percussion set for a drum kit,
    // bank 0 for a melodic voice -- rather than leaving the channel on
    // whatever it had, or swapping a kit for a piano.
    int fallback_bank = bank == PERCUSSION_BANK ? PERCUSSION_BANK : 0;
    int fallback_voice = bank == PERCUSSION_BANK ? 0 : voice;
    printf("no voice %d-%d in the soundfont; falling back to %d-%d\n",
           bank, voice, fallback_bank, fallback_voice);
    fluid_synth_program_select(fl_synth, channel, fl_sfont_id,
                               fallback_bank, fallback_voice);
  }
  printf("set endpoint #%d to voice %d-%d\n", channel, bank, voice);
  if (channel == CHANNEL_DRUM) choose_voice(CHANNEL_KICK, bank, voice);
  int base = voice_channel_base(channel);
  for (int k = 0; base >= 0 && k < VOICE_CHANNELS_PER_DRONE; k++) {
    int got_bank, got_voice, sfont;
    fluid_synth_get_program(fl_synth, channel, &sfont, &got_bank, &got_voice);
    fluid_synth_program_select(fl_synth, base + k, fl_sfont_id, got_bank,
                               got_voice);
  }
}

#endif
