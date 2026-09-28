#ifndef JML_MANDOLIN_H
#define JML_MANDOLIN_H

#include <pthread.h>

// The mandolin: whatever comes into the audio device's second input, played
// out of the right channel, the alternate one, which goes to the talkbox the
// mandolin shares.  Everything else only reaches the right when CH puts it
// there; the mandolin always does.  On from the start, since that's where the
// mandolin now goes: left option (M) switches it off and on, and shift-left
// option selects it, the way `1` and shift-`1` do the whistle.
//
// Like the whistle it's an instrument on the keyboard but not an endpoint --
// fluidsynth has nothing to do with it -- so its selection lives here, and
// while it's selected its voices are on the keys:
//
//   Z  Tuner       the mandolin muted, and a tuning guide on screen
//   X  Boost       the mandolin about 6dB louder, and everything on it
//   J  Vocoder     the whistle's vocoder, played by the mandolin
//   A  Bass        the whistle's Bass voice, following the mandolin down to
//                  190Hz, just under its G, two octaves under it
//   S  Synth       a synth pedal: jammer's chord, in saws through a filter,
//                  played by the mandolin -- each strum is a stab, as loud
//                  and as open as it's played -- but only by what's
//                  pitched, so chops and scratches leave it silent
//   D  Drone       sympathetic strings: three octaves of them, one on every
//                  note, but only the chord's root and fifth let ring -- no
//                  third -- so what
//                  the mandolin plays in the chord rings on for seconds
//                  after, and the rest die away; a chord change damps the
//                  old chord's and lets the new one's ring
//   F  Shimmer     a reverb that climbs an octave as it rings, fed only the
//                  pitched part of what's played, so scratches stay dry
//   C  Breath FX   not a sound but a setting: the breath controller brings
//                  the effects in, from none at all at rest -- the plain
//                  mandolin, dry -- to 300% at full: the voices, and the
//                  Resonator crossfaded in
//                  over the first third of the breath and louder after.
//                  Drive it pushes instead: a light edge at rest, a fuzz
//                  at full.
//                  The tremolos it deepens, from none to all of it over
//                  that first third.  The Bass it switches instead: no
//                  breath, no bass, and any breath, all of it.  And the
//                  Leslie it spins: see below
//
// And on the bottom row, and H, effects on the mandolin itself rather
// than voices beside it, in a chain in the order a pedalboard would have
// them:
//
//   B  Talkbox     the mandolin through a mouth the breath opens: at rest a
//                  closed "oo", opening to "ah" and then "ae" at full, so a
//                  breath is a "wah" and a longer one a "wow-yeah".  Breath
//                  FX leaves it be, since the breath is already playing it
//   V  Drive       an overdrive, a Tube Screamer's shape: the mids pushed
//                  into a soft, uneven clip, and the top taken off, 6dB up
//   H  Resonator   the mandolin through a string tuned to each note of the
//                  chord, in the octave under its G, ringing for about a
//                  second, and not much of it straight: a metallic halo
//                  that follows the changes
//   ,  Harm Trem   a harmonic tremolo, a brownface Fender's: the lows and
//                  the highs swelling in turn, once a beat, split at the
//                  chord's root
//   .  Tremolo     a tremolo in time: twice a beat, three times in jig
//                  time, loudest on each 8th, the pedals' beat followed
//   N  Leslie      a Leslie at fast: the horn and the drum each spinning,
//                  pitch and level both, at their own speeds.  With Breath
//                  FX, the breath is its motor instead: at rest it turns
//                  slow, and blowing pushes it towards fast, the harder the
//                  faster, the horn getting there in a second or so and the
//                  heavier drum over several, and each coasting back down
//
// On the keys the whistle has them on, where it has them.  Each is on and off
// by itself, any of them at once, like the whistle's vocal effects.  The
// mandolin itself plays under all of them but the Tuner, which mutes the lot.
//
// The chain's order: Talkbox, Drive, Resonator, Harm Trem, Tremolo, Leslie.
//
// The voices -- the Vocoder, the Bass, Synth, Drone and Shimmer --
// go to the left, where fluidsynth's endpoints are, since the mandolin
// itself is still heard under them on the right.  The chain is the mandolin
// itself, so it's on the right with it.
//
// The Bass and the Vocoder are instances of their own, apart from the
// whistle's, so the two can run at once on their two inputs.  They
// have a gate of their own, apart from the whistle's effects', in the Vocal
// FX menu, for the two that make something out of nothing -- the Bass, whose
// pitch tracker plays the room as notes, and the Vocoder, which brings a
// quiet input up -- so nothing quieter plays them.  The rest pass what
// comes in, and quiet in is quiet out.
//
// Its output is summed in after the right channel's level (alt_channel_gain),
// since that's there to match everything else to the mandolin.
//
// Include from whistle.h, after voicefx.h.

enum {
  MANDO_TUNER,
  MANDO_BOOST,
  MANDO_VOCODER,
  MANDO_BASS,
  MANDO_DRONE,
  MANDO_SHIMMER,
  MANDO_SYNTH,
  MANDO_BREATH,  // not a sound: see the top
  // The rest are the mandolin itself, through them, rather than voices.
  MANDO_TALKBOX,
  MANDO_DRIVE,
  MANDO_RESONATOR,
  MANDO_HARM_TREM,
  MANDO_TREMOLO,
  MANDO_LESLIE,
  N_MANDO_VOICES,
};

typedef struct {
  int note;           // the key's pseudo-note, as KEYS[] sends it
  const char* label;  // what to draw on the key; "\n" splits lines
  const char* name;   // what the status row calls it
} MandoVoice;

static const MandoVoice MANDO_VOICES[N_MANDO_VOICES] = {
  [MANDO_TUNER] = {'Z', "Tuner", "tuner"},
  [MANDO_BOOST] = {'X', "Boost", "boost"},
  [MANDO_VOCODER] = {'J', "Vocoder", "voc"},
  [MANDO_BASS] = {'A', "Bass", "bass"},
  [MANDO_DRONE] = {'D', "Drone", "drone"},
  [MANDO_SHIMMER] = {'F', "Shimmer", "shimmer"},
  [MANDO_SYNTH] = {'S', "Synth", "synth"},
  [MANDO_BREATH] = {'C', "Breath\nFX", "breath"},
  [MANDO_TALKBOX] = {'B', "Talk\nbox", "talkbox"},
  [MANDO_DRIVE] = {'V', "Drive", "drive"},
  [MANDO_RESONATOR] = {'H', "Reso\nnator", "resonator"},
  [MANDO_HARM_TREM] = {',', "Harm\nTrem", "harmtrem"},
  [MANDO_TREMOLO] = {'.', "Tremolo", "tremolo"},
  [MANDO_LESLIE] = {'N', "Leslie", "leslie"},
};

// Breath FX: the effects' level at full breath, from none at rest, and how
// smoothly it follows the breath.  The breath controller comes in whole steps,
// and at rest it flickers between two: that's the effects on and off, and
// on a sustained sound -- Shimmer's tail -- it crackles.  Followed over this
// long, sample by sample, the steps and the flicker are a glide.
#define MANDO_BREATH_HIGH 3.0
#define MANDO_BREATH_SMOOTH_S 0.040

#define MANDO_BIT(v) (1u << (v))
// And one more bit, in what's published: whether it's on at all.
#define MANDO_PUB_ON (1u << N_MANDO_VOICES)

// About 6dB.
#define MANDO_BOOST_GAIN 2.0f

// Its level, all of it, voices and all, from the Audio Output menu: from
// silent to double, remembered.  Neither the global volume, which is
// fluidsynth's, nor the alternate channel volume moves it.
//
// The slider's middle, 1, is MANDO_GAIN_UNIT: where it was first set by ear
// against the rig, about 5dB over the input as it comes in.
#define MAX_MANDO_GAIN 2.0
#define MANDO_GAIN_UNIT 1.83f
static double mando_volume = 1.0;  // the slider's
static _Atomic float mando_gain = MANDO_GAIN_UNIT;

static void set_mando_gain(double volume) {
  if (volume < 0) volume = 0;
  if (volume > MAX_MANDO_GAIN) volume = MAX_MANDO_GAIN;
  mando_volume = volume;
  atomic_store_explicit(&mando_gain, (float)volume * MANDO_GAIN_UNIT,
                        memory_order_relaxed);
}

// The whistle's Bass, moved to the mandolin: the lowest note that counts is
// just under the G string, the highest well over what the E string reaches,
// and the preset's four octaves down brought up two, so a mandolin's low G
// comes out as a bass's G1.
#define MANDO_BASS_LOW_HZ 190
#define MANDO_BASS_HIGH_HZ 1600
#define MANDO_BASS_OCTAVE 2
#define MANDO_BASS_LEVEL_FULL 2
// And the mandolin into it 15dB hotter than it comes in: a pickup runs
// quieter than a whistle into a microphone, and the Bass's dynamics are set
// against the whistle's.  After the gate, which goes by the input as it is.
#define MANDO_BASS_INPUT_GAIN 5.623f
// And into the Vocoder 9dB hotter.  Its gate is inside it, so that's moved
// up by as much, to go by the input as it is too.
#define MANDO_VOCODER_INPUT_GAIN 2.818f

// The mandolin's voice on a key, or -1.
static int mando_voice_for_note(int note) {
  for (int v = 0; v < N_MANDO_VOICES; v++) {
    if (MANDO_VOICES[v].note == note) return v;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// State the UI owns, under jammer_lock, published to the audio thread
// ---------------------------------------------------------------------------

static bool mando_on = true;
static bool mando_selected;
static unsigned mando_voices;  // MANDO_BIT each

// Its voices' gate, in dBFS peak, like the whistle effects' (RoomGate), and
// with the same range.  Remembered, like that one.
static int mando_gate_db = VFX_GATE_DEFAULT_DB;
static _Atomic int mando_pub_gate_db = VFX_GATE_DEFAULT_DB;

static _Atomic unsigned mando_pub = MANDO_PUB_ON;

// What the tuner hears, in Hz x100, or 0 when it isn't hearing a note.
static _Atomic int mando_meter_hz;
// The loudest the input has been since the UI last looked, x10000, for
// setting the gate against.
static _Atomic int mando_meter_level;

static void mando_publish(void) {
  atomic_store_explicit(&mando_pub,
                        mando_voices | (mando_on ? MANDO_PUB_ON : 0),
                        memory_order_relaxed);
  atomic_store_explicit(&mando_pub_gate_db, mando_gate_db,
                        memory_order_relaxed);
  if (!(mando_voices & MANDO_BIT(MANDO_TUNER))) {
    atomic_store_explicit(&mando_meter_hz, 0, memory_order_relaxed);
  }
}

// ---------------------------------------------------------------------------
// Listening to it: the tuner, and whether what's played is pitched
//
// Both by YIN, as the whistle's Voice Bass finds a pitch, on the input taken
// down in rate into a line.  The tuner reads the note, at about 24kHz and
// over a longer window, so it's right to a cent or so; Shimmer only asks
// whether there's a pitch at all, at 12kHz, quickly.
// ---------------------------------------------------------------------------

#define MANDO_LINE 2048
#define MANDO_YIN_MAX 512   // the longest lag either looks at, decimated

typedef struct {
  double rate;  // after decimating
  int decimate, dec_n, hop_n;
  double dec_sum;
  float line[MANDO_LINE];
  int head, filled;
} MandoLine;

static void mando_line_prepare(MandoLine* l, double rate, double target) {
  memset(l, 0, sizeof(*l));
  l->decimate = (int)lround(rate / target);
  if (l->decimate < 1) l->decimate = 1;
  l->rate = rate / l->decimate;
}

// One sample in; true once every `hop_s`, when it's time to look.
static bool mando_line_push(MandoLine* l, float x, double hop_s) {
  l->dec_sum += x;
  if (++l->dec_n < l->decimate) return false;
  l->line[l->head] = (float)(l->dec_sum / l->dec_n);
  l->head = (l->head + 1) % MANDO_LINE;
  if (l->filled < MANDO_LINE) l->filled++;
  l->dec_sum = 0;
  l->dec_n = 0;
  if (++l->hop_n < (int)(l->rate * hop_s)) return false;
  l->hop_n = 0;
  return true;
}

#define MANDO_AT(l, i) \
  (l)->line[((l)->head - 1 - (i) + 2 * MANDO_LINE) % MANDO_LINE]

// YIN's normalized difference over the last `window` samples, at each lag
// up to `hi` + 1, into d.  False if there isn't enough yet, or it's quieter
// than `gate`, RMS.
static bool mando_yin(const MandoLine* l, int window, int hi, double gate,
                      double* d) {
  if (hi + 1 >= MANDO_YIN_MAX || l->filled < window + hi + 2) return false;
  double power = 0;
  for (int i = 0; i < window; i++) power += MANDO_AT(l, i) * MANDO_AT(l, i);
  if (sqrt(power / window) < gate) return false;
  double running = 0;
  d[0] = 1;
  for (int tau = 1; tau <= hi + 1; tau++) {
    double sum = 0;
    for (int i = 0; i < window; i++) {
      double diff = MANDO_AT(l, i) - MANDO_AT(l, i + tau);
      sum += diff * diff;
    }
    running += sum;
    d[tau] = running > 0 ? sum * tau / running : 1;
  }
  return true;
}

#define TUNER_RATE 24000
#define TUNER_WINDOW_S 0.040
#define TUNER_HOP_S 0.020
#define TUNER_LOW_HZ 150
#define TUNER_HIGH_HZ 1500
#define TUNER_THRESHOLD 0.15
#define TUNER_GATE 0.002   // RMS: quieter than this is no note

// The pitch over the last TUNER_WINDOW_S, or 0.
static double tuner_pitch(const MandoLine* t) {
  int window = (int)(t->rate * TUNER_WINDOW_S);
  int lo = (int)(t->rate / TUNER_HIGH_HZ), hi = (int)(t->rate / TUNER_LOW_HZ);
  double d[MANDO_YIN_MAX];
  if (!mando_yin(t, window, hi, TUNER_GATE, d)) return 0;
  int found = 0;
  for (int tau = lo > 2 ? lo : 2; tau <= hi && !found; tau++) {
    if (d[tau] < TUNER_THRESHOLD && d[tau] <= d[tau + 1] &&
        d[tau] <= d[tau - 1]) {
      found = tau;
    }
  }
  if (!found) return 0;
  double a = d[found - 1], b = d[found], c = d[found + 1];
  double denom = a - 2 * b + c;
  double tau = found + (denom != 0 ? 0.5 * (a - c) / denom : 0);
  double hz = tau > 0 ? t->rate / tau : 0;
  return hz >= TUNER_LOW_HZ && hz <= TUNER_HIGH_HZ ? hz : 0;
}

// One sample in; every hop, what it heard to the UI.
static void tuner_run(MandoLine* t, float x) {
  if (!mando_line_push(t, x, TUNER_HOP_S)) return;
  atomic_store_explicit(&mando_meter_hz, (int)(tuner_pitch(t) * 100),
                        memory_order_relaxed);
}

// Pitched or a scratch: how periodic the last 20ms has been, anywhere from
// 50Hz up to 1kHz -- a chord's shared fundamental is low -- as YIN's lowest
// dip.  A chord rings on and repeats itself; a muted chop is noise.
#define TONAL_RATE 12000
#define TONAL_WINDOW_S 0.020
#define TONAL_HOP_S 0.005
#define TONAL_LOW_HZ 50
#define TONAL_HIGH_HZ 1000
#define TONAL_DIP 0.25   // under this is pitched
#define TONAL_NONE 0.55  // over this is noise

// How pitched, 0-1, or -1 between looks.
static double tonal_run(MandoLine* l, float x) {
  if (!mando_line_push(l, x, TONAL_HOP_S)) return -1;
  int window = (int)(l->rate * TONAL_WINDOW_S);
  int lo = (int)(l->rate / TONAL_HIGH_HZ), hi = (int)(l->rate / TONAL_LOW_HZ);
  double d[MANDO_YIN_MAX];
  if (!mando_yin(l, window, hi, 0, d)) return 0;
  double dip = 1;
  for (int tau = lo; tau <= hi; tau++) dip = fmin(dip, d[tau]);
  return fmin(1, fmax(0, (TONAL_NONE - dip) / (TONAL_NONE - TONAL_DIP)));
}

// The mandolin's strings, for the tuning guide: G3 D4 A4 E5.
static const int MANDO_STRINGS[4] = {55, 62, 69, 76};

// ---------------------------------------------------------------------------
// The effects
// ---------------------------------------------------------------------------

// A biquad, from the Audio EQ Cookbook, for the fixed filters below.
typedef struct {
  double b0, b1, b2, a1, a2;
  double x1, x2, y1, y2;
} Biquad;

enum { BQ_LOWPASS, BQ_HIGHPASS, BQ_PEAK, BQ_LOWSHELF, BQ_HIGHSHELF };

static void biquad_set(Biquad* f, int kind, double hz, double q, double db,
                       double rate) {
  double w = 2 * M_PI * fmin(hz, 0.45 * rate) / rate;
  double cw = cos(w), alpha = sin(w) / (2 * q), a = pow(10, db / 40);
  double b0, b1, b2, a0, a1, a2;
  switch (kind) {
  case BQ_LOWPASS:
    b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0;
    a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    break;
  case BQ_HIGHPASS:
    b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0;
    a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    break;
  case BQ_PEAK:
    b0 = 1 + alpha * a; b1 = -2 * cw; b2 = 1 - alpha * a;
    a0 = 1 + alpha / a; a1 = -2 * cw; a2 = 1 - alpha / a;
    break;
  default: {  // the shelves
    double sq = 2 * sqrt(a) * alpha, s = kind == BQ_LOWSHELF ? -1 : 1;
    b0 = a * ((a + 1) + s * (a - 1) * cw + sq);
    b1 = -2 * s * a * ((a - 1) + s * (a + 1) * cw);
    b2 = a * ((a + 1) + s * (a - 1) * cw - sq);
    a0 = (a + 1) - s * (a - 1) * cw + sq;
    a1 = 2 * s * ((a - 1) - s * (a + 1) * cw);
    a2 = (a + 1) - s * (a - 1) * cw - sq;
    break;
  }
  }
  f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
  f->a1 = a1 / a0; f->a2 = a2 / a0;
  f->x1 = f->x2 = f->y1 = f->y2 = 0;
}

static double biquad_run(Biquad* f, double x) {
  double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 -
             f->a1 * f->y1 - f->a2 * f->y2;
  f->x2 = f->x1;
  f->x1 = x;
  f->y2 = f->y1;
  f->y1 = y;
  return y;
}

// A state-variable filter's bandpass, 0dB and in phase at `hz`, which can
// move sample by sample: for the Talkbox's formants.
typedef struct {
  double ic1, ic2;
} MandoBand;

static double mando_band(MandoBand* b, double x, double hz, double q,
                         double rate) {
  double g = tan(M_PI * fmin(hz, 0.45 * rate) / rate), k = 1 / q;
  double a1 = 1 / (1 + g * (g + k)), a2 = g * a1;
  double v1 = a1 * b->ic1 + a2 * (x - b->ic2);
  double v2 = b->ic2 + g * v1;
  b->ic1 = 2 * v1 - b->ic1;
  b->ic2 = 2 * v2 - b->ic2;
  return k * v1;
}

// Talkbox: three formants, in parallel bandpasses, moving from one vowel to
// the next as the breath comes -- "oo" at rest, "ah" at half, "ae" at full --
// each between two by its frequencies' logs.  The breath is followed over
// TALKBOX_SMOOTH_S, quick enough to say a "wah" with but not so quick the
// controller's steps zip.  TALKBOX_LEVEL makes up what the mouth takes
// away, and a little more, to sit about 3dB over the mandolin.
#define TALKBOX_FORMANTS 3
#define TALKBOX_VOWELS 3
#define TALKBOX_Q 5.0
#define TALKBOX_SMOOTH_S 0.025
#define TALKBOX_LEVEL 3.25

static const double TALKBOX_HZ[TALKBOX_VOWELS][TALKBOX_FORMANTS] = {
  {300, 870, 2240},   // oo
  {730, 1090, 2440},  // ah
  {660, 1720, 2410},  // ae
};
static const double TALKBOX_GAIN[TALKBOX_FORMANTS] = {1, 0.7, 0.35};

typedef struct {
  MandoBand formant[TALKBOX_FORMANTS];
  double open, smooth;  // the breath, followed, 0 to 1
} Talkbox;

static void talkbox_prepare(Talkbox* t, double rate) {
  memset(t, 0, sizeof(*t));
  t->smooth = 1 - exp(-1 / (rate * TALKBOX_SMOOTH_S));
}

static double talkbox_run(Talkbox* t, double x, double blown, double rate) {
  t->open += (blown - t->open) * t->smooth;
  double at = t->open * (TALKBOX_VOWELS - 1);
  int from = (int)fmin(at, TALKBOX_VOWELS - 2);
  double frac = at - from, y = 0;
  for (int f = 0; f < TALKBOX_FORMANTS; f++) {
    double hz = TALKBOX_HZ[from][f] *
      pow(TALKBOX_HZ[from + 1][f] / TALKBOX_HZ[from][f], frac);
    y += TALKBOX_GAIN[f] *
      mando_band(&t->formant[f], x, hz, TALKBOX_Q, rate);
  }
  return TALKBOX_LEVEL * y;
}

// Drive: a Tube Screamer's shape.  The lows kept out of the clipping, so it
// stays tight rather than farting out on the chop, the mids pushed hard into
// a soft clip that isn't quite even -- a little second harmonic, as a diode
// pair that doesn't match gives -- and a lowpass after, for the fizz.
// DRIVE_GAIN is for an electric mandolin into an interface's instrument
// input, which peaks around -20dB: hard strums clip well into it, soft ones
// only just.
#define DRIVE_HIGHPASS_HZ 450
#define DRIVE_GAIN 60.0
// With Breath FX the breath sets how hard it's pushed instead: from a light
// edge at rest to a fuzz at full, the gain swept between these by its log.
// Its level made up to about where it is at DRIVE_GAIN all the way: under
// it, less clipped is a lot quieter, so brought up, by more the gentler it
// is; over it, clipping hard is only a little louder, so brought down a
// little, about 0.8dB an octave of gain.
#define DRIVE_GAIN_REST 3.0
#define DRIVE_GAIN_FULL 400.0
#define DRIVE_MAKEUP_GAIN 15.5  // where the making up is 3dB
#define DRIVE_EVEN 0.128        // the level's fall against the gain's rise
#define DRIVE_BIAS 0.15
#define DRIVE_TONE_HZ 3200
#define DRIVE_LEVEL 0.12  // 6dB over the mandolin as it comes in

typedef struct {
  Biquad in_hp, tone, tone2;
} Drive;

static void drive_prepare(Drive* d, double rate) {
  biquad_set(&d->in_hp, BQ_HIGHPASS, DRIVE_HIGHPASS_HZ, 0.5, 0, rate);
  biquad_set(&d->tone, BQ_LOWPASS, DRIVE_TONE_HZ, 0.7, 0, rate);
  biquad_set(&d->tone2, BQ_LOWPASS, DRIVE_TONE_HZ * 1.6, 0.7, 0, rate);
}

// How hard to push it, for Breath FX's `push`, 0 to 1; and how loud, to keep
// it about where it is at DRIVE_GAIN.
static double drive_gain(double push) {
  return DRIVE_GAIN_REST * pow(DRIVE_GAIN_FULL / DRIVE_GAIN_REST, push);
}

static double drive_level(double gain) {
  double at = DRIVE_MAKEUP_GAIN / gain, unit = DRIVE_MAKEUP_GAIN / DRIVE_GAIN;
  return DRIVE_LEVEL * sqrt((1 + at * at) / (1 + unit * unit)) *
         pow(DRIVE_GAIN / gain, DRIVE_EVEN);
}

static double drive_run(Drive* d, double x, double gain) {
  // What the Tube Screamer does: the clipped mids on top of the clean signal.
  double mids = biquad_run(&d->in_hp, x) * gain;
  double clipped = tanh(mids + DRIVE_BIAS) - tanh(DRIVE_BIAS);
  double y = x + clipped;
  return drive_level(gain) * biquad_run(&d->tone2, biquad_run(&d->tone, y));
}

// Leslie, fast: the treble and the bass split at 800Hz, as the cabinet
// does, the horn spinning at about 6.8 times a second and the drum a little
// slower.  Each swings in pitch, as it comes towards you and goes away --
// the horn's by a fraction of a millisecond of delay, the drum's less -- and
// in level, the horn much more than the drum.
//
// With Breath FX the breath drives its motor: at rest it turns at chorale,
// slow, and the breath pushes it towards tremolo, fast, as far as it's
// blown.  The rotors take their time about it, as a cabinet's do: the horn,
// light, spins up in about a second and down in a little more, and the drum,
// heavier, takes four or five seconds up and longer still to coast down.
// Each follows where it's being pushed over these, as time constants.
#define LESLIE_CROSSOVER_HZ 800
#define LESLIE_HORN_HZ 6.8
#define LESLIE_DRUM_HZ 5.9
#define LESLIE_HORN_SLOW_HZ 0.8
#define LESLIE_DRUM_SLOW_HZ 0.67
#define LESLIE_HORN_UP_S 0.35
#define LESLIE_HORN_DOWN_S 0.5
#define LESLIE_DRUM_UP_S 1.5
#define LESLIE_DRUM_DOWN_S 2.2
#define LESLIE_HORN_DELAY_MS 0.45
#define LESLIE_DRUM_DELAY_MS 0.20
#define LESLIE_HORN_AM 0.5
#define LESLIE_DRUM_AM 0.25
#define LESLIE_LINE 2048
#define LESLIE_LEVEL 0.93  // back up by what the swinging level takes off

typedef struct {
  Biquad low, low2;
  float horn[LESLIE_LINE], drum[LESLIE_LINE];
  int pos;
  double horn_phase, drum_phase;
  double horn_hz, drum_hz;  // how fast each is turning now
  double horn_up, horn_down, drum_up, drum_down;
} Leslie;

static void leslie_prepare(Leslie* l, double rate) {
  memset(l, 0, sizeof(*l));
  biquad_set(&l->low, BQ_LOWPASS, LESLIE_CROSSOVER_HZ, 0.7, 0, rate);
  biquad_set(&l->low2, BQ_LOWPASS, LESLIE_CROSSOVER_HZ, 0.7, 0, rate);
  l->drum_phase = 0.3;  // not in step with the horn
  l->horn_hz = LESLIE_HORN_HZ;
  l->drum_hz = LESLIE_DRUM_HZ;
  l->horn_up = 1 - exp(-1 / (rate * LESLIE_HORN_UP_S));
  l->horn_down = 1 - exp(-1 / (rate * LESLIE_HORN_DOWN_S));
  l->drum_up = 1 - exp(-1 / (rate * LESLIE_DRUM_UP_S));
  l->drum_down = 1 - exp(-1 / (rate * LESLIE_DRUM_DOWN_S));
}

// A rotor towards `to`, quicker spinning up than coasting down.
static void leslie_spin(double* hz, double to, double up, double down) {
  *hz += (to - *hz) * (to > *hz ? up : down);
}

// `motor` is how hard it's being pushed from slow, 0, to fast, 1.
static double leslie_run(Leslie* l, double x, double motor, double rate) {
  leslie_spin(&l->horn_hz,
              LESLIE_HORN_SLOW_HZ + motor * (LESLIE_HORN_HZ -
                                             LESLIE_HORN_SLOW_HZ),
              l->horn_up, l->horn_down);
  leslie_spin(&l->drum_hz,
              LESLIE_DRUM_SLOW_HZ + motor * (LESLIE_DRUM_HZ -
                                             LESLIE_DRUM_SLOW_HZ),
              l->drum_up, l->drum_down);
  double low = biquad_run(&l->low2, biquad_run(&l->low, x));
  double high = x - low;
  l->horn[l->pos] = (float)high;
  l->drum[l->pos] = (float)low;
  l->horn_phase += l->horn_hz / rate;
  l->drum_phase += l->drum_hz / rate;
  if (l->horn_phase >= 1) l->horn_phase -= 1;
  if (l->drum_phase >= 1) l->drum_phase -= 1;
  double hs = sin(2 * M_PI * l->horn_phase);
  double ds = sin(2 * M_PI * l->drum_phase);
  double ms = rate / 1000;
  // Nearest when the delay's shortest, and loudest a little after.
  double horn = vfx_read(l->horn, LESLIE_LINE, l->pos,
                         2 + ms * LESLIE_HORN_DELAY_MS * (1 + hs)) *
    (1 - LESLIE_HORN_AM * 0.5 * (1 + sin(2 * M_PI * l->horn_phase + 1.2)));
  double drum = vfx_read(l->drum, LESLIE_LINE, l->pos,
                         2 + ms * LESLIE_DRUM_DELAY_MS * (1 + ds)) *
    (1 - LESLIE_DRUM_AM * 0.5 * (1 + sin(2 * M_PI * l->drum_phase + 1.2)));
  l->pos = (l->pos + 1) % LESLIE_LINE;
  return LESLIE_LEVEL * (horn + drum);
}

// Synth: a synth pedal.  jammer's chord -- its root under C3 and again an
// octave up and another, and its fifth, never its third -- in pairs of saws
// a few cents apart, through a resonant lowpass.  The mandolin plays it: how loud it is, and
// how far the filter opens, both follow how hard it's played, quick to
// come and a tenth of a second or so to go, so each strum is a stab.  Only
// as much as it's pitched (tonal_run, as Shimmer's send), so a chop or a
// scratch plays nothing.  In the chord the feet or a voice have chosen, so
// it's in tune whatever the mandolin's voicing.
#define SYNTH_VOICES 4
#define SYNTH_DETUNE 1.004
#define SYNTH_LOW_HZ 200
#define SYNTH_HIGH_HZ 5000
#define SYNTH_RANGE_DB 40
#define SYNTH_Q 2.5
#define SYNTH_ATTACK_S 0.002
#define SYNTH_RELEASE_S 0.120
#define SYNTH_LEVEL 1.4

typedef struct {
  double phase[SYNTH_VOICES][2];
  VfxSvf filter;
  double level, attack, release;
} SynthPedal;

static void synth_prepare(SynthPedal* p, double rate) {
  memset(p, 0, sizeof(*p));
  p->attack = 1 - exp(-1 / (rate * SYNTH_ATTACK_S));
  p->release = 1 - exp(-1 / (rate * SYNTH_RELEASE_S));
}

// `hz` the chord's notes, from synth_chord; `gate` the mandolin gate's, since
// it makes a chord out of whatever comes in, the room included, times how
// pitched it is.
static double synth_run(SynthPedal* p, double x, const double* hz,
                        double gate, int gate_db, double rate) {
  double level = fabs(x) * gate;
  p->level += (level - p->level) *
              (level > p->level ? p->attack : p->release);
  if (p->level < 1e-7) return 0;
  double saw = 0;
  for (int v = 0; v < SYNTH_VOICES; v++) {
    for (int d = 0; d < 2; d++) {
      double f = hz[v] * (d ? SYNTH_DETUNE : 1 / SYNTH_DETUNE);
      saw += vfx_saw_wave(&p->phase[v][d], f, rate);
    }
  }
  saw /= 2 * SYNTH_VOICES;
  double open = (20 * log10(p->level) - gate_db) / SYNTH_RANGE_DB;
  open = fmin(1, fmax(0, open));
  double cutoff = SYNTH_LOW_HZ * pow((double)SYNTH_HIGH_HZ / SYNTH_LOW_HZ,
                                     open);
  double y = vfx_lowpass(&p->filter, saw, cutoff, SYNTH_Q, rate);
  // pi/2, from the rectified mandolin's mean up to its peak.
  return SYNTH_LEVEL * 1.57 * p->level * y;
}

// The chord for the Synth, from what jammermidilib.h publishes.
static void synth_chord(double* hz) {
  int root = atomic_load_explicit(&audio_chord_root, memory_order_relaxed);
  int fifth = atomic_load_explicit(&audio_chord_fifth, memory_order_relaxed);
  int base = 36 + root % 12;  // C2 to B2
  hz[0] = midi_hz(base);
  hz[1] = midi_hz(base + 12);
  // Never the third, whatever's said it: an octave up instead.
  hz[2] = midi_hz(base + 24);
  hz[3] = midi_hz(base + 12 + fifth);
}

// ---------------------------------------------------------------------------
// What knows the chord and the beat
// ---------------------------------------------------------------------------

// The chord's notes as pitch classes, 0-11: the root, the fifth, and the
// third once the feet or a voice have said which.  Returns how many.
static int mando_chord_pcs(int* pc) {
  int root = atomic_load_explicit(&audio_chord_root, memory_order_relaxed);
  int third = atomic_load_explicit(&audio_chord_third, memory_order_relaxed);
  int fifth = atomic_load_explicit(&audio_chord_fifth, memory_order_relaxed);
  int n = 0;
  pc[n++] = root % 12;
  pc[n++] = (root + fifth) % 12;
  if (third) pc[n++] = (root + third) % 12;
  return n;
}

static bool mando_in_chord(int note, const int* pc, int n) {
  for (int i = 0; i < n; i++) {
    if (note % 12 == pc[i]) return true;
  }
  return false;
}

// The beat, sample by sample, for what goes in time with it: the tremolos.  A count of its own, going at the pedals' tempo and pulled
// into step with their beat while they keep it.  The pull is spread over
// each block as going a little faster or slower -- never more than a
// quarter -- so it never jumps, and a tremolo only leans into place.  When
// the feet stop it goes on at their last tempo, or 116 BPM if there's been
// none.
#define MANDO_CLOCK_PULL 0.3   // of how far out it is, each block
#define MANDO_CLOCK_LEAN 0.25  // the most faster or slower it goes for it

typedef struct {
  double beats;      // since it started
  double beat_ns;    // the tempo it's at
  double per_frame;  // beats a frame, this block
  bool jig;
} MandoClock;

static void mando_clock_block(MandoClock* c, int len, double rate) {
  uint64_t beat = atomic_load_explicit(&audio_beat_ns, memory_order_relaxed);
  if (beat > 0) c->beat_ns = (double)beat;
  if (c->beat_ns <= 0) c->beat_ns = AUDIO_DEFAULT_BEAT_NS;
  double base = 1e9 / (c->beat_ns * rate);
  double pull = 0;
  double phase = audio_beat_phase(audio_block_ns);
  if (phase >= 0) {
    double off = phase - (c->beats - floor(c->beats));
    off -= round(off);  // the shorter way round, -0.5 to 0.5
    pull = fmax(-MANDO_CLOCK_LEAN * base,
                fmin(MANDO_CLOCK_LEAN * base, MANDO_CLOCK_PULL * off / len));
  }
  c->per_frame = base + pull;
  c->jig = atomic_load_explicit(&audio_jig, memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Strings tuned to the chord: the Drone and the Resonator
//
// A string on every note from `low` up, each a delay a period long fed back
// through a gentle lowpass -- a Karplus-Strong string, excited by the
// mandolin rather than plucked -- so it rings at its note and the note's
// harmonics.  Only the chord's are fed, and only they ring long; the rest
// are damped, so on a change the old chord's fade quickly and the new one's
// start ringing, in tune from the first moment rather than gliding there.
// Each string's feed is (1 - its feedback), so a steady note on it comes out
// at about its own level.
// ---------------------------------------------------------------------------

#define STRINGS_MAX 36
#define STRINGS_LINE 2048     // a period at C3 up to 192kHz
#define STRINGS_FEED_S 0.010  // how quickly a string's feed comes and goes

typedef struct {
  float line[STRINGS_MAX][STRINGS_LINE];
  int pos, low, n;
  double ring_s, stop_s, damp;
  double delay[STRINGS_MAX];
  double store[STRINGS_MAX];  // the lowpass in each loop
  double feedback[STRINGS_MAX];
  double feed[STRINGS_MAX], feed_to[STRINGS_MAX];
  double feed_smooth;
  // What the feedback is for a string that rings, and one that doesn't.
  double ring_feedback[STRINGS_MAX], stop_feedback[STRINGS_MAX];
} ChordStrings;

static void strings_prepare(ChordStrings* s, int low, int n, double ring_s,
                            double stop_s, double damp, double rate) {
  memset(s, 0, sizeof(*s));
  s->low = low;
  s->n = n > STRINGS_MAX ? STRINGS_MAX : n;
  s->ring_s = ring_s;
  s->stop_s = stop_s;
  s->damp = damp;
  s->feed_smooth = 1 - exp(-1 / (rate * STRINGS_FEED_S));
  for (int i = 0; i < s->n; i++) {
    double hz = midi_hz(low + i), w = 2 * M_PI * hz / rate;
    // The loop's lowpass delays it a little, and takes a little off even at
    // the fundamental: both made up, so it's in tune and rings as long as
    // it says there.
    double lag = damp / (1 - damp);
    s->delay[i] = fmin(STRINGS_LINE - 2, fmax(1, rate / hz - lag));
    double loss = (1 - damp) / sqrt(1 - 2 * damp * cos(w) + damp * damp);
    s->ring_feedback[i] = fmin(0.9999, pow(10, -3 / (ring_s * hz)) / loss);
    s->stop_feedback[i] = fmin(0.9999, pow(10, -3 / (stop_s * hz)) / loss);
    s->feedback[i] = s->stop_feedback[i];
  }
}

// Once a block: which ring, from the chord.
static void strings_chord(ChordStrings* s, const int* pc, int n) {
  for (int i = 0; i < s->n; i++) {
    bool ring = mando_in_chord(s->low + i, pc, n);
    s->feedback[i] = ring ? s->ring_feedback[i] : s->stop_feedback[i];
    s->feed_to[i] = ring ? 1 - s->ring_feedback[i] : 0;
  }
}

static double strings_run(ChordStrings* s, double x) {
  double out = 0;
  for (int i = 0; i < s->n; i++) {
    s->feed[i] += (s->feed_to[i] - s->feed[i]) * s->feed_smooth;
    double back = vfx_read(s->line[i], STRINGS_LINE, s->pos, s->delay[i]);
    s->store[i] = (1 - s->damp) * back + s->damp * s->store[i];
    double y = s->feed[i] * x + s->feedback[i] * s->store[i];
    s->line[i][s->pos] = (float)y;
    out += y;
  }
  s->pos = (s->pos + 1) % STRINGS_LINE;
  return out;
}

// Drone: three octaves of sympathetic strings, C3 to B5, the chord's root and
// fifth ringing for four seconds -- never its third -- and the rest for a
// fifth of one.
#define DRONE_LOW 48
#define DRONE_STRINGS 36
#define DRONE_RING_S 4.0
#define DRONE_STOP_S 0.2
#define DRONE_DAMP 0.3
#define DRONE_LEVEL 1.6

// Resonator: an octave of them, C3 to B3, whose harmonics cover the rest,
// ringing for most of a second, with only a little of the mandolin straight.
#define RESONATOR_LOW 48
#define RESONATOR_STRINGS 12
#define RESONATOR_RING_S 0.8
#define RESONATOR_STOP_S 0.05
#define RESONATOR_DAMP 0.15
#define RESONATOR_DRY 0.3
#define RESONATOR_WET 1.3

// ---------------------------------------------------------------------------
// The tremolos
//
// Harm Trem: a brownface Fender's harmonic tremolo, the mandolin split in
// two and each half swelling while the other ebbs, once a beat, the lows
// loudest on it.  Split at the chord's root, put between 350Hz and an octave
// up, gliding there on a change.
//
// Tremolo: the level, twice a beat, or three times in jig time, full on each
// 8th and down to a sixth between.
//
// `depth` is Breath FX's, 0 to 1, or 1 without it.
// ---------------------------------------------------------------------------

#define HTREM_DEPTH 0.85
#define HTREM_LOW_HZ 350
#define HTREM_GLIDE_S 0.050
#define HTREM_LEVEL 1.2

typedef struct {
  VfxSvf a, b;
  double hz, glide;
} HarmTrem;

static void htrem_prepare(HarmTrem* h, double rate) {
  memset(h, 0, sizeof(*h));
  h->hz = HTREM_LOW_HZ;
  h->glide = 1 - exp(-1 / (rate * HTREM_GLIDE_S));
}

// Where to split for the root, pitch class `pc`.
static double htrem_split(int pc) {
  double hz = midi_hz(60 + pc);  // C4 to B4
  while (hz < HTREM_LOW_HZ) hz *= 2;
  while (hz >= 2 * HTREM_LOW_HZ) hz /= 2;
  return hz;
}

static double htrem_run(HarmTrem* h, double x, double split, double beats,
                        double depth, double rate) {
  h->hz += (split - h->hz) * h->glide;
  double low = vfx_lowpass(&h->b, vfx_lowpass(&h->a, x, h->hz, 0.7, rate),
                           h->hz, 0.7, rate);
  double high = x - low;
  double s = cos(2 * M_PI * beats), d = HTREM_DEPTH * depth;
  return HTREM_LEVEL * (low * (1 - d * 0.5 * (1 - s)) +
                        high * (1 - d * 0.5 * (1 + s)));
}

#define TREM_DEPTH 0.85
#define TREM_LEVEL 1.45

static double trem_run(double x, const MandoClock* c, double depth) {
  double p = c->beats * (c->jig ? 3 : 2);
  return TREM_LEVEL * x *
    (1 - TREM_DEPTH * depth * 0.5 * (1 - cos(2 * M_PI * p)));
}

// Shimmer: a mono Freeverb, eight combs and four allpasses, with its own
// output shifted up an octave and fed back in, so each time round the tail
// climbs.  Only what's pitched goes in (tonal_run), and only over the gate.
#define SHIMMER_COMBS 8
#define SHIMMER_ALLPASSES 4
#define SHIMMER_COMB_FRAMES 4096     // enough for 1617 at up to 96kHz
#define SHIMMER_ALLPASS_FRAMES 2048
#define SHIMMER_ROOM 0.88            // the combs' feedback: how long it rings
#define SHIMMER_DAMP 0.35
#define SHIMMER_INPUT 0.015          // Freeverb's fixed gain in
#define SHIMMER_FEEDBACK 0.35        // of the octave up, back in
#define SHIMMER_GRAIN_S 0.060
#define SHIMMER_WET 2.0              // about 4dB under the mandolin
#define SHIMMER_TAIL_S 12            // how long to go on after it stops
#define TONAL_ATTACK_S 0.005
#define TONAL_RELEASE_S 0.040

static const int SHIMMER_COMB_44K[SHIMMER_COMBS] = {
  1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int SHIMMER_ALLPASS_44K[SHIMMER_ALLPASSES] = {556, 441, 341, 225};

typedef struct {
  float comb[SHIMMER_COMBS][SHIMMER_COMB_FRAMES];
  float allpass[SHIMMER_ALLPASSES][SHIMMER_ALLPASS_FRAMES];
  int comb_len[SHIMMER_COMBS], allpass_len[SHIMMER_ALLPASSES];
  int comb_pos[SHIMMER_COMBS], allpass_pos[SHIMMER_ALLPASSES];
  float store[SHIMMER_COMBS];
  VfxShifter up;
  double grain;
  float last;  // its last output, for the octave up
} Shimmer;

static void shimmer_prepare(Shimmer* r, double rate) {
  memset(r, 0, sizeof(*r));
  for (int i = 0; i < SHIMMER_COMBS; i++) {
    r->comb_len[i] = (int)(SHIMMER_COMB_44K[i] * rate / 44100);
    if (r->comb_len[i] >= SHIMMER_COMB_FRAMES) {
      r->comb_len[i] = SHIMMER_COMB_FRAMES - 1;
    }
  }
  for (int i = 0; i < SHIMMER_ALLPASSES; i++) {
    r->allpass_len[i] = (int)(SHIMMER_ALLPASS_44K[i] * rate / 44100);
    if (r->allpass_len[i] >= SHIMMER_ALLPASS_FRAMES) {
      r->allpass_len[i] = SHIMMER_ALLPASS_FRAMES - 1;
    }
  }
  r->up.phase[1] = 0.5;
  r->grain = rate * SHIMMER_GRAIN_S;
}

static float shimmer_run(Shimmer* r, float in) {
  float up = (float)vfx_shift(&r->up, r->last, 2.0, r->grain);
  // Soft-limited, so the octave going round can't build up without end.
  float x = (float)(SHIMMER_INPUT * (in + tanh(SHIMMER_FEEDBACK * up)));
  float out = 0;
  for (int i = 0; i < SHIMMER_COMBS; i++) {
    float y = r->comb[i][r->comb_pos[i]];
    r->store[i] = (float)(y * (1 - SHIMMER_DAMP) + r->store[i] * SHIMMER_DAMP);
    r->comb[i][r->comb_pos[i]] = (float)(x + r->store[i] * SHIMMER_ROOM);
    r->comb_pos[i] = (r->comb_pos[i] + 1) % r->comb_len[i];
    out += y;
  }
  for (int i = 0; i < SHIMMER_ALLPASSES; i++) {
    float b = r->allpass[i][r->allpass_pos[i]];
    r->allpass[i][r->allpass_pos[i]] = out + b * 0.5f;
    r->allpass_pos[i] = (r->allpass_pos[i] + 1) % r->allpass_len[i];
    out = b - out;
  }
  r->last = out;
  return out;
}

// ---------------------------------------------------------------------------
// On the audio thread
// ---------------------------------------------------------------------------

static struct {
  double rate;
  struct Engine engine;  // the Bass
  Vocoder vocoder;
  MandoLine tuner;
  RoomGate gate;  // the Bass's and Shimmer's
  WhistleRamp dry, voice[N_MANDO_VOICES];
  WhistleRamp heard;  // Shimmer's tail, which rings on after its send stops
  WhistleRamp boost;
  // Breath FX's level for the voices, and for the Resonator, following the
  // breath smoothly; and the Bass's switch.
  double breath, chain_breath, breath_smooth;
  WhistleRamp bass_breath;
  MandoClock clock;
  ChordStrings drone;
  SynthPedal synth;
  Talkbox talkbox;
  Drive drive;
  ChordStrings resonator;
  HarmTrem harm_trem;
  Leslie leslie;
  // Shimmer
  Shimmer shimmer;
  MandoLine tonal;
  double tonal_target, tonal_level, tonal_attack, tonal_release;
  long shimmer_tail;  // frames it has left to ring
} mando;

// The limiter mando_add puts on each side once the mandolin's on it, so
// nothing on it -- the Talkbox's resonances, Boost, the volume turned up,
// whatever else is on that side with it -- ever clips.  Quiet enough and it
// does nothing; over MANDO_LIMIT it comes down right away, on the sample
// that went over, and back up over MANDO_LIMIT_RELEASE_S.  About -1dBFS.
#define MANDO_LIMIT 0.89
#define MANDO_LIMIT_RELEASE_S 0.080

typedef struct {
  double env;  // the peak it's holding down to MANDO_LIMIT, falling back
} MandoLimiter;

static MandoLimiter mando_limiter[2];  // left, right
static double mando_limit_release;

static void mando_limit(MandoLimiter* l, float* buf, int len) {
  for (int i = 0; i < len; i++) {
    double a = fabs(buf[i]);
    l->env = a > l->env ? a : l->env + (a - l->env) * mando_limit_release;
    if (l->env > MANDO_LIMIT) buf[i] = (float)(buf[i] * MANDO_LIMIT / l->env);
  }
}

// Its block, summed onto the right by mando_add after the right's level, and
// its voices', onto the left.  As long as the longest block
// the input takes (WHISTLE_MAX_BLOCK).
#define MANDO_MAX_BLOCK 8192
static float mando_block[MANDO_MAX_BLOCK];
static float mando_voice_block[MANDO_MAX_BLOCK];
static int mando_block_len;

// Which of the whistle's engine voices is its Bass: see
// whistle_resolve_voices.  Set before the stream starts.
static int mando_bass_engine_voice = 1;

// Called with no audio running, as the whistle's engine is.
static void mando_prepare(double rate) {
  memset(&mando.dry, 0, sizeof(mando.dry));
  memset(mando.voice, 0, sizeof(mando.voice));
  mando.rate = rate;
  engine_init(&mando.engine, (float)rate);
  engine_set_voice(&mando.engine, mando_bass_engine_voice);
  engine_set_volume(&mando.engine, WHISTLE_VOLUME_DEFAULT);
  engine_set_octave(&mando.engine, MANDO_BASS_OCTAVE);
  engine_set_level_full(&mando.engine, MANDO_BASS_LEVEL_FULL);
  engine_set_range(&mando.engine, MANDO_BASS_LOW_HZ, MANDO_BASS_HIGH_HZ);
  vocoder_prepare(&mando.vocoder, rate);
  mando_line_prepare(&mando.tuner, rate, TUNER_RATE);
  room_gate_init(&mando.gate, rate);
  memset(&mando.heard, 0, sizeof(mando.heard));
  WhistleRamp unity = {.live = 1, .target = 1};
  mando.boost = mando.bass_breath = unity;
  mando.breath = mando.chain_breath = 1;
  mando.breath_smooth = 1 - exp(-1 / (rate * MANDO_BREATH_SMOOTH_S));
  memset(&mando.clock, 0, sizeof(mando.clock));
  strings_prepare(&mando.drone, DRONE_LOW, DRONE_STRINGS, DRONE_RING_S,
                  DRONE_STOP_S, DRONE_DAMP, rate);
  strings_prepare(&mando.resonator, RESONATOR_LOW, RESONATOR_STRINGS,
                  RESONATOR_RING_S, RESONATOR_STOP_S, RESONATOR_DAMP, rate);
  htrem_prepare(&mando.harm_trem, rate);
  synth_prepare(&mando.synth, rate);
  talkbox_prepare(&mando.talkbox, rate);
  drive_prepare(&mando.drive, rate);
  leslie_prepare(&mando.leslie, rate);
  shimmer_prepare(&mando.shimmer, rate);
  mando_line_prepare(&mando.tonal, rate, TONAL_RATE);
  mando.tonal_target = mando.tonal_level = 0;
  mando.tonal_attack = 1 - exp(-1 / (rate * TONAL_ATTACK_S));
  mando.tonal_release = 1 - exp(-1 / (rate * TONAL_RELEASE_S));
  mando.shimmer_tail = 0;
  memset(mando_limiter, 0, sizeof(mando_limiter));
  mando_limit_release = 1 - exp(-1 / (rate * MANDO_LIMIT_RELEASE_S));
  mando_block_len = 0;
}

// Would this voice be heard, or be fading out?
static bool mando_ramp_live(const WhistleRamp* r) {
  return r->live > 0 || r->target > 0;
}

static void mando_rec_push(const float* in, int len);  // below

// The mandolin, from the second input, into mando_block, and its voices
// into mando_voice_block.  Called from whistle_mix with the block it popped.
static void mando_process(const float* in, int len, double rate) {
  mando_block_len = 0;
  if (len > MANDO_MAX_BLOCK) return;
  if (mando.rate != rate) mando_prepare(rate);
  unsigned pub = atomic_load_explicit(&mando_pub, memory_order_relaxed);
  bool on = pub & MANDO_PUB_ON;
  bool tuning = pub & MANDO_BIT(MANDO_TUNER);
  float ramp_frames = (float)fmax(1, rate * 0.010);
  // The Tuner mutes all of it.
  bool heard = on && !tuning;
  whistle_ramp_to(&mando.dry, heard ? 1 : 0, ramp_frames);
  // Boost is on all of it, the mandolin and everything on it.
  whistle_ramp_to(&mando.boost,
                  pub & MANDO_BIT(MANDO_BOOST) ? MANDO_BOOST_GAIN : 1.0f,
                  ramp_frames);
  for (int v = MANDO_VOCODER; v < N_MANDO_VOICES; v++) {
    if (v == MANDO_BREATH) continue;
    // The ones in the chain crossfade in and out whether or not it's heard:
    // the dry ramp's what mutes it.
    bool in_chain = v >= MANDO_TALKBOX;
    whistle_ramp_to(&mando.voice[v],
                    (heard || in_chain) && (pub & MANDO_BIT(v)) ? 1 : 0,
                    ramp_frames);
  }
  // Breath FX: the effects from none at rest to 300% at full, following
  // the breath smoothly -- the voices, and the Resonator; Drive pushed by it
  // -- and the Bass on with any breath and off with none.
  bool breathing = pub & MANDO_BIT(MANDO_BREATH);
  double blown = breath_blown(
    atomic_load_explicit(&audio_breath, memory_order_relaxed));
  double breath_level = breathing ? MANDO_BREATH_HIGH * blown : 1;
  double chain_level = pub & MANDO_BIT(MANDO_RESONATOR) ? breath_level : 1;
  // The Leslie's motor: flat out, or with Breath FX, the breath.
  double motor = breathing ? blown : 1;
  whistle_ramp_to(&mando.bass_breath, !breathing || blown > 0 ? 1 : 0,
                  ramp_frames);
  whistle_ramp_to(&mando.heard, heard ? 1 : 0, ramp_frames);
  bool bass = mando_ramp_live(&mando.voice[MANDO_BASS]);
  bool vocoder = mando_ramp_live(&mando.voice[MANDO_VOCODER]);
  bool drone = mando_ramp_live(&mando.voice[MANDO_DRONE]);
  bool synth = mando_ramp_live(&mando.voice[MANDO_SYNTH]);
  bool talkbox = mando_ramp_live(&mando.voice[MANDO_TALKBOX]);
  bool drive = mando_ramp_live(&mando.voice[MANDO_DRIVE]);
  bool resonator = mando_ramp_live(&mando.voice[MANDO_RESONATOR]);
  bool harm_trem = mando_ramp_live(&mando.voice[MANDO_HARM_TREM]);
  bool tremolo = mando_ramp_live(&mando.voice[MANDO_TREMOLO]);
  bool leslie = mando_ramp_live(&mando.voice[MANDO_LESLIE]);
  double synth_hz[SYNTH_VOICES];
  if (synth) synth_chord(synth_hz);
  // What follows the chord, told it once a block, and the beat.
  int pc[3];
  int pcs = mando_chord_pcs(pc);
  if (drone) strings_chord(&mando.drone, pc, 2);  // no third
  if (resonator) strings_chord(&mando.resonator, pc, pcs);
  double split = htrem_split(pc[0]);
  mando_clock_block(&mando.clock, len, rate);
  bool shimmer_in = mando_ramp_live(&mando.voice[MANDO_SHIMMER]);
  if (shimmer_in) mando.shimmer_tail = (long)(SHIMMER_TAIL_S * rate);
  bool shimmer = shimmer_in || mando.shimmer_tail > 0;
  double chord_hz[VOCODER_VOICES], chord_weight[VOCODER_VOICES];
  int chord_notes = vocoder ? vocoder_chord(chord_hz, chord_weight) : 0;
  int gate_db = atomic_load_explicit(&mando_pub_gate_db,
                                     memory_order_relaxed);
  double threshold = pow(10, gate_db / 20.0);
  mando.gate.threshold = threshold;
  mando.vocoder.room.threshold = threshold * MANDO_VOCODER_INPUT_GAIN;

  mando_rec_push(in, len);

  float peak = 0;
  for (int i = 0; i < len; i++) {
    float x = in[i];
    if (fabsf(x) > peak) peak = fabsf(x);
    if (tuning) tuner_run(&mando.tuner, x);
    mando.breath += (breath_level - mando.breath) * mando.breath_smooth;
    mando.chain_breath += (chain_level - mando.chain_breath) *
                          mando.breath_smooth;
    mando.clock.beats += mando.clock.per_frame;
    // The chain: each crossfaded in and out.  With Breath FX, the Resonator
    // is crossfaded in from what went into it as the breath comes, all of it by a third of the breath, and
    // louder after that; Drive pushed harder; and the tremolos deepened, all
    // the way by that third.  Not the Talkbox, which the breath already
    // plays, nor the Leslie, which it spins.
    double dry = x;
    if (talkbox) {
      double r = whistle_ramp_next(&mando.voice[MANDO_TALKBOX]);
      dry += r * (talkbox_run(&mando.talkbox, dry, blown, rate) - dry);
    }
    if (drive) {
      double r = whistle_ramp_next(&mando.voice[MANDO_DRIVE]);
      // With Breath FX, the breath pushes it: followed as the voices are.
      double gain = breathing
        ? drive_gain(fmin(1, mando.breath / MANDO_BREATH_HIGH)) : DRIVE_GAIN;
      dry += r * (drive_run(&mando.drive, dry, gain) - dry);
    }
    double pre = dry;
    if (resonator) {
      double r = whistle_ramp_next(&mando.voice[MANDO_RESONATOR]);
      double wet = RESONATOR_DRY * dry +
                   RESONATOR_WET * strings_run(&mando.resonator, dry);
      dry += r * (wet - dry);
    }
    if (resonator) {
      double e = mando.chain_breath;
      dry = pre * (1 - fmin(e, 1)) + dry * e;
    }
    double depth = fmin(mando.breath, 1);
    if (harm_trem) {
      double r = whistle_ramp_next(&mando.voice[MANDO_HARM_TREM]);
      dry += r * (htrem_run(&mando.harm_trem, dry, split, mando.clock.beats,
                            depth, rate) - dry);
    }
    if (tremolo) {
      double r = whistle_ramp_next(&mando.voice[MANDO_TREMOLO]);
      dry += r * (trem_run(dry, &mando.clock, depth) - dry);
    }
    if (leslie) {
      double r = whistle_ramp_next(&mando.voice[MANDO_LESLIE]);
      dry += r * (leslie_run(&mando.leslie, dry, motor, rate) - dry);
    }
    float boost = whistle_ramp_next(&mando.boost);
    mando_block[i] = boost * whistle_ramp_next(&mando.dry) * (float)dry;
    float out = 0, bass_out = 0;
    // How pitched it is, for Shimmer's send and the Synth.
    if (shimmer || synth) {
      double tonal = tonal_run(&mando.tonal, x);
      if (tonal >= 0) mando.tonal_target = tonal;
      mando.tonal_level += (mando.tonal_target - mando.tonal_level) *
        (mando.tonal_target > mando.tonal_level ? mando.tonal_attack
                                                : mando.tonal_release);
    }
    double gate = bass || synth ? room_gate_run(&mando.gate, x) : 0;
    if (synth) {
      out += whistle_ramp_next(&mando.voice[MANDO_SYNTH]) *
        (float)synth_run(&mando.synth, x, synth_hz, gate * mando.tonal_level,
                         gate_db, rate);
    }
    if (bass) {
      // Gated going in, so the room doesn't play it, and hotter.
      float gated = (float)(x * gate) * MANDO_BASS_INPUT_GAIN;
      float left, right;
      engine_process_stereo(&mando.engine, gated, &left, &right);
      bass_out = whistle_ramp_next(&mando.voice[MANDO_BASS]) *
                 0.5f * (left + right);
    }
    if (vocoder) {
      out += whistle_ramp_next(&mando.voice[MANDO_VOCODER]) *
        vocoder_process(&mando.vocoder, x * MANDO_VOCODER_INPUT_GAIN,
                        chord_hz, chord_weight, chord_notes, 1);
    }
    if (drone) {
      out += whistle_ramp_next(&mando.voice[MANDO_DRONE]) *
        (float)(DRONE_LEVEL * strings_run(&mando.drone, x));
    }
    if (shimmer) {
      // The send switches, and the tail rings on after it.
      float send = whistle_ramp_next(&mando.voice[MANDO_SHIMMER]) *
                   (float)(x * mando.tonal_level);
      out += whistle_ramp_next(&mando.heard) * (float)SHIMMER_WET *
             shimmer_run(&mando.shimmer, send);
      if (!shimmer_in) mando.shimmer_tail--;
    }
    mando_voice_block[i] = boost *
      ((float)mando.breath * out +
       whistle_ramp_next(&mando.bass_breath) * bass_out);
  }
  mando_block_len = len;
  int scaled = (int)(peak * 10000);
  if (scaled > atomic_load_explicit(&mando_meter_level,
                                    memory_order_relaxed)) {
    atomic_store_explicit(&mando_meter_level, scaled, memory_order_relaxed);
  }
}

// Onto the right channel, and its voices onto the left, or both onto the only
// one on a mono device, and each side limited.  After the right's level: see
// the top.  On the audio thread.
static void mando_add(float** out, int nout, int len) {
  if (nout < 1 || mando_block_len != len) {
    mando_block_len = 0;
    return;
  }
  float* right = out[nout >= 2 ? 1 : 0];
  float* voices = out[0];
  float gain = atomic_load_explicit(&mando_gain, memory_order_relaxed);
  for (int i = 0; i < len; i++) {
    right[i] += gain * mando_block[i];
    voices[i] += gain * mando_voice_block[i];
  }
  mando_limit(&mando_limiter[0], voices, len);
  if (right != voices) mando_limit(&mando_limiter[1], right, len);
  mando_block_len = 0;
}

// ---------------------------------------------------------------------------
// What the keys do, on the main thread with jammer_lock held
// ---------------------------------------------------------------------------

static void mando_toggle(void) {
  mando_on = !mando_on;
  mando_publish();
}

// From the Vocal FX menu.
static void mando_set_gate(int db) {
  mando_gate_db = db;
  mando_publish();
}

static void mando_set_voice(int v) {
  if (v < 0 || v >= N_MANDO_VOICES) return;
  mando_voices ^= MANDO_BIT(v);
  mando_publish();
}

// esc: back to how it starts, on and playing straight through.
static void mando_reset(void) {
  mando_on = true;
  mando_selected = false;
  mando_voices = 0;
  mando_publish();
}

// The mandolin hears the second input, so there's none without one.
__attribute__((unused))
static bool mando_has_input(void) {
  return atomic_load_explicit(&whistle_input_count,
                              memory_order_relaxed) >= 2;
}

// ---------------------------------------------------------------------------
// Recording it
//
// Mandolin > Record Mandolin: the second input as it comes, before anything
// is done to it, into a WAV -- 32-bit float, mono, the rig's rate, as the
// number clips are -- for trying the effects against afterwards.  The audio
// thread only copies each block into a ring; a thread of the recording's
// own drains it to the file, so the disk never holds the audio up.  A block
// that finds the ring full is dropped, and counted.
// ---------------------------------------------------------------------------

#define MANDO_REC_RING (1u << 20)  // about 20s at 48kHz: plenty of slack

static float mando_rec_ring[MANDO_REC_RING];
static _Atomic unsigned mando_rec_write, mando_rec_read;
static _Atomic int mando_recording;      // the audio thread should copy
static _Atomic long long mando_rec_frames;  // written to the file so far
static _Atomic int mando_rec_dropped;    // blocks the ring had no room for

static struct {
  FILE* file;
  pthread_t thread;
  _Atomic int stop;
  double rate;
  char path[1024];
} mando_rec;

// Audio thread, from mando_process: the block as it came in.
static void mando_rec_push(const float* in, int len) {
  if (!atomic_load_explicit(&mando_recording, memory_order_acquire)) return;
  unsigned write = atomic_load_explicit(&mando_rec_write,
                                        memory_order_relaxed);
  unsigned read = atomic_load_explicit(&mando_rec_read, memory_order_acquire);
  if (MANDO_REC_RING - (write - read) < (unsigned)len) {
    atomic_fetch_add_explicit(&mando_rec_dropped, 1, memory_order_relaxed);
    return;
  }
  for (int i = 0; i < len; i++) {
    mando_rec_ring[(write + (unsigned)i) & (MANDO_REC_RING - 1)] = in[i];
  }
  atomic_store_explicit(&mando_rec_write, write + (unsigned)len,
                        memory_order_release);
}

static void mando_wav_header(FILE* f, double rate, long long frames) {
  uint32_t data = (uint32_t)(frames * 4);
  uint32_t r = (uint32_t)rate;
  uint32_t riff = 36 + data, fmt_len = 16, bytes_per_s = r * 4;
  uint16_t format = 3 /* float */, channels = 1, align = 4, bits = 32;
  fseek(f, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt_len, 4, 1, f);
  fwrite(&format, 2, 1, f); fwrite(&channels, 2, 1, f);
  fwrite(&r, 4, 1, f); fwrite(&bytes_per_s, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fseek(f, 0, SEEK_END);
}

// Whatever's in the ring, onto the end of the file.
static void mando_rec_drain(void) {
  unsigned read = atomic_load_explicit(&mando_rec_read, memory_order_relaxed);
  unsigned write = atomic_load_explicit(&mando_rec_write,
                                        memory_order_acquire);
  while (read != write) {
    unsigned at = read & (MANDO_REC_RING - 1);
    unsigned n = write - read;
    if (n > MANDO_REC_RING - at) n = MANDO_REC_RING - at;  // to the wrap
    fwrite(mando_rec_ring + at, sizeof(float), n, mando_rec.file);
    read += n;
    atomic_fetch_add_explicit(&mando_rec_frames, n, memory_order_relaxed);
  }
  atomic_store_explicit(&mando_rec_read, read, memory_order_release);
}

static void* mando_rec_thread(void* unused) {
  (void)unused;
  while (!atomic_load_explicit(&mando_rec.stop, memory_order_acquire)) {
    mando_rec_drain();
    usleep(50000);
  }
  mando_rec_drain();
  return NULL;
}

// Main thread.  Start recording into `path`, at `rate`; false, and errno,
// if the file can't be made.
static bool mando_rec_start(const char* path, double rate) {
  if (mando_rec.file) return true;
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  snprintf(mando_rec.path, sizeof(mando_rec.path), "%s", path);
  mando_rec.file = f;
  mando_rec.rate = rate;
  mando_wav_header(f, rate, 0);
  atomic_store(&mando_rec_frames, 0);
  atomic_store(&mando_rec_dropped, 0);
  atomic_store(&mando_rec.stop, 0);
  // Anything from before now isn't this recording's.
  atomic_store(&mando_rec_read, atomic_load(&mando_rec_write));
  pthread_create(&mando_rec.thread, NULL, mando_rec_thread, NULL);
  atomic_store_explicit(&mando_recording, 1, memory_order_release);
  return true;
}

// Main thread.  Stop, and finish the file; how many seconds it kept.
static double mando_rec_stop(void) {
  if (!mando_rec.file) return 0;
  atomic_store_explicit(&mando_recording, 0, memory_order_release);
  atomic_store_explicit(&mando_rec.stop, 1, memory_order_release);
  pthread_join(mando_rec.thread, NULL);
  long long frames = atomic_load(&mando_rec_frames);
  mando_wav_header(mando_rec.file, mando_rec.rate, frames);
  fclose(mando_rec.file);
  mando_rec.file = NULL;
  return frames / mando_rec.rate;
}

static bool mando_rec_running(void) {
  return mando_rec.file != NULL;
}

#endif
