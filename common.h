#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

/* endpoints */
#define ENDPOINT_SAX 0
#define ENDPOINT_TROMBONE 1
#define ENDPOINT_JAWHARP 2
#define ENDPOINT_BASS_SAX 3
#define ENDPOINT_BASS_TROMBONE 4
#define ENDPOINT_HAMMOND 5
#define ENDPOINT_ORGAN_LOW 6
#define ENDPOINT_ORGAN_FLEX 7
#define ENDPOINT_SINE_PAD 8
#define ENDPOINT_OVERDRIVEN_RHODES 9
#define ENDPOINT_RHODES 10
#define ENDPOINT_SWEEP_PAD 15
#define ENDPOINT_DRUM_A 16
#define ENDPOINT_DRUM_B 17
#define ENDPOINT_DRUM_C 18
#define ENDPOINT_DRUM_D 19
// These three play samples from https://www.jefftk.com/Foot-Percussion--Nightingale--La-Belle-Rose
#define ENDPOINT_FOOT_1 20 // 1-b
#define ENDPOINT_FOOT_3 21 // 3-a
#define ENDPOINT_FOOT_4 22 // 4-a
// These two are from https://www.jefftk.com/p/what-noise-does-a-tambourine-make
#define ENDPOINT_TAMBOURINE_FREE 23
#define ENDPOINT_TAMBOURINE_STOPPED 24
#define ENDPOINT_AUTO_RIGHTHAND 25
#define ENDPOINT_GROOVE_BASS 26
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
