#ifndef JML_KEY_LAYOUT_H
#define JML_KEY_LAYOUT_H

// The on-screen keyboard: one entry per physical key, giving its position, the
// pseudo-note handle_keypad() expects for it, a short label for what it does,
// and how to decide whether it should be lit.
//
// The pseudo-note values match what kbd.py sends on the Pi, so handle_keypad()
// in jammermidilib.h is shared unchanged.
//
// Include after jammermidilib.h.

#include <Carbon/Carbon.h>  // kVK_* virtual keycodes

// How a key's lit state is computed.
typedef enum {
  LIT_NEVER,        // momentary; only flashes when struck
  LIT_SEL_EP,       // selected_endpoint == arg
  LIT_EP_ON,        // c->on[arg]
  LIT_VOICE,        // c->voices[selected_endpoint] == arg
  LIT_DRUM_VOICE,   // drum selected and c->drum_voice == arg
  LIT_EP_FLAG,      // per-selected-endpoint bool, arg = FLAG_*
  LIT_GLOBAL_FLAG,  // arg = GLOBAL_*
  LIT_MODE,         // musical_mode == arg
  LIT_ARMED,        // waiting on 3 digits for arg (DELETE or F8)
  LIT_OCTAVE,       // octave_deltas[selected] is nonzero, sign matches arg
  LIT_VOLUME,       // volume_deltas[selected] is nonzero, sign matches arg
  LIT_DIGIT,        // lit while a digit entry is in progress
} LitKind;

// Per-endpoint flags in struct Configuration.
enum {
  FLAG_DOWNBEAT, FLAG_UPBEAT, FLAG_UPBEAT_HIGH, FLAG_DOUBLED,
  FLAG_SHORTISH, FLAG_SHORTER, FLAG_PRE_UNIQUE, FLAG_CHORD, FLAG_VEL,
  FLAG_PAN, FLAG_DUCKED, FLAG_AIR_LOCKED, FLAG_FOLLOWS_AIR,
};

// Globals in jammermidilib.h.
enum {
  GLOBAL_JIG, GLOBAL_DRUM_CHOOSES, GLOBAL_DRUM_CHOOSES_SOME,
  GLOBAL_ALL_DRUMS_DOWNBEAT, GLOBAL_FADED,
};

// Colour families, so related keys read as a group.
typedef enum {
  GROUP_NONE,      // unbound filler key
  GROUP_SELECT,    // pick which endpoint the modifier keys apply to
  GROUP_TOGGLE,    // turn an endpoint on/off
  GROUP_VOICE,     // pick the instrument for the selected endpoint
  GROUP_MODIFIER,  // per-endpoint behaviour flags
  GROUP_GLOBAL,    // whole-rig settings
} KeyGroup;

typedef struct {
  int vk;             // macOS virtual keycode, or -1 for filler
  int note;           // what handle_keypad() receives
  const char* cap;    // the keyboard letter, printed small in the corner
  const char* label;  // abbreviation for what it does; "\n" splits lines
  KeyGroup group;
  LitKind lit;
  int arg;
  // When the drum endpoint is selected, A-G pick drum sounds instead of
  // voices.  NULL label means "no different when drums are selected".
  const char* drum_label;
  LitKind drum_lit;
  int drum_arg;
  double row;         // 0 = function row, increasing downwards
  double x;           // left edge, in key units
  double w;           // width, in key units
  double h;           // height in rows; 0 means a normal one-row key
} Key;

#define NOLABEL NULL, LIT_NEVER, 0

static const Key KEYS[] = {
  // ---- function row -------------------------------------------------------
  {kVK_Escape, ESCAPE, "esc", "RESET",  GROUP_GLOBAL, LIT_NEVER, 0,
   NOLABEL, 0, 0, 1.5},
  {kVK_F1, F1, "F1", "CLEAR\nENDPT", GROUP_MODIFIER, LIT_NEVER, 0,
   NOLABEL, 0, 2, 1},
  {kVK_F2, F2, "F2", "PAN", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_PAN,
   NOLABEL, 0, 3, 1},
  {kVK_F3, 0, "F3", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 0, 4, 1},
  {kVK_F4, F4, "F4", "DUCK", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_DUCKED,
   NOLABEL, 0, 5, 1},
  {kVK_F5, F5, "F5", "DRUM\nSOME", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_DRUM_CHOOSES_SOME, NOLABEL, 0, 6, 1},
  {kVK_F6, F6, "F6", "AIR\nLOCK", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_AIR_LOCKED,
   NOLABEL, 0, 7, 1},
  {kVK_F7, F7, "F7", "FOLLOW\nAIR", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_FOLLOWS_AIR, NOLABEL, 0, 8, 1},
  {kVK_F8, F8, "F8", "ROOT\nnnn", GROUP_GLOBAL, LIT_ARMED, F8,
   NOLABEL, 0, 9, 1},
  {kVK_F9, F9, "F9", "DRUM\nPICKS", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_DRUM_CHOOSES, NOLABEL, 0, 10, 1},
  {kVK_F10, F10, "F10", "ALL\nDOWN", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_ALL_DRUMS_DOWNBEAT, NOLABEL, 0, 11, 1},

  // ---- number row: select which endpoint the modifiers act on -------------
  {kVK_ANSI_Grave, '`', "`", "Drum", GROUP_SELECT, LIT_SEL_EP, ENDPOINT_DRUM,
   NOLABEL, 1, 0, 1},
  {kVK_ANSI_1, '1', "1", "Jaw\nharp", GROUP_SELECT, LIT_SEL_EP,
   ENDPOINT_JAWHARP, NOLABEL, 1, 1, 1},
  {kVK_ANSI_2, '2', "2", "Foot\nBass", GROUP_SELECT, LIT_SEL_EP,
   ENDPOINT_FOOTBASS, NOLABEL, 1, 2, 1},
  {kVK_ANSI_3, '3', "3", "Arp", GROUP_SELECT, LIT_SEL_EP, ENDPOINT_ARP,
   NOLABEL, 1, 3, 1},
  {kVK_ANSI_4, '4', "4", "Flex", GROUP_SELECT, LIT_SEL_EP, ENDPOINT_FLEX,
   NOLABEL, 1, 4, 1},
  {kVK_ANSI_5, '5', "5", "Low", GROUP_SELECT, LIT_SEL_EP, ENDPOINT_LOW,
   NOLABEL, 1, 5, 1},
  {kVK_ANSI_6, '6', "6", "Hi", GROUP_SELECT, LIT_SEL_EP, ENDPOINT_HI,
   NOLABEL, 1, 6, 1},
  {kVK_ANSI_7, '7', "7", "Over\nlay", GROUP_SELECT, LIT_SEL_EP,
   ENDPOINT_OVERLAY, NOLABEL, 1, 7, 1},
  {kVK_ANSI_8, '8', "8", "Drone\nBass", GROUP_SELECT, LIT_SEL_EP,
   ENDPOINT_DRONE_BASS, NOLABEL, 1, 8, 1},
  {kVK_ANSI_9, '9', "9", "Drone\nChord", GROUP_SELECT, LIT_SEL_EP,
   ENDPOINT_DRONE_CHORD, NOLABEL, 1, 9, 1},
  {kVK_ANSI_0, '0', "0", "JIG", GROUP_GLOBAL, LIT_GLOBAL_FLAG, GLOBAL_JIG,
   NOLABEL, 1, 10, 1},
  {kVK_ANSI_Minus, '-', "-", "VOL−", GROUP_MODIFIER, LIT_VOLUME, -1,
   NOLABEL, 1, 11, 1},
  {kVK_ANSI_Equal, '=', "=", "VOL+", GROUP_MODIFIER, LIT_VOLUME, 1,
   NOLABEL, 1, 12, 1},
  {kVK_Delete, DELETE, "del", "VOL\nnnn", GROUP_MODIFIER, LIT_ARMED, DELETE,
   NOLABEL, 1, 13, 2},

  // ---- qwerty row: turn endpoints on and off ------------------------------
  {kVK_Tab, TAB, "tab", "Drum", GROUP_TOGGLE, LIT_EP_ON, ENDPOINT_DRUM,
   NOLABEL, 2, 0, 1.5},
  {kVK_ANSI_Q, 'Q', "Q", "Jaw\nharp", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_JAWHARP, NOLABEL, 2, 1.5, 1},
  {kVK_ANSI_W, 'W', "W", "Foot\nBass", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_FOOTBASS, NOLABEL, 2, 2.5, 1},
  {kVK_ANSI_E, 'E', "E", "Arp", GROUP_TOGGLE, LIT_EP_ON, ENDPOINT_ARP,
   NOLABEL, 2, 3.5, 1},
  {kVK_ANSI_R, 'R', "R", "Flex", GROUP_TOGGLE, LIT_EP_ON, ENDPOINT_FLEX,
   NOLABEL, 2, 4.5, 1},
  {kVK_ANSI_T, 'T', "T", "Low", GROUP_TOGGLE, LIT_EP_ON, ENDPOINT_LOW,
   NOLABEL, 2, 5.5, 1},
  {kVK_ANSI_Y, 'Y', "Y", "Hi", GROUP_TOGGLE, LIT_EP_ON, ENDPOINT_HI,
   NOLABEL, 2, 6.5, 1},
  {kVK_ANSI_U, 'U', "U", "Over\nlay", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_OVERLAY, NOLABEL, 2, 7.5, 1},
  {kVK_ANSI_I, 'I', "I", "Drone\nBass", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_DRONE_BASS, NOLABEL, 2, 8.5, 1},
  {kVK_ANSI_O, 'O', "O", "Drone\nChord", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_DRONE_CHORD, NOLABEL, 2, 9.5, 1},
  {kVK_ANSI_P, 'P', "P", "DOUB\nLED", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_DOUBLED, NOLABEL, 2, 10.5, 1},
  {kVK_ANSI_LeftBracket, '[', "[", "PRE\nUNIQ", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_PRE_UNIQUE, NOLABEL, 2, 11.5, 1},
  {kVK_ANSI_RightBracket, ']', "]", "OCT+", GROUP_MODIFIER, LIT_OCTAVE, 1,
   NOLABEL, 2, 12.5, 1},
  {kVK_ANSI_Backslash, '\\', "\\", "OCT−", GROUP_MODIFIER, LIT_OCTAVE, -1,
   NOLABEL, 2, 13.5, 1.5},

  // ---- home row: voices, then note-shape modifiers ------------------------
  {-1, 0, "caps", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 3, 0, 1.75},
  {kVK_ANSI_A, 'A', "A", "SynBass\n2", GROUP_VOICE, LIT_VOICE, 39,
   "Rim", LIT_DRUM_VOICE, KIT_RIM, 3, 1.75, 1},
  {kVK_ANSI_S, 'S', "S", "SynBass\n1", GROUP_VOICE, LIT_VOICE, 38,
   "Rim 2", LIT_DRUM_VOICE, KIT_RIM2, 3, 2.75, 1},
  {kVK_ANSI_D, 'D', "D", "Acou\nBass", GROUP_VOICE, LIT_VOICE, 32,
   "Snare", LIT_DRUM_VOICE, KIT_SNARE, 3, 3.75, 1},
  {kVK_ANSI_F, 'F', "F", "Draw\nbar", GROUP_VOICE, LIT_VOICE, 16,
   "Clap", LIT_DRUM_VOICE, KIT_CLAP, 3, 4.75, 1},
  {kVK_ANSI_G, 'G', "G", "Fret\nless", GROUP_VOICE, LIT_VOICE, 35,
   "E.Snare", LIT_DRUM_VOICE, KIT_ESNARE, 3, 5.75, 1},
  {kVK_ANSI_H, 'H', "H", "Rock\nOrgan", GROUP_VOICE, LIT_VOICE, 18,
   NOLABEL, 3, 6.75, 1},
  {kVK_ANSI_J, 'J', "J", "DOWN\nBEAT", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_DOWNBEAT, NOLABEL, 3, 7.75, 1},
  {kVK_ANSI_K, 'K', "K", "UP\nBEAT", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_UPBEAT,
   NOLABEL, 3, 8.75, 1},
  {kVK_ANSI_L, 'L', "L", "UP\nHIGH", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_UPBEAT_HIGH, NOLABEL, 3, 9.75, 1},
  {kVK_ANSI_Semicolon, ';', ";", "SHORT\nISH", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_SHORTISH, NOLABEL, 3, 10.75, 1},
  {kVK_ANSI_Quote, '\'', "'", "SHORT\nER", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_SHORTER, NOLABEL, 3, 11.75, 1},
  {-1, 0, "return", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 3, 12.75, 2.25},

  // ---- bottom row: more voices, then note modifiers -----------------------
  {-1, 0, "shift", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 4, 0, 2.25},
  {kVK_ANSI_Z, 'Z', "Z", "Pan\nFlute", GROUP_VOICE, LIT_VOICE, 75,
   NOLABEL, 4, 2.25, 1},
  {kVK_ANSI_X, 'X', "X", "Vox\nLead", GROUP_VOICE, LIT_VOICE, 85,
   NOLABEL, 4, 3.25, 1},
  {kVK_ANSI_C, 'C', "C", "E.\nPiano", GROUP_VOICE, LIT_VOICE, 4,
   NOLABEL, 4, 4.25, 1},
  {kVK_ANSI_V, 'V', "V", "Bari\nSax", GROUP_VOICE, LIT_VOICE, 67,
   NOLABEL, 4, 5.25, 1},
  {kVK_ANSI_B, 'B', "B", "Saw\nLead", GROUP_VOICE, LIT_VOICE, 81,
   NOLABEL, 4, 6.25, 1},
  {kVK_ANSI_N, 'N', "N", "Bass\nLead", GROUP_VOICE, LIT_VOICE, 87,
   NOLABEL, 4, 7.25, 1},
  {kVK_ANSI_M, 'M', "M", "Tub\nBells", GROUP_VOICE, LIT_VOICE, 15,
   NOLABEL, 4, 8.25, 1},
  {kVK_ANSI_Comma, ',', ",", "CHORD", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_CHORD,
   NOLABEL, 4, 9.25, 1},
  {kVK_ANSI_Period, '.', ".", "VEL", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_VEL,
   NOLABEL, 4, 10.25, 1},
  {kVK_ANSI_Slash, '/', "/", "FADE\nOUT", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_FADED, NOLABEL, 4, 11.25, 1},
  {-1, 0, "shift", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 4, 12.25, 2.75},

  // ---- bottom row: unused modifiers, then the arrows pick the mode --------
  {-1, 0, "fn", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 0, 1},
  {-1, 0, "ctrl", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 1, 1},
  {-1, 0, "opt", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 2, 1},
  {-1, 0, "cmd", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 3, 1.25},
  {-1, 0, "space", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 4.25, 5},
  {-1, 0, "cmd", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 9.25, 1.25},
  {-1, 0, "opt", NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL, 5, 10.5, 0.75},
  {kVK_LeftArrow, LEFT, "←", "MIXO", GROUP_GLOBAL, LIT_MODE, MODE_MIXO,
   NOLABEL, 5, 11.25, 1.25},
  {kVK_UpArrow, UP, "↑", "MAJOR", GROUP_GLOBAL, LIT_MODE, MODE_MAJOR,
   NOLABEL, 5, 12.5, 1.25, 0.5},
  {kVK_DownArrow, DOWN, "↓", "MINOR", GROUP_GLOBAL, LIT_MODE, MODE_MINOR,
   NOLABEL, 5.5, 12.5, 1.25, 0.5},
  {kVK_RightArrow, RIGHT, "→", "BETH", GROUP_GLOBAL, LIT_MODE,
   MODE_BETH_COHENS, NOLABEL, 5, 13.75, 1.25},

  // ---- digits again, for the 3-digit entry that F8 and del start ----------
  // (no separate keys; the number row doubles as the numeric entry)
};

#define N_KEYS ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

#define N_LAYOUT_COLS 15.0
#define N_LAYOUT_ROWS 6.0

static const char* ENDPOINT_NAMES[N_ENDPOINTS] = {
  "Jawharp",     // ENDPOINT_JAWHARP
  "Drone Bass",  // ENDPOINT_DRONE_BASS
  "Drone Chord", // ENDPOINT_DRONE_CHORD
  "Foot Bass",   // ENDPOINT_FOOTBASS
  "Arp",         // ENDPOINT_ARP
  "Flex",        // ENDPOINT_FLEX
  "Low",         // ENDPOINT_LOW
  "Hi",          // ENDPOINT_HI
  "Overlay",     // ENDPOINT_OVERLAY
  "Drum",        // ENDPOINT_DRUM
};

#endif
