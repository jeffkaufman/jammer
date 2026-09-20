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
#define N_ENDPOINTS (ENDPOINT_FOOTBASS_3+1)
#define N_DRONE_ENDPOINTS (ENDPOINT_DRONE_CHORD+1)

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
