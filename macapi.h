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
// And the Breath Gate's brushes and Grid Hat, past the drones' voice
// channels, below.
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

static void breath_set(int breath, unsigned fx) {
  atomic_store_explicit(&audio_breath, breath, memory_order_relaxed);
  atomic_store_explicit(&audio_breath_fx, fx, memory_order_relaxed);
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
// scrub, and it holds there, under the slap past 90% (jammermidilib.h's
// breath_brush_slap).  Noise through the head's swish and the
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
    for (int ch = 0; ch < 2; ch++) {
      double v1 = a1 * riser.ic1[ch] + a2 * (breath_noise() - riser.ic2[ch]);
      double v2 = riser.ic2[ch] + g * v1;
      riser.ic1[ch] = 2 * v1 - riser.ic1[ch];
      riser.ic2[ch] = 2 * v2 - riser.ic2[ch];
      float y = (float)(gain * (v1 / q + 0.3 * v2));
      if (ch == 0) left[i] += y; else right[i] += y;
    }
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

static void play_breath_instruments(float* left, float* right, int len,
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
    // And so are the brushes' and the Grid Hat's, on their own kits.
    if (channel == CHANNEL_DRUM) {
      fluid_synth_cc(fl_synth, CHANNEL_KICK, note, velocity);
      fluid_synth_cc(fl_synth, CHANNEL_BRUSH, note, velocity);
      fluid_synth_cc(fl_synth, CHANNEL_HAT, note, velocity);
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
