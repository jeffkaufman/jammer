#ifndef JML_VOICES_H
#define JML_VOICES_H

// Per-voice and per-endpoint volume tuning, shared by the Linux and Mac
// builds.  Include after jammermidilib.h, which supplies MIDI_MAX and the
// send_midi()/choose_voice() platform API.

void select_endpoint_voice(int endpoint, int voice, int bank, int volume_delta,
                           int manual_volume, bool pan) {
  send_midi(MIDI_CC, CC_07, 0, endpoint);

  int volume = 70;
  switch (voice) {

  case 80:
    volume = 62;
    break;
  case 81:
    volume = 90;
    break;
  case 84:
    volume = 68;
    break;
  case 38:
    volume = 87;
    break;
  case 85:
    volume = 80;
    break;
  case 75:
    volume = 87;
    break;
  case 39:
    volume = 86;
    break;
  case 7:
    volume = 98;
    break;
  case 35:
    volume = 107;
    break;
  case 24:
    volume = 102;
    break;
  case 64:
    volume = 87;
    break;
  case 66:
    volume = 82;
    break;
  case 67:
    volume = 92;
    break;
  case 26:
  case 28:
    volume = 112;
    break;
  case 4:
    volume = 122;
    break;
  case 0:
    volume = 100;
    break;
  case 18:
    volume = 65;
    break;
  case 5:
    volume = 90;
    break;
  case 16:
    volume = 110;
    break;
  case 32:
    volume = 115;
    break;
  }

  if (endpoint == ENDPOINT_DRUM) {
    volume = MIDI_MAX;
  }
  
  if (manual_volume != -1) {
    volume = manual_volume;
  }

  volume += volume_delta;

  if (endpoint == ENDPOINT_FLEX) {
    volume -= 24;
  } else if (is_footbass(endpoint)) {
    volume -= 20;
  } else if (endpoint == ENDPOINT_ARP) {
    volume -= 40;
  } else if (endpoint == ENDPOINT_LOW) {
    volume += 10;
  } 

  if (endpoint == ENDPOINT_OVERLAY ||
      endpoint == ENDPOINT_FLEX ||
      endpoint == ENDPOINT_HI ||
      endpoint == ENDPOINT_ARP) {
    if (voice == 39) {
      volume -= 10;
    } else if (voice == 26) {
      volume -= 20;
    } else if (voice == 4) {
      volume -= 15;
    } else if (voice == 64) {
      volume -= 10;
    }
  }

  send_midi(MIDI_CC, CC_07, volume, endpoint);
  send_midi(MIDI_CC, CC_PAN, pan ? MIDI_MAX : 0, endpoint);
  send_midi(MIDI_CC, CC_BALANCE, pan ? MIDI_MAX : 0, endpoint);

  if (endpoint != CHANNEL_DRUM) {
    choose_voice(endpoint, bank, voice);
  }
}

#endif
