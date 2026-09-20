#ifndef JML_KEYPAD_H
#define JML_KEYPAD_H

// Turning key presses into handle_keypad() calls, and working out which keys
// should be lit.  Kept apart from the Cocoa code so test-keypad.c can drive it
// directly.
//
// Include after jammermidilib.h, keylayout.h and whistle.h.

// ---------------------------------------------------------------------------
// Keypad: the Mac keyboard standing in for kbd.py
// ---------------------------------------------------------------------------

// kbd.py has a mode where DELETE or F8 collects three digits and sends them as
// a velocity.  The Mac app doesn't: the root note is picked from the status
// bar instead, and manual per-voice volumes aren't something worth typing
// blind.  So a key press is just a key press.
//
// Caller must hold the lock.
static void keypad_key(int note) {
  handle_keypad(MIDI_ON, note, 64);
}

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

// ---------------------------------------------------------------------------
// The whistle
//
// The whistle bass is an instrument on this keyboard but not an endpoint --
// endpoints are fluidsynth MIDI channels and it has none -- so selecting it
// is tracked separately, and while it is selected the keys that would act on
// an endpoint act on it instead.  Same shape as the drum override below it:
// one selection changes what a shared set of keys means.
//
// Which keys those are:
//
//   voice keys     the ten in WHISTLE_VOICES; the other three do nothing,
//                  exactly as they do nothing while the drum is selected
//   ] and \\        octave, within the engine's +/-3
//   - and =        the engine's own volume knob, 0-9
//
// Everything else per-endpoint is swallowed rather than passed through, since
// the endpoint it would act on is not the instrument you are looking at.
// Whole-rig keys -- the mode, the tempo, esc -- go through as usual.

// True if this key does nothing while the whistle is selected, so the drawing
// code can grey it out the way it greys out a voice key that no drum kit uses.
// Caller must hold the lock.
static bool whistle_key_is_dead(const Key* key) {
  if (!whistle_selected || !key->label) return false;
  if (key->group == GROUP_VOICE) {
    return whistle_voice_for_note(key->note) < 0;
  }
  if (key->group == GROUP_MODIFIER) {
    return key->note != ']' && key->note != '\\' &&
           key->note != '-' && key->note != '=';
  }
  return false;
}

// Handle a key press if the whistle owns it, and say whether it did.  A press
// it doesn't own goes on to handle_keypad() as it always has.
//
// Caller must hold the lock.
static bool whistle_key(const Key* key, bool selecting) {
  if (key->lit == LIT_WHISTLE_ON) {
    if (selecting) {
      whistle_select();
    } else {
      whistle_toggle();
    }
    return true;
  }

  // Shift on an endpoint's toggle key selects that endpoint, which is how you
  // get back out of the whistle.  Let it through to do its own work.
  if (selecting && key->select_note) {
    whistle_selected = false;
    return false;
  }

  if (!whistle_selected) return false;

  if (key->group == GROUP_VOICE) {
    // -1 for the three voice keys the whistle doesn't use.  Swallowed either
    // way: falling through would pick a melodic voice for whichever endpoint
    // happened to be selected last.
    whistle_set_voice(whistle_voice_for_note(key->note));
    return true;
  }

  if (key->group == GROUP_MODIFIER) {
    switch (key->note) {
    case ']':  whistle_bump_octave(1);  return true;
    case '\\': whistle_bump_octave(-1); return true;
    case '-':  whistle_bump_volume(-1); return true;
    case '=':  whistle_bump_volume(1);  return true;
    }
    return true;   // the rest are per-endpoint flags the whistle has no use for
  }

  return false;
}

// ---------------------------------------------------------------------------
// Lit state
// ---------------------------------------------------------------------------

// Caller must hold the lock.
static bool key_is_lit(const Key* key) {
  int sel = c->selected_endpoint;
  bool drums_selected = (sel == ENDPOINT_DRUM);

  if (key->lit == LIT_WHISTLE_ON) return whistle_on;

  if (whistle_selected) {
    switch (key->group) {
    case GROUP_VOICE:
      return whistle_voice_for_note(key->note) == whistle_voice;
    case GROUP_MODIFIER:
      // Only the two the whistle actually uses light; the per-endpoint flags
      // go dark, because what they would be reporting isn't on screen.
      if (key->lit == LIT_OCTAVE) return whistle_octave * key->arg > 0;
      if (key->lit == LIT_VOLUME) {
        return (whistle_volume - WHISTLE_VOLUME_DEFAULT) * key->arg > 0;
      }
      return false;
    default:
      break;   // toggles and whole-rig keys mean what they always mean
    }
  }

  LitKind lit = key->lit;
  int arg = key->arg;
  if (drums_selected && key->drum_label) {
    lit = key->drum_lit;
    arg = key->drum_arg;
  }

  switch (lit) {
  case LIT_EP_ON:       return c->on[arg];
  case LIT_VOICE:       return !drums_selected && c->voices[sel] == arg;
  case LIT_DRUM_VOICE:  return drums_selected && c->drum_voice == arg;
  case LIT_EP_FLAG:     return ep_flag(sel, arg);
  case LIT_GLOBAL_FLAG: return global_flag(arg);
  case LIT_MODE:        return musical_mode == arg;
  case LIT_OCTAVE:      return c->octave_deltas[sel] * arg > 0;
  case LIT_VOLUME:      return c->volume_deltas[sel] * arg > 0;
  case LIT_WHISTLE_ON:  // handled above, before the whistle override
  case LIT_NEVER:       break;
  }
  return false;
}

// The key whose endpoint the modifier keys currently act on gets a distinct
// outline, whether or not that endpoint is switched on.
//
// Caller must hold the lock.
static bool key_is_selected_endpoint(const Key* key) {
  if (key->lit == LIT_WHISTLE_ON) return whistle_selected;
  return !whistle_selected && key->lit == LIT_EP_ON && key->label &&
    key->arg == c->selected_endpoint;
}

#endif
