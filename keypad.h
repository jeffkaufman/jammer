#ifndef JML_KEYPAD_H
#define JML_KEYPAD_H

// Turning key presses into handle_keypad() calls, and working out which keys
// should be lit.  Kept apart from the Cocoa code so test-keypad.c can drive it
// directly.
//
// Include after jammermidilib.h, keylayout.h, whistle.h and speechwords.h.

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
  // Drum Some is on underneath speech choosing, but it's F3 that lights for
  // that: F5 lit means the feet are choosing.
  case GLOBAL_DRUM_CHOOSES_SOME:
    return drum_chooses_some_notes && !speech_chooses_notes;
  case GLOBAL_SPEECH_PICKS:       return speech_chooses_notes;
  case GLOBAL_SPEECH_COMMANDS:    return speech_commands_on;
  case GLOBAL_ALL_DRUMS_DOWNBEAT: return allow_all_drums_downbeat;
  case GLOBAL_FADED:              return fade_target == 0;
  case GLOBAL_KICK_DUCK:          return kick_duck;
  case GLOBAL_BASS_SWEEP:   return breath_fx & BREATH_FX_SWEEP_BASS;
  case GLOBAL_TREBLE_SWEEP: return breath_fx & BREATH_FX_SWEEP_TREBLE;
  case GLOBAL_PEAK_SWEEP:   return breath_fx & BREATH_FX_SWEEP_PEAK;
  case GLOBAL_VOICE_LEAD:   return voice_lead_on;
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
//   voice keys     the twelve in WHISTLE_VOICES; B does nothing
//   J K L          the vocoder and the vocal effects beside it (voicefx.h)
//   F2             which input the vocal effects hear, with a second one
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
           key->note != '-' && key->note != '=' &&
           !whistle_fx_for_note(key->note) &&
           !(key->note == F2 && whistle_has_second_mic());
  }
  return false;
}

// Handle a key press if the whistle owns it, and say whether it did.  A press
// it doesn't own goes on to handle_keypad() as it always has.
//
// Caller must hold the lock.
static bool whistle_key(const Key* key, bool selecting) {
  // Toggling selects, the same as it does for an endpoint in
  // toggle_endpoint(): whatever you just switched is what you'll want to set
  // up next.
  if (key->lit == LIT_WHISTLE_ON) {
    if (!selecting) whistle_toggle();
    whistle_select();
    return true;
  }

  // An endpoint's toggle key, shifted or not, selects that endpoint, which is
  // how you get back out of the whistle.  Let it through to do its own work.
  if (key->group == GROUP_TOGGLE) {
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
    int fx = whistle_fx_for_note(key->note);
    if (fx) whistle_set_fx(fx);
    if (key->note == F2) whistle_swap_fx_mic();
    return true;   // the rest are per-endpoint flags the whistle has no use for
  }

  return false;
}

// ---------------------------------------------------------------------------
// The drones
//
// With a drone selected the voice keys pick from DRONE_VOICES rather than the
// usual voices; handle_keypad does the picking, and these say what to draw.
// The whistle wins over both when it is selected, since then the keys are
// its whatever endpoint was selected last.
// ---------------------------------------------------------------------------

static bool drone_keys_active(void) {
  return !whistle_selected && is_drone(c->selected_endpoint);
}

// The Breath Gate's BREATH_VOICES entry on this key, or -1.  Caller must
// hold the lock.
static int breath_voice_on_key(const Key* key) {
  if (!drone_keys_active() || key->group != GROUP_VOICE ||
      c->selected_endpoint != ENDPOINT_BREATH) {
    return -1;
  }
  return breath_voice_for_note(key->note);
}

// The DRONE_VOICES entry this key picks right now, or -1 if it isn't picking
// one -- as on the Breath Gate's own voices, which win over the pads.
// Caller must hold the lock.
static int drone_voice_on_key(const Key* key) {
  if (!drone_keys_active() || key->group != GROUP_VOICE) return -1;
  if (breath_voice_on_key(key) >= 0) return -1;
  return drone_voice_for_note(key->note);
}

// A voice key with no pad on it does nothing while a drone is selected.
// Caller must hold the lock.
static bool drone_key_is_dead(const Key* key) {
  return drone_keys_active() && key->group == GROUP_VOICE && key->label &&
    drone_voice_for_note(key->note) < 0 && breath_voice_on_key(key) < 0;
}

// ---------------------------------------------------------------------------
// Flags an endpoint ignores
//
// The per-endpoint modifiers are stored for every endpoint, but each kind of
// endpoint only reads some of them:
//
//   foot basses, arp   all of them (arpeggiate_endpoint)
//   drum               all but CHORD and OCT, since psend_midi doesn't pass
//                      drum notes through endpoint_note (arpeggiate_drum)
//   jawharp, drones    no rhythm and no VEL: update_bass holds the note.
//                      The drones' II and Q are the trance gate, though
//                      (publish_music), and S and SS still mean the third
//                      and re-striking
//   flex, low, upper,  the piano plays them (handle_piano), so none of the
//   overlay            rhythm or length flags, and no CHORD (plays_chords);
//                      Flex is always full velocity
//
// Switching one of these on does nothing you could hear, so the key draws
// dead, the way the whistle's unused keys do.
// ---------------------------------------------------------------------------

static bool endpoint_uses_flag(int ep, int flag) {
  if (is_footbass(ep) || ep == ENDPOINT_ARP) return true;
  if (ep == ENDPOINT_DRUM) return flag != FLAG_CHORD;
  switch (flag) {
  case FLAG_CHORD:
    return plays_chords(ep);
  case FLAG_DOWNBEAT: case FLAG_UPBEAT: case FLAG_UPBEAT_HIGH:
    return false;
  case FLAG_DOUBLED: case FLAG_PRE_UNIQUE:
    return is_drone(ep);
  case FLAG_SHORTISH: case FLAG_SHORTER:
    return holds_bass_note(ep);
  case FLAG_VEL:
    return !holds_bass_note(ep) && ep != ENDPOINT_FLEX;
  }
  return true;  // the ones every channel has: CH, P, AL, AF
}

// True if this modifier does nothing for the selected endpoint.  The whistle
// has its own rules (whistle_key_is_dead).  Caller must hold the lock.
static bool endpoint_key_is_dead(const Key* key) {
  if (whistle_selected || !key->label || key->group != GROUP_MODIFIER) {
    return false;
  }
  int sel = c->selected_endpoint;
  if (key->lit == LIT_OCTAVE) return sel == ENDPOINT_DRUM;
  if (key->lit == LIT_EP_FLAG) return !endpoint_uses_flag(sel, key->arg);
  return false;
}

// Any reason for a key to draw dead right now.  Caller must hold the lock.
static bool key_is_dead(const Key* key) {
  return whistle_key_is_dead(key) || drone_key_is_dead(key) ||
    endpoint_key_is_dead(key);
}

// Strike a key, as a keypress would, minus the drawing.  Caller must hold the
// lock.
static void strike_key_locked(const Key* key, bool selecting) {
  int note = (selecting && key->select_note) ? key->select_note : key->note;
  // The whistle gets first refusal: while it is selected the voice, octave
  // and volume keys are its, and its own on/off key never reaches
  // handle_keypad at all.
  if (!whistle_key(key, selecting)) {
    // A reset is a reset.  The whistle's setup knobs are left alone -- see
    // whistle_reset.
    if (note == ESCAPE) whistle_reset();
    keypad_key(note);
  }
}

// ---------------------------------------------------------------------------
// Spoken names
//
// What "press ..." matches against (see speechwords.h): each key's label as
// it's drawn right now -- so the voice keys answer to the drum kits, the
// drones' pads or the whistle's voices when those are what's on them -- plus
// a spoken form for the labels that are abbreviations or symbols.
// ---------------------------------------------------------------------------

// The label a key shows right now, the way drawKey picks it, or NULL for a key
// that does nothing at the moment.  Caller must hold the lock.
static const char* key_current_label(const Key* key) {
  if (!key->label) return NULL;
  if (key_is_dead(key)) return NULL;
  // The whistle first: while it's selected the voice keys are its, whatever
  // endpoint was selected before it.
  if (whistle_selected && key->group == GROUP_VOICE) {
    return whistle_voice_label(whistle_voice_for_note(key->note));
  }
  if (whistle_selected && whistle_fx_for_note(key->note) &&
      key->group == GROUP_MODIFIER) {
    return WHISTLE_FX[whistle_fx_for_note(key->note)].label;
  }
  if (whistle_selected && key->note == F2) return whistle_fx_mic_label();
  int drone = drone_voice_on_key(key);
  if (drone >= 0) return DRONE_VOICES[drone].label;
  int breath_voice = breath_voice_on_key(key);
  if (breath_voice >= 0) return BREATH_VOICES[breath_voice].label;
  if (c->selected_endpoint == ENDPOINT_DRUM && key->drum_label) {
    if (key->drum_label[0] == '\0') return NULL;  // blank with the drum
    return key->drum_label;
  }
  return key->label;
}

// Labels that don't say themselves: what to call them as well -- or instead,
// for the ones whose label is mostly a symbol and says nothing once the
// symbol's gone ("VOL−" and "VOL+" would both be "vol").
//
// Also for labels cut short to fit on the key: the name to say is the whole
// thing, and the part that fits isn't a name of its own.
//
// And for labels split mid-word to fit, like "Arpeg\ngiator", so the
// dictionary the recognizer is given (all_spoken_phrases) has the word and
// not its halves.  Those match the same either way.
static const struct { const char* label; const char* spoken; bool instead; }
SPOKEN_ALIASES[] = {
  {"CLEAR\nENDPT", "clear endpoint"},
  {"SPEECH\nRECOG", "speech recognition", true},
  {"NUMBER\nRECOG", "number recognition", true},
  {"DRUM\nCHOOSES", "drum chooses notes", true},
  {"VOL−", "volume down", true},
  {"VOL+", "volume up", true},
  {"OCT−", "octave down", true},
  {"OCT+", "octave up", true},
  {"PRE\nUNIQ", "pre unique"},
  {"SynBass\n2", "synth bass 2"},
  {"SynBass\n1", "synth bass 1"},
  {"Acou\nBass", "acoustic bass"},
  {"E.\nPiano", "electric piano"},
  {"Bari\nSax", "baritone sax"},
  {"Tub\nBells", "tubular bells"},
  {"VEL", "velocity"},
  {"FRAYG", "freygish"},
  {"FM", "frequency modulator", true},
  {"Arpeg\ngiator", "arpeggiator"},
  {"Over\nlay", "overlay"},
  {"DOUB\nLED", "doubled"},
  {"SHORT\nISH", "shortish"},
  {"SHORT\nER", "shorter"},
  {"DOWN\nBEAT", "downbeat"},
  {"UP\nBEAT", "upbeat"},
  {"Draw\nbar", "drawbar"},
  {"Fret\nless", "fretless"},
  {"Poly\nsynth", "polysynth"},
  {"Octave\nless", "octaveless"},
  {"Accor\ndion", "accordion"},
  {"Wash\nboard", "washboard"},
  {"MIXO\nLYDIAN", "mixolydian"},
};

// A label as words to say: lower case, with the line breaks as spaces.
static void spoken_label(const char* label, char* out, int out_size) {
  int n = 0;
  for (const char* p = label; *p && n < out_size - 1; p++) {
    out[n++] = *p == '\n' ? ' ' : (char)tolower((unsigned char)*p);
  }
  out[n] = '\0';
}

// The phrases for one label: its aliases if it has any, since those are how
// it's actually said, else the label itself.  Appended to out, skipping any
// already there.
static int add_spoken_phrases(const char* label, char (*out)[48], int n,
                              int max) {
  char phrases[4][48];
  int count = 0;
  for (int a = 0; a < (int)(sizeof(SPOKEN_ALIASES) /
                            sizeof(SPOKEN_ALIASES[0])) && count < 4; a++) {
    if (strcmp(SPOKEN_ALIASES[a].label, label) != 0) continue;
    snprintf(phrases[count++], 48, "%s", SPOKEN_ALIASES[a].spoken);
  }
  if (count == 0) spoken_label(label, phrases[count++], 48);
  for (int i = 0; i < count && n < max; i++) {
    bool seen = false;
    for (int j = 0; j < n; j++) if (strcmp(out[j], phrases[i]) == 0) seen = true;
    if (!seen) snprintf(out[n++], 48, "%s", phrases[i]);
  }
  return n;
}

// What to call a key in feedback: the name it answers to right now, as said
// ("arpeggiator", "whistle bass"), not the letter on it.  Caller must hold
// the lock.
static void key_spoken_title(const Key* key, char* out, int out_size) {
  const char* label = key_current_label(key);
  if (!label) label = key->label ? key->label : key->cap;
  char phrases[4][48];
  int n = add_spoken_phrases(label, phrases, 0, 4);
  snprintf(out, out_size, "%s", n > 0 ? phrases[0] : key->cap);
}

// Every phrase any button answers to in any state -- its own label, the drum
// kits, the drones' pads, the whistle's voices -- for teaching the speech
// recognizer what to expect.  From the tables, not by trying each state, so
// it needs no lock.  Returns how many.
static int all_spoken_phrases(char (*out)[48], int max) {
  int n = 0;
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].label) n = add_spoken_phrases(KEYS[i].label, out, n, max);
    if (KEYS[i].drum_label && KEYS[i].drum_label[0]) {
      n = add_spoken_phrases(KEYS[i].drum_label, out, n, max);
    }
  }
  for (int i = 0; i < N_DRONE_VOICES; i++) {
    n = add_spoken_phrases(DRONE_VOICES[i].label, out, n, max);
  }
  for (int i = 0; i < N_BREATH_VOICES; i++) {
    n = add_spoken_phrases(BREATH_VOICES[i].label, out, n, max);
  }
  for (int i = 0; i < N_WHISTLE_VOICES; i++) {
    n = add_spoken_phrases(WHISTLE_VOICES[i].label, out, n, max);
  }
  for (int fx = VFX_VOCODER; fx < N_VFX; fx++) {
    n = add_spoken_phrases(WHISTLE_FX[fx].label, out, n, max);
  }
  return n;
}

// Every name any key answers to right now, normalized (sw_name), with the
// index into KEYS each belongs to.  Returns how many.  Caller must hold the
// lock.
static int key_spoken_names(char (*names)[SW_NAME_MAX], int* keys, int max) {
  int n = 0;
  for (int i = 0; i < N_KEYS && n < max; i++) {
    const char* label = key_current_label(&KEYS[i]);
    if (!label) continue;
    bool instead = false;
    for (int a = 0; a < (int)(sizeof(SPOKEN_ALIASES) /
                              sizeof(SPOKEN_ALIASES[0])) && n < max; a++) {
      if (strcmp(SPOKEN_ALIASES[a].label, label) != 0) continue;
      sw_name(SPOKEN_ALIASES[a].spoken, names[n], SW_NAME_MAX);
      keys[n++] = i;
      instead |= SPOKEN_ALIASES[a].instead;
    }
    if (instead || n >= max) continue;
    sw_name(label, names[n], SW_NAME_MAX);
    if (names[n][0]) keys[n++] = i;
  }
  return n;
}

// Whether shift-clicking this key means anything, so "select ..." can refuse
// the keys where it would just be a click.
static bool key_selects(const Key* key) {
  return key->select_note || key->lit == LIT_WHISTLE_ON;
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
    {
      int v = whistle_voice_for_note(key->note);
      if (v >= 0 && WHISTLE_VOICES[v].own == WHISTLE_BREATH_BLOW) {
        return whistle_blow_on;
      }
      return v >= 0 && v == whistle_voice && !whistle_voice_muted;
    }
    case GROUP_MODIFIER:
      // Only the two the whistle actually uses light; the per-endpoint flags
      // go dark, because what they would be reporting isn't on screen.
      if (whistle_fx_for_note(key->note)) {
        return whistle_fx == whistle_fx_for_note(key->note);
      }
      if (key->note == F2) return whistle_fx_mic;
      if (key->lit == LIT_OCTAVE) return whistle_octave * key->arg > 0;
      if (key->lit == LIT_VOLUME) {
        return (whistle_volume - WHISTLE_VOLUME_DEFAULT) * key->arg > 0;
      }
      return false;
    default:
      break;   // toggles and whole-rig keys mean what they always mean
    }
  }

  if (drone_keys_active() && key->group == GROUP_VOICE) {
    int breath_voice = breath_voice_on_key(key);
    if (breath_voice >= 0) {
      return c->breath_layers & BREATH_VOICES[breath_voice].layer;
    }
    int index = drone_voice_for_note(key->note);
    return index >= 0 && c->voices[sel] == DRONE_VOICES[index].program;
  }

  // A flag this endpoint ignores goes dark with the rest of its key, even if
  // it's set: on is only worth showing if it does something.
  if (endpoint_key_is_dead(key)) return false;

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
