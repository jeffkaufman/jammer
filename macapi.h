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

#include "common.h"

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
// The mandolin's chord: the root's MIDI note, then the third and fifth above it
// in semitones, a byte each.
static _Atomic unsigned audio_chord;

static void breath_set(const BreathState* state) {
  atomic_store_explicit(&audio_breath, state->breath, memory_order_relaxed);
  atomic_store_explicit(&audio_breath_fx, state->fx, memory_order_relaxed);
  atomic_store_explicit(&audio_chord,
                        (unsigned)state->chord_root << 16 |
                        (unsigned)state->chord_third << 8 |
                        (unsigned)state->chord_fifth,
                        memory_order_relaxed);
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

#define SYNTH_CHANNELS 32  // MIDI channels, each rendered to its own pair
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

    float* left = out[0] + done;
    float* right = out[1] + done;
    for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
      if (kick_duck_exempt(ch) || ch == ENDPOINT_BREATH) continue;
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
// The Breath Gate's percussion voices (N and M, and for now A, S, D and G,
// with it selected): played by moving the breath rather than by how hard it is, so
// holding it steady is silence and every sound is set off by a movement.
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
//   Mandolin      the ridges are courses of an electric mandolin, tuned to
//                 the chord the drones are playing, root, third and fifth
//                 up from the mandolin's low G: breathe up and it strums up,
//                 down and it strums down.  Played with the palm resting on
//                 the strings, so each note is a short, dull "chk" at its
//                 pitch rather than a ring.  Plucked strings, Karplus-Strong,
//                 two to a course a few cents apart, through a pickup's
//                 presence and a little overdrive.
//   Cuica         the samba friction drum: a squeaking, vocal tone while the
//                 breath moves, louder the faster, its pitch how far into the
//                 breath it is -- up-strokes whoop up and down-strokes fall.
//   Talking Drum  struck at the start of each stroke, harder the quicker the
//                 stroke starts, and its pitch bends with the breath while it
//                 rings.
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

#define MANDO_COURSES 8           // across the breath's range
#define MANDO_LOWEST 55           // G3, the mandolin's low string
#define MANDO_RING_MS 150         // muted: each note's gone in this long
#define MANDO_DAMP 0.45           // the palm on the strings: lower is duller
#define MANDO_DETUNE_CENTS 2      // each string of a course, either way
#define MANDO_DRIVE 2.0
#define MANDO_LEVEL 0.07

#define CUICA_LOW_HZ 250
#define CUICA_HIGH_HZ 900
#define CUICA_FULL_SPEED 4.0      // ranges a second for full voice
#define CUICA_LEVEL 0.07

#define TALK_LOW_HZ 90
#define TALK_HIGH_HZ 180
#define TALK_MOVING 0.4           // ranges a second: slower is at rest
#define TALK_FULL_SPEED 4.0       // a stroke this quick strikes hardest
#define TALK_LISTEN_MS 8          // how long into a stroke it's judged
#define TALK_LEVEL 0.02
#define TALK_RISE_MS 1.5          // a strike's rise, so it doesn't click

#define SPEED_SMOOTH_MS 8         // how the breath's speed is smoothed

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

// The breath's speed, in ranges a second, smoothed; signed.
static double breath_speed(double* speed, double moved, double sample_rate) {
  double ks = 1 - exp(-1 / (sample_rate * SPEED_SMOOTH_MS / 1000));
  *speed += (moved * sample_rate - *speed) * ks;
  return *speed;
}

static double midi_hz(double note) {
  return 440 * pow(2, (note - 69) / 12);
}

// The mandolin: plucked strings, each a delay line round which a burst of
// noise is filtered a little more each trip (Karplus-Strong).  The filter is
// the palm: a lowpass, so the highs die first, and a loss each trip set so
// every note is gone in MANDO_RING_MS whatever its pitch.  Tuned exactly by
// an allpass for the part of the delay the line and the lowpass don't make.
#define MANDO_MAX_DELAY 2048
typedef struct {
  float line[MANDO_MAX_DELAY];
  int len, pos;
  double lp;                 // the palm's lowpass
  double keep;               // kept each trip round
  double ap, ap_in, ap_out;  // the allpass: coefficient and state
} KsString;

typedef struct {
  KsString strings[MANDO_COURSES][2];
  Bandpass presence;
  int zone;      // how many courses are below the breath
  bool started;  // zone is known: nothing to pluck on the first frame
} Mandolin;

static BreathPlayer mando_player;
static Mandolin mando;

// A first-order allpass's delay at w, in samples.
static double allpass_delay(double c, double w) {
  return -(atan2(-sin(w), c + cos(w)) - atan2(-c * sin(w), 1 + c * cos(w))) /
         w;
}

static void mando_pluck(KsString* k, double hz, double sample_rate) {
  double w = 2 * M_PI * hz / sample_rate;
  // The lowpass's own delay at this pitch, and how much it lets through.
  double r = 1 - MANDO_DAMP;
  double lp_delay = atan2(r * sin(w), 1 - r * cos(w)) / w;
  double lp_gain = MANDO_DAMP / sqrt(1 - 2 * r * cos(w) + r * r);
  double delay = sample_rate / hz - lp_delay;
  int len = (int)floor(delay);
  double frac = delay - len;
  // Keep the allpass's share between 0.1 and 1.1 samples, where it's tame.
  if (frac < 0.1) {
    len--;
    frac++;
  }
  if (len < 2) len = 2;
  if (len > MANDO_MAX_DELAY) len = MANDO_MAX_DELAY;
  // (1 - d) / (1 + d) delays by d only at the bottom of the range: nudge it
  // until it's right at this pitch.
  double want = frac;
  for (int i = 0; i < 4; i++) {
    frac += want - allpass_delay((1 - frac) / (1 + frac), w);
  }
  bool retune = len != k->len;
  k->len = len;
  k->pos = 0;
  k->ap = (1 - frac) / (1 + frac);
  k->keep = fmin(0.999, pow(10, -3 * 1000 / (hz * MANDO_RING_MS)) / lp_gain);
  if (retune) k->lp = k->ap_in = k->ap_out = 0;
  // The pick: bright noise, on top of what's left of the last note.
  // Without its average, which would otherwise sit in the line as an
  // offset the loop hardly loses.
  double amp = 0.85 + 0.15 * (breath_noise() + 1) / 2;
  static double burst[MANDO_MAX_DELAY];
  double pick = 0, mean = 0;
  for (int i = 0; i < len; i++) {
    pick += (breath_noise() - pick) * 0.8;
    burst[i] = pick;
    mean += pick / len;
  }
  for (int i = 0; i < len; i++) {
    k->line[i] = (float)((retune ? 0 : 0.2 * k->line[i]) +
                         amp * (burst[i] - mean));
  }
}

static double mando_string_run(KsString* k) {
  if (k->len == 0) return 0;
  double out = k->line[k->pos];
  k->lp += (out - k->lp) * MANDO_DAMP;
  double ap = k->ap * k->lp + k->ap_in - k->ap * k->ap_out;
  k->ap_in = k->lp;
  k->ap_out = ap;
  k->line[k->pos] = (float)(k->keep * ap);
  if (++k->pos >= k->len) k->pos = 0;
  return out;
}

static void play_mandolin(float* left, float* right, int len, bool playing,
                          double blown, double sample_rate) {
  double k = 1 - exp(-1 / (sample_rate * BREATH_PLAY_SMOOTH_MS / 1000));
  unsigned chord = atomic_load_explicit(&audio_chord, memory_order_relaxed);
  // Up from the drones' root to the mandolin's range.
  int root = (int)(chord >> 16);
  while (root < MANDO_LOWEST) root += 12;
  int tones[3] = {0, (int)(chord >> 8 & 0xff), (int)(chord & 0xff)};
  double spread = pow(2, MANDO_DETUNE_CENTS / 1200.0);
  bandpass_set(&mando.presence, 2500, 0.9, sample_rate);
  for (int i = 0; i < len; i++) {
    breath_player_move(&mando_player, playing ? blown : 0, k);
    // Course n sits at (n + 0.5) / MANDO_COURSES of the breath's range.
    int zone = (int)floor(mando_player.stick * MANDO_COURSES + 0.5);
    if (zone < 0) zone = 0;
    if (zone > MANDO_COURSES) zone = MANDO_COURSES;
    if (!mando.started) {
      mando.zone = zone;
      mando.started = true;
    }
    while (playing && zone != mando.zone) {
      // Up through the next course, or down through the one below.
      int n = zone > mando.zone ? mando.zone++ : --mando.zone;
      double hz = midi_hz(root + tones[n % 3] + 12 * (n / 3));
      mando_pluck(&mando.strings[n][0], hz * spread, sample_rate);
      mando_pluck(&mando.strings[n][1], hz / spread, sample_rate);
    }
    double y = 0;
    for (int n = 0; n < MANDO_COURSES; n++) {
      y += mando_string_run(&mando.strings[n][0]) +
           mando_string_run(&mando.strings[n][1]);
    }
    // The pickup's presence, and the amp pushed a little.
    y += 0.6 * bandpass_run(&mando.presence, y);
    y = tanh(MANDO_DRIVE * y) / MANDO_DRIVE;
    left[i] += (float)(y * MANDO_LEVEL);
    right[i] += (float)(y * MANDO_LEVEL);
  }
}

// The cuica: a sawtooth, tamed at its corners (polyBLEP) so it doesn't
// alias, with a little friction noise, through two resonances that follow
// its pitch.
typedef struct {
  double speed, amp, phase;
  Bandpass body[2];
} Cuica;

static BreathPlayer cuica_player;
static Cuica cuica;

static double polyblep(double t, double dt) {
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

static void play_cuica(float* left, float* right, int len, bool playing,
                       double blown, double sample_rate) {
  double k = 1 - exp(-1 / (sample_rate * BREATH_PLAY_SMOOTH_MS / 1000));
  double up = 1 - exp(-1 / (sample_rate * 0.004));
  double down = 1 - exp(-1 / (sample_rate * 0.030));
  for (int i = 0; i < len; i++) {
    double moved = breath_player_move(&cuica_player, playing ? blown : 0, k);
    double speed = fabs(breath_speed(&cuica.speed, moved, sample_rate));
    double target = playing ? pow(fmin(1, speed / CUICA_FULL_SPEED), 0.7) : 0;
    cuica.amp += (target - cuica.amp) * (target > cuica.amp ? up : down);
    // Where it is in the breath is the pitch, with the wobble of a stick
    // that catches and slips.
    double hz = CUICA_LOW_HZ *
      pow((double)CUICA_HIGH_HZ / CUICA_LOW_HZ, cuica_player.stick) *
      (1 + 0.004 * breath_noise());
    double dt = hz / sample_rate;
    cuica.phase += dt;
    if (cuica.phase >= 1) cuica.phase -= 1;
    double saw = 2 * cuica.phase - 1 - polyblep(cuica.phase, dt);
    double x = 0.8 * saw + 0.15 * breath_noise();
    bandpass_set(&cuica.body[0], hz, 3, sample_rate);
    bandpass_set(&cuica.body[1], hz * 3, 5, sample_rate);
    double y = cuica.amp * (0.9 * bandpass_run(&cuica.body[0], x) +
                            0.4 * bandpass_run(&cuica.body[1], x));
    left[i] += (float)(y * CUICA_LEVEL);
    right[i] += (float)(y * CUICA_LEVEL);
  }
}

// The talking drum: a membrane's first four modes, sine partials whose pitch
// is wherever the breath is while they ring, and a tick of noise for the
// stick.
#define TALK_MODES 4
static const double TALK_RATIO[TALK_MODES] = {1, 1.59, 2.14, 2.30};
static const double TALK_RING_MS[TALK_MODES] = {350, 180, 120, 90};
static const double TALK_MIX[TALK_MODES] = {1, 0.5, 0.35, 0.25};

typedef struct {
  double speed;
  int stroke;        // which way it's going: 1, -1, or 0 at rest
  int listen;        // frames left to judge a new stroke by, or 0
  double stroke_peak;
  double phase[TALK_MODES], amp[TALK_MODES];
  // A strike rises to this over TALK_RISE_MS rather than jumping there,
  // which is a step in the waveform wherever it happens to be -- a click,
  // most of all when struck again while it's still ringing.  0 once there.
  double rise_to[TALK_MODES];
  double tick, tick_rise_to;
  Bandpass stick;
} TalkingDrum;

static BreathPlayer talk_player;
static TalkingDrum talk;

static void play_talking_drum(float* left, float* right, int len,
                              bool playing, double blown,
                              double sample_rate) {
  double k = 1 - exp(-1 / (sample_rate * BREATH_PLAY_SMOOTH_MS / 1000));
  double ring[TALK_MODES];
  for (int m = 0; m < TALK_MODES; m++) {
    ring[m] = exp(-1 / (sample_rate * TALK_RING_MS[m] / 1000));
  }
  double tick_decay = exp(-1 / (sample_rate * 0.004));
  double rise = 1000 / (sample_rate * TALK_RISE_MS);
  bandpass_set(&talk.stick, 500, 1.5, sample_rate);
  for (int i = 0; i < len; i++) {
    double moved = breath_player_move(&talk_player, playing ? blown : 0, k);
    double speed = breath_speed(&talk.speed, moved, sample_rate);
    int way = speed > TALK_MOVING ? 1 : speed < -TALK_MOVING ? -1 : 0;
    if (playing && way != 0 && way != talk.stroke) {
      // A stroke starts, from rest or turning round: listen to how quick
      // it is for a few milliseconds, then strike.
      talk.listen = (int)(sample_rate * TALK_LISTEN_MS / 1000);
      talk.stroke_peak = 0;
    }
    talk.stroke = way;
    if (talk.listen > 0) {
      talk.stroke_peak = fmax(talk.stroke_peak, fabs(speed));
      if (--talk.listen == 0) {
        double hit = 0.35 + 0.65 * fmin(1, talk.stroke_peak / TALK_FULL_SPEED);
        for (int m = 0; m < TALK_MODES; m++) {
          talk.rise_to[m] = fmax(talk.amp[m], hit * TALK_MIX[m]);
        }
        talk.tick_rise_to = fmax(talk.tick, hit);
      }
    }
    double hz = TALK_LOW_HZ *
      pow((double)TALK_HIGH_HZ / TALK_LOW_HZ, talk_player.stick);
    double y = 0;
    for (int m = 0; m < TALK_MODES; m++) {
      talk.phase[m] += hz * TALK_RATIO[m] / sample_rate;
      if (talk.phase[m] >= 1) talk.phase[m] -= 1;
      y += talk.amp[m] * sin(2 * M_PI * talk.phase[m]);
      if (talk.rise_to[m] > 0) {
        talk.amp[m] += talk.rise_to[m] * rise;
        if (talk.amp[m] >= talk.rise_to[m]) {
          talk.amp[m] = talk.rise_to[m];
          talk.rise_to[m] = 0;
        }
      } else {
        talk.amp[m] *= ring[m];
      }
    }
    y += 0.25 * bandpass_run(&talk.stick, breath_noise() * talk.tick);
    if (talk.tick_rise_to > 0) {
      talk.tick += talk.tick_rise_to * rise;
      if (talk.tick >= talk.tick_rise_to) {
        talk.tick = talk.tick_rise_to;
        talk.tick_rise_to = 0;
      }
    } else {
      talk.tick *= tick_decay;
    }
    left[i] += (float)(y * TALK_LEVEL);
    right[i] += (float)(y * TALK_LEVEL);
  }
}

static void play_breath_instruments(float* left, float* right, int len,
                                    double sample_rate) {
  unsigned fx = atomic_load_explicit(&audio_breath_fx, memory_order_relaxed);
  double blown = breath_blown(
    atomic_load_explicit(&audio_breath, memory_order_relaxed));

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
  on = fx & BREATH_FX_MANDOLIN;
  if (breath_player_run(&mando_player, on, &mando, sizeof(mando),
                        sample_rate, len)) {
    play_mandolin(left, right, len, on, blown, sample_rate);
  }
  on = fx & BREATH_FX_CUICA;
  if (breath_player_run(&cuica_player, on, &cuica, sizeof(cuica),
                        sample_rate, len)) {
    play_cuica(left, right, len, on, blown, sample_rate);
  }
  on = fx & BREATH_FX_TALKING_DRUM;
  if (breath_player_run(&talk_player, on, &talk, sizeof(talk), sample_rate,
                        len)) {
    play_talking_drum(left, right, len, on, blown, sample_rate);
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
  int result = render_kick_ducked((fluid_synth_t*)data, len, nfx, fx, nout,
                                  out, synth_sample_rate);
  if (nout >= 2) {
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
  fluid_settings_setint(fl_settings, "synth.midi-channels", SYNTH_CHANNELS);
  fluid_settings_setint(fl_settings, "synth.reverb.active", 0);
  fluid_settings_setint(fl_settings, "synth.chorus.active", 0);
  // Channel 9 is percussion, as on the Pi (ENDPOINT_DRUM == CHANNEL_DRUM == 9).
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");

  fl_synth = new_fluid_synth(fl_settings);
  if (!fl_synth) die("couldn't create fluidsynth synth");
  fluid_settings_setint(fl_settings, "synth.audio-channels", 1);
  fluid_synth_set_channel_type(fl_synth, CHANNEL_KICK, CHANNEL_TYPE_DRUM);

  fl_sfont_id = fluid_synth_sfload(fl_synth, soundfont_path, 1);
  if (fl_sfont_id == FLUID_FAILED) {
    printf("failed to load soundfont %s\n", soundfont_path);
    die("couldn't load soundfont");
  }
  printf("loaded soundfont %s\n", soundfont_path);

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

void send_midi(int action, int note, int velocity, int endpoint) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;

  if (!fl_synth) return;  // no synth yet, or we're shutting down

  int channel = endpoint;

  if (action == MIDI_CC) {
    fluid_synth_cc(fl_synth, channel, note, velocity);
    // The kick's channel is the drum's, split off: same volume, pan, fade.
    if (channel == CHANNEL_DRUM) {
      fluid_synth_cc(fl_synth, CHANNEL_KICK, note, velocity);
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
}

#endif
