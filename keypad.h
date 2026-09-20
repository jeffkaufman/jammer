#ifndef JML_KEYPAD_H
#define JML_KEYPAD_H

// Turning key presses into handle_keypad() calls, and working out which keys
// should be lit.  Kept apart from the Cocoa code so test-keypad.c can drive it
// directly.
//
// Include after jammermidilib.h and keylayout.h.

// ---------------------------------------------------------------------------
// Keypad: the Mac keyboard standing in for kbd.py
// ---------------------------------------------------------------------------

// kbd.py collects three digits after DELETE or F8 and sends them as the
// velocity of a note for that key.  Same here.
static int armed_note = 0;
static char digits_read[4];
static int n_digits_read = 0;

// Caller must hold the lock.
static void keypad_key(int note) {
  if (note >= '0' && note <= '9' && armed_note) {
    digits_read[n_digits_read++] = note;
    if (n_digits_read == 3) {
      digits_read[3] = '\0';
      int val = atoi(digits_read);
      if (val >= 0 && val <= MIDI_MAX) {
        handle_keypad(MIDI_ON, armed_note, val);
      }
      n_digits_read = 0;
      armed_note = 0;
    }
    return;
  }

  armed_note = 0;
  n_digits_read = 0;

  if (note == DELETE || note == F8) {
    armed_note = note;
    return;
  }

  handle_keypad(MIDI_ON, note, 64);
}

// ---------------------------------------------------------------------------
// Lit state
// ---------------------------------------------------------------------------

static bool ep_flag(int ep, int flag) {
  switch (flag) {
  case FLAG_DOWNBEAT:    return c->downbeat[ep];
  case FLAG_UPBEAT:      return c->upbeat[ep];
  case FLAG_UPBEAT_HIGH: return c->upbeat_high[ep];
  case FLAG_DOUBLED:     return c->doubled[ep];
  case FLAG_SHORTISH:    return c->shortish[ep];
  case FLAG_SHORTER:     return c->shorter[ep];
  case FLAG_PRE_UNIQUE:  return c->pre_unique[ep];
  case FLAG_CHORD:       return c->chord[ep];
  case FLAG_VEL:         return c->vel[ep];
  case FLAG_PAN:         return c->pans[ep];
  case FLAG_DUCKED:      return c->ducked[ep];
  case FLAG_AIR_LOCKED:  return c->air_lockeds[ep];
  case FLAG_FOLLOWS_AIR: return c->follows_air[ep];
  }
  return false;
}

static bool global_flag(int flag) {
  switch (flag) {
  case GLOBAL_JIG:                return jig_time;
  case GLOBAL_DRUM_CHOOSES:       return drum_chooses_notes;
  case GLOBAL_DRUM_CHOOSES_SOME:  return drum_chooses_some_notes;
  case GLOBAL_ALL_DRUMS_DOWNBEAT: return allow_all_drums_downbeat;
  case GLOBAL_FADED:              return fade_target == 0;
  }
  return false;
}

// Caller must hold the lock.
static bool key_is_lit(const Key* key) {
  int sel = c->selected_endpoint;
  bool drums_selected = (sel == ENDPOINT_DRUM);

  LitKind lit = key->lit;
  int arg = key->arg;
  if (drums_selected && key->drum_label) {
    lit = key->drum_lit;
    arg = key->drum_arg;
  }

  switch (lit) {
  case LIT_SEL_EP:      return sel == arg;
  case LIT_EP_ON:       return c->on[arg];
  case LIT_VOICE:       return !drums_selected && c->voices[sel] == arg;
  case LIT_DRUM_VOICE:  return drums_selected && c->drum_voice == arg;
  case LIT_EP_FLAG:     return ep_flag(sel, arg);
  case LIT_GLOBAL_FLAG: return global_flag(arg);
  case LIT_MODE:        return musical_mode == arg;
  case LIT_ARMED:       return armed_note == arg;
  case LIT_OCTAVE:      return c->octave_deltas[sel] * arg > 0;
  case LIT_VOLUME:      return c->volume_deltas[sel] * arg > 0;
  case LIT_DIGIT:       return armed_note != 0;
  case LIT_NEVER:       break;
  }
  return false;
}

#endif
