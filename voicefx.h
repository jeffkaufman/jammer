#ifndef JML_VOICE_FX_H
#define JML_VOICE_FX_H

// The whistle's vocal effects beside the vocoder: what goes into the
// microphone, through one of these, out to the rig.  One at a time, on J K L
// while the whistle's selected, or from the Vocal FX menu, and over
// whichever whistle voice is playing, as the vocoder is.
//
//   Robot      ring modulated with the chord: its root, and its third and
//              fifth beside it, so it changes colour with the chord as well
//              as pitch
//   Voice Bass a bass an octave under the voice, following its pitch and
//              nothing else: a sub that sings where the voice does, and the
//              voice itself taken down the octave with it
//
// Each goes through the same gate as the vocoder (RoomGate), so the band in
// the microphone doesn't reach the PA through it, and a gain that meets the
// microphone halfway, as the vocoder does, so a quiet one still carries and a
// loud one doesn't bury the rig.
//
// On the audio thread, realtime safe.  Include from whistle.h, after the
// vocoder, which this shares its gate with.  Which is which: the VFX_
// numbers in whistle.h, beside the keys they're on.

// Where each sits against the vocoder, by level, measured with
// ./voicefx-levels over the kept number clips.
static const double VFX_LEVEL[N_VFX] = {
  [VFX_ROBOT] = 0.58, [VFX_BASS] = 1.41,
};

// The halfway gain: the voice's level goes as the square root of what comes
// in, followed slowly enough not to distort it.
#define VFX_AGC 0.45
#define VFX_AGC_FLOOR 0.005

// Voice Bass's pitch detector, YIN on the input taken down to about 12kHz:
// how far back to look, what to look for, and how often.
#define VFX_TUNE_RATE 12000
#define VFX_TUNE_LINE 1024        // decimated samples kept
#define VFX_TUNE_WINDOW_S 0.030   // compared at each lag
#define VFX_TUNE_HOP_S 0.005      // between looks
#define VFX_TUNE_LOW_HZ 70
#define VFX_TUNE_HIGH_HZ 700
#define VFX_TUNE_THRESHOLD 0.15   // YIN's: lower is stricter
#define VFX_TUNE_HOLD_S 0.060     // a gap in the pitch that isn't a stop
// And its shifter, and its sub: how fast that follows the voice's pitch and
// level, and how much of each goes out.
#define VFX_SHIFT_FRAMES 8192
#define VFX_BASS_GLIDE_S 0.008
#define VFX_BASS_SUB 0.8
#define VFX_BASS_VOICE 0.5

typedef struct {
  float buf[VFX_SHIFT_FRAMES];
  int pos;
  double phase[2];  // the two grains', 0-1
  double grain[2];  // and their lengths, in samples
} VfxShifter;

static struct {
  double rate;
  RoomGate room;
  double agc;          // the slow level the halfway gain goes by
  double agc_attack, agc_release;

  // Robot
  double robot_phase[3];

  // Voice Bass
  float line[VFX_TUNE_LINE];  // decimated, newest at head
  int head, filled;
  int decimate, dec_n, hop_n;
  double dec_sum;
  double pitch_hz;     // the voice's, or 0 when there isn't one
  int unpitched;       // hops since it last had one
  VfxShifter shifter;
  double sub_hz, sub_phase;  // the sub, gliding after the voice
  double sub_level;          // and its level, following the voice's
  double glide;
} vfx;

static void vfx_prepare(double rate) {
  memset(&vfx, 0, sizeof(vfx));
  vfx.rate = rate;
  room_gate_init(&vfx.room, rate);
  vfx.agc = VFX_AGC_FLOOR;
  vfx.agc_attack = 1 - exp(-1 / (rate * 0.010));
  vfx.agc_release = 1 - exp(-1 / (rate * 0.300));
  vfx.decimate = (int)lround(rate / VFX_TUNE_RATE);
  if (vfx.decimate < 1) vfx.decimate = 1;
  vfx.shifter.phase[1] = 0.5;
  vfx.glide = 1 - exp(-1 / (rate * VFX_BASS_GLIDE_S));
}

// What each block needs to know, worked out once for it.
typedef struct {
  double carrier_hz[3], carrier_weight[3];  // Robot's
} VfxBlock;

static void vfx_block(VfxBlock* b) {
  memset(b, 0, sizeof(*b));
  int root = atomic_load_explicit(&audio_chord_root, memory_order_relaxed);
  int third = atomic_load_explicit(&audio_chord_third, memory_order_relaxed);
  int fifth = atomic_load_explicit(&audio_chord_fifth, memory_order_relaxed);
  // The chord from C3 up, the root strongest; the third only once the feet
  // or a voice have said which it is.
  b->carrier_hz[0] = midi_hz(48 + root % 12);
  b->carrier_weight[0] = 0.6;
  b->carrier_hz[1] = midi_hz(48 + (root + third) % 12);
  b->carrier_weight[1] = third ? 0.35 : 0;
  b->carrier_hz[2] = midi_hz(48 + (root + fifth) % 12);
  b->carrier_weight[2] = 0.35;
}

// Read `delay` samples back from `pos` in a ring of `size`, between samples.
static float vfx_read(const float* buf, int size, int pos, double delay) {
  double at = pos - delay;
  while (at < 0) at += size;
  int i = (int)at;
  double frac = at - i;
  float a = buf[i % size], b = buf[(i + 1) % size];
  return (float)(a + (b - a) * frac);
}

// Robot: the voice times the chord.
static float vfx_robot(float x, const VfxBlock* b) {
  double carrier = 0;
  for (int i = 0; i < 3; i++) {
    vfx.robot_phase[i] += b->carrier_hz[i] / vfx.rate;
    if (vfx.robot_phase[i] >= 1) vfx.robot_phase[i] -= 1;
    carrier += b->carrier_weight[i] * sin(2 * M_PI * vfx.robot_phase[i]);
  }
  return (float)(1.4 * x * carrier);
}

// The voice's pitch over the last VFX_TUNE_WINDOW_S of the line, by YIN, or 0
// if it hasn't got one.
static double vfx_pitch(void) {
  double rate = vfx.rate / vfx.decimate;
  int window = (int)(rate * VFX_TUNE_WINDOW_S);
  int lo = (int)(rate / VFX_TUNE_HIGH_HZ), hi = (int)(rate / VFX_TUNE_LOW_HZ);
  if (vfx.filled < window + hi + 2) return 0;
  // x(i) is i samples before the newest.
  #define VFX_AT(i) \
    vfx.line[(vfx.head - 1 - (i) + 2 * VFX_TUNE_LINE) % VFX_TUNE_LINE]
  double d[VFX_TUNE_LINE / 2];
  double running = 0;
  int found = 0;
  for (int tau = 1; tau <= hi + 1; tau++) {
    double sum = 0;
    for (int i = 0; i < window; i++) {
      double diff = VFX_AT(i) - VFX_AT(i + tau);
      sum += diff * diff;
    }
    running += sum;
    d[tau] = running > 0 ? sum * tau / running : 1;
    // The first dip under the threshold, once it's stopped falling.
    if (!found && tau - 1 >= lo && tau >= 3 &&
        d[tau - 1] < VFX_TUNE_THRESHOLD && d[tau - 1] <= d[tau] &&
        d[tau - 1] <= d[tau - 2]) {
      found = tau - 1;
    }
  }
  #undef VFX_AT
  if (!found) return 0;
  // Between samples, from the dip and its neighbours.
  double a = d[found - 1], b = d[found], c = d[found + 1];
  double denom = a - 2 * b + c;
  double tau = found + (denom != 0 ? 0.5 * (a - c) / denom : 0);
  return rate / tau;
}

// A pitch shifter: two grains reading back through a delay line at `ratio`
// times the speed it's written, half a grain apart, each faded in and out
// so the two always sum to one.
//
// Each grain takes a new length, `grain`, only as it starts over, when its
// fade has it silent.  How far back it reads is its phase times its length,
// so a length that changed partway through jumped it to somewhere else in
// the line, mid-sound: a click, every time the voice's pitch moved.
static double vfx_shift(VfxShifter* s, float x, double ratio, double grain) {
  s->buf[s->pos] = x;
  double out = 0;
  for (int g = 0; g < 2; g++) {
    if (s->grain[g] <= 0) s->grain[g] = grain;
    double next = s->phase[g] + (1 - ratio) / s->grain[g];
    if (next >= 1 || next < 0) s->grain[g] = grain;
    s->phase[g] = next - floor(next);
    double p = s->phase[g];
    double w = sin(M_PI * p);
    out += w * w * vfx_read(s->buf, VFX_SHIFT_FRAMES, s->pos,
                            1 + p * s->grain[g]);
  }
  s->pos = (s->pos + 1) % VFX_SHIFT_FRAMES;
  return out;
}

// Voice Bass: find the voice's pitch, and play a sub an octave under it --
// a sine with a little of its octave, so it carries on a PA that can't do
// the fundamental -- gliding after it and following its level, with the
// voice itself shifted down the octave on top.  Where there's no pitch to go
// by, a consonant or a breath, the sub holds a moment and then fades, and
// the shifted voice carries on.
static float vfx_bass(float x) {
  vfx.dec_sum += x;
  if (++vfx.dec_n >= vfx.decimate) {
    vfx.line[vfx.head] = (float)(vfx.dec_sum / vfx.dec_n);
    vfx.head = (vfx.head + 1) % VFX_TUNE_LINE;
    if (vfx.filled < VFX_TUNE_LINE) vfx.filled++;
    vfx.dec_sum = 0;
    vfx.dec_n = 0;
    if (++vfx.hop_n >= (int)(VFX_TUNE_RATE * VFX_TUNE_HOP_S)) {
      vfx.hop_n = 0;
      double hz = vfx_pitch();
      if (hz > 0) {
        vfx.pitch_hz = hz;
        vfx.unpitched = 0;
      } else if (++vfx.unpitched > (int)(VFX_TUNE_HOLD_S / VFX_TUNE_HOP_S)) {
        vfx.pitch_hz = 0;
      }
    }
  }

  // The sub.  A new note after a gap starts on its pitch, not a glide up
  // from wherever the last one ended.
  double target_hz = vfx.pitch_hz / 2;
  if (target_hz > 0) {
    if (vfx.sub_level < 1e-4) vfx.sub_hz = target_hz;
    vfx.sub_hz += (target_hz - vfx.sub_hz) * vfx.glide;
  }
  double target_level = vfx.pitch_hz > 0 ? fabs(x) : 0;
  vfx.sub_level += (target_level - vfx.sub_level) * vfx.glide * 0.25;
  vfx.sub_phase += vfx.sub_hz / vfx.rate;
  if (vfx.sub_phase >= 1) vfx.sub_phase -= 1;
  double sub = sin(2 * M_PI * vfx.sub_phase) +
               0.35 * sin(4 * M_PI * vfx.sub_phase);

  // The voice, down the octave, in grains of three of its periods.
  double period = vfx.pitch_hz > 0 ? vfx.rate / vfx.pitch_hz
                                   : vfx.rate * 0.008;
  double grain = fmin(fmax(3 * period, vfx.rate * 0.012), vfx.rate * 0.040);
  double down = vfx_shift(&vfx.shifter, x, 0.5, grain);

  // Level times pi/2 for the sine's peak from the rectified voice's mean.
  return (float)(VFX_BASS_SUB * 1.57 * vfx.sub_level * sub +
                 VFX_BASS_VOICE * down);
}

// One sample of `fx` in, out, before the effect volume.
static float vfx_process(int fx, float in, const VfxBlock* b) {
  double gate = room_gate_run(&vfx.room, in);
  double level = fabs(in);
  vfx.agc += (level - vfx.agc) *
             (level > vfx.agc ? vfx.agc_attack : vfx.agc_release);
  float x = (float)(in * gate * VFX_AGC / sqrt(fmax(vfx.agc, VFX_AGC_FLOOR)));
  float y = 0;
  switch (fx) {
  case VFX_ROBOT: y = vfx_robot(x, b); break;
  case VFX_BASS:  y = vfx_bass(x); break;
  }
  return (float)tanh(y * VFX_LEVEL[fx]);
}

#endif
