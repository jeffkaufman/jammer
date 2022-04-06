#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void select_endpoint_voice(int endpoint, int voice, int bank, int volume_delta,
                           int manual_volume, bool pan);

#define CHANNEL_DRUM 9

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
#define N_ENDPOINTS (ENDPOINT_DRUM+1)
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
