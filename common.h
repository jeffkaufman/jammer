#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void select_endpoint_voice(int endpoint, int voice, int bank, int volume_delta,
                           int manual_volume, bool pan);

#define CHANNEL_DRUM 9

// SoundFont keeps the percussion sets in bank 128, above the 127 melodic
// banks.  It's a real bank number here even though it's too big to send as a
// 7-bit MIDI bank-select value.
#define PERCUSSION_BANK 128

// A kit can take its kick from a melodic program played low instead of from a
// percussion sample.  That can't share the drum channel, which is busy being
// a percussion set, so it gets one of the channels above the endpoints.  It
// isn't an endpoint: nothing selects it or switches it on, it just sounds
// when the kit it belongs to plays a kick.
//
// Moved up out of the way when the extra foot basses arrived and took the two
// channels above the drum, one of which was this one.  Any channel above the
// endpoints will do; 15 is the last one and leaves the most room to grow
// into.
#define CHANNEL_PITCHED_KICK 15

// The kick from a percussion set, on a channel of its own so that Kick Duck
// can leave it out of what it ducks.  The Mac makes 16, past the endpoints'
// sixteen, a second percussion channel for it (macapi.h); the Pi's fluidsynth
// has only the one, so there it's the drum channel, as it always was.
#ifndef CHANNEL_KICK
#define CHANNEL_KICK CHANNEL_DRUM
#endif

// The breath controller's range, as the Mac's breath effects read it: below
// BREATH_FLOOR is the controller at rest, and BREATH_FULL is as far as it
// goes.  The Breath Gate opens BREATH_GATE_OPEN of the way between them and
// shuts below BREATH_GATE_SHUT, the gap so a breath hovering at the edge
// doesn't chatter.  Shared because jammermidilib.h restrikes the Breath
// Gate's chord where macapi.h opens its gate.
#define BREATH_FLOOR 4
#define BREATH_FULL 110
#define BREATH_GATE_OPEN 0.12
#define BREATH_GATE_SHUT 0.06

// The breath as 0-1, from the controller at rest to as far as it goes.
static inline double breath_blown(int breath) {
  double x = (double)(breath - BREATH_FLOOR) / (BREATH_FULL - BREATH_FLOOR);
  return x < 0 ? 0 : x > 1 ? 1 : x;
}

// What the breath controller is shaping or playing, one bit each: the
// sweeps (the Mac's 4, 6 and 7), and the Breath Gate's percussion voices
// when it's on and playing one.  jammermidilib.h works out which are on; the
// Mac's audio (macapi.h) does them.  The Pi does none.  The sweeps come first
// and in this order, since macapi.h numbers its filters by them.
enum {
  BREATH_FX_SWEEP_BASS = 1 << 0,
  BREATH_FX_SWEEP_TREBLE = 1 << 1,
  BREATH_FX_SWEEP_PEAK = 1 << 2,
  BREATH_FX_GUIRO = 1 << 3,
  BREATH_FX_WASHBOARD = 1 << 4,
  BREATH_FX_GUIRA = 1 << 5,
};

/* endpoints */
#define ENDPOINT_JAWHARP 0
#define ENDPOINT_DRONE_BASS 1
#define ENDPOINT_DRONE_CHORD 2
// drone endpoints end
#define ENDPOINT_FOOTBASS 3
#define ENDPOINT_ARP 4
#define ENDPOINT_FLEX 5
#define ENDPOINT_LOW 6
#define ENDPOINT_HI 7
#define ENDPOINT_OVERLAY 8
// can't change this, because fluidsynth does percussion on channel 10 (which
// we call 9)
#define ENDPOINT_DRUM CHANNEL_DRUM
// Two more foot basses, above the drum because that one can't move.  Same
// bass line as ENDPOINT_FOOTBASS and the same handling everywhere -- what
// differs is the rhythmic treatment each one is cleared to, so several can
// run at once and land on different beats.  See clear_footbass_2 and
// clear_footbass_3.  Nothing about channel 10 or 11 is special: only 9 is,
// because that's where GM keeps percussion.
#define ENDPOINT_FOOTBASS_2 10
#define ENDPOINT_FOOTBASS_3 11
// A second drone bass and drone chord, so two pads can be layered.  They are
// drones in every way the originals are -- see is_drone -- and start out
// cleared the same way.
#define ENDPOINT_DRONE_BASS_2 12
#define ENDPOINT_DRONE_CHORD_2 13
// The Breath Gate: a drone chord that sounds only while you blow (the Mac's
// audio gates its channel), or, on one of its own voices, percussion the
// breath plays by moving.  A drone like the others otherwise.  The Mac's
// alone: on the Pi nothing switches it on.
#define ENDPOINT_BREATH 14
#define N_ENDPOINTS (ENDPOINT_BREATH+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0
#define MIDI_PITCH_BEND 0xe0

#define CC_BANK_SELECT 0x00
#define CC_MOD 0x01
#define CC_BREATH 0x02
#define CC_07 0x07
#define CC_BALANCE 0x08
#define CC_PAN 0x0a
#define CC_11 0x0b

#endif
