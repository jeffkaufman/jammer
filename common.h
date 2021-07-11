#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

/* endpoints */
#define ENDPOINT_JAWHARP 0
#define ENDPOINT_HAMMOND 1
#define ENDPOINT_ORGAN_LOW 2
#define ENDPOINT_ORGAN_FLEX 3
#define ENDPOINT_SINE_PAD 4
#define ENDPOINT_OVERDRIVEN_RHODES 5
#define ENDPOINT_RHODES 6
#define ENDPOINT_SWEEP_PAD 7
#define ENDPOINT_DRUM_A 10
#define ENDPOINT_DRUM_B 10
#define ENDPOINT_DRUM_C 10
#define ENDPOINT_DRUM_D 10
#define ENDPOINT_AUTO_RIGHTHAND 12
#define ENDPOINT_GROOVE_BASS 13
#define N_ENDPOINTS (ENDPOINT_GROOVE_BASS+1)

// aliases
#define ENDPOINT_FOOTBASS ENDPOINT_ORGAN_LOW


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
