#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void select_endpoint_voice(int endpoint, int voice, int volume_delta);

/* endpoints */
#define ENDPOINT_JAWHARP 0
#define ENDPOINT_FOOTBASS 1
#define ENDPOINT_FLEX 2
#define ENDPOINT_LOW 3
#define ENDPOINT_HI 4
#define ENDPOINT_OVERLAY 5
#define N_ENDPOINTS (ENDPOINT_OVERLAY+1)

/* midi values */
#define MIDI_OFF 0x80
#define MIDI_ON 0x90
#define MIDI_CC 0xb0
#define MIDI_PITCH_BEND 0xe0

#define CC_MOD 0x01
#define CC_BREATH 0x02
#define CC_07 0x07
#define CC_11 0x0b

#endif
