#ifndef JML_COMMON_H
#define JML_COMMON_H

void die(char *errmsg) {
  printf("%s\n",errmsg);
  exit(-1);
}

void select_arp_voice(int voice_index);
void select_jawharp_voice(int voice_index);

/* endpoints */
#define ENDPOINT_JAWHARP 0
#define ENDPOINT_HAMMOND 1
#define ENDPOINT_ORGAN_LOW 2
#define ENDPOINT_ORGAN_FLEX 3
#define ENDPOINT_RHODES 4
#define ENDPOINT_OVERDRIVEN_RHODES 4
#define ENDPOINT_SINE_PAD 6
#define ENDPOINT_SWEEP_PAD 7
#define N_ENDPOINTS (ENDPOINT_SWEEP_PAD+1)

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

#define N_ARP_VOICES 6
static int arp_voices[N_ARP_VOICES] = {
   39,  // Synth Bass 2
   38,  // Synth Bass 1
   84,  // Lead 3 (calliope)
   35,  // Electric Bass (finger)
   26,  // Acoustic Guitar (nylon)
   28,  // Electric Guitar (jazz)
};

#define N_JAWHARP_VOICES 7
static int jawharp_voices[N_JAWHARP_VOICES] = {
   4,  // Electric Piano 1
   24,
   26,
   64,
   66,
   67,
   81,  // Lead 2 (sawtooth)
};

#endif
