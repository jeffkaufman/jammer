#ifndef JML_KEY_LAYOUT_H
#define JML_KEY_LAYOUT_H

// The on-screen keyboard: one entry per physical key, giving its position, the
// pseudo-note handle_keypad() expects for it, the labels to print on it, and
// how to decide whether it should be lit.
//
// Two labels: `shortname` is the abbreviation written on the paper tabs stuck
// to the physical keyboard, printed big so the screen and the keyboard match
// at a glance, and `label` spells out what it actually does.
//
// The pseudo-note values match what kbd.py sends on the Pi, so handle_keypad()
// in jammermidilib.h is shared unchanged.  Shift-selecting an endpoint is a
// Mac-only convenience, so instead of a new pseudo-note a toggle key carries
// the select pseudo-note the number row used to send.
//
// Include after jammermidilib.h.

#include <Carbon/Carbon.h>  // kVK_* virtual keycodes

// How a key's lit state is computed.
typedef enum {
  LIT_NEVER,        // momentary; only flashes when struck
  LIT_EP_ON,        // c->on[arg]
  LIT_VOICE,        // c->voices[selected_endpoint] == arg
  LIT_DRUM_VOICE,   // drum selected and c->drum_voice == arg
  LIT_EP_FLAG,      // per-selected-endpoint bool, arg = FLAG_*
  LIT_GLOBAL_FLAG,  // arg = GLOBAL_*
  LIT_MODE,         // musical_mode == arg
  LIT_OCTAVE,       // octave_deltas[selected] is nonzero, sign matches arg
  LIT_VOLUME,       // volume_deltas[selected] is nonzero, sign matches arg
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
  GROUP_TOGGLE,    // turn an endpoint on/off (shift: select it)
  GROUP_VOICE,     // pick the instrument for the selected endpoint
  GROUP_MODIFIER,  // per-endpoint behaviour flags
  GROUP_GLOBAL,    // whole-rig settings
} KeyGroup;

typedef struct {
  int vk;             // macOS virtual keycode, or -1 for filler
  int note;           // what handle_keypad() receives
  int select_note;    // what shift+key sends instead; 0 if shift does nothing
  const char* cap;    // the keyboard letter, printed small in the corner
  const char* shortname;  // the paper tab on the real keyboard, or NULL
  const char* label;  // what it does, spelled out; "\n" splits lines
  KeyGroup group;
  LitKind lit;
  int arg;
  // When the drum endpoint is selected, the voice keys pick drum kits
  // instead.  NULL label means "no different when drums are selected".
  const char* drum_label;
  LitKind drum_lit;
  int drum_arg;
  double row;         // 0 = function row, increasing downwards
  double x;           // left edge, in key units
  double w;           // width, in key units
  double h;           // height in rows; 0 means a normal one-row key
} Key;

#define NOLABEL NULL, LIT_NEVER, 0
// A voice key that does nothing while the drum is selected.  Distinct from
// NOLABEL, which means "this key is the same whatever is selected".
#define BLANK_ON_DRUM "", LIT_NEVER, 0
#define FILLER NULL, NULL, GROUP_NONE, LIT_NEVER, 0, NOLABEL

static const Key KEYS[] = {
  // ---- function row -------------------------------------------------------
  {kVK_Escape, ESCAPE, 0, "esc", "FR", "RESET", GROUP_GLOBAL, LIT_NEVER, 0,
   NOLABEL, 0, 0, 1.5},
  {kVK_F1, F1, 0, "F1", "ER", "CLEAR\nENDPT", GROUP_MODIFIER, LIT_NEVER, 0,
   NOLABEL, 0, 2, 1},
  {kVK_F2, F2, 0, "F2", "CH", "PAN", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_PAN,
   NOLABEL, 0, 3, 1},
  {-1, 0, 0, "F3", FILLER, 0, 4, 1},
  {kVK_F4, F4, 0, "F4", "P", "DUCK", GROUP_MODIFIER, LIT_EP_FLAG, FLAG_DUCKED,
   NOLABEL, 0, 5, 1},
  {kVK_F5, F5, 0, "F5", "DS", "DRUM\nSOME", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_DRUM_CHOOSES_SOME, NOLABEL, 0, 6, 1},
  {kVK_F6, F6, 0, "F6", "AL", "AIR\nLOCK", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_AIR_LOCKED, NOLABEL, 0, 7, 1},
  {kVK_F7, F7, 0, "F7", "AF", "FOLLOW\nAIR", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_FOLLOWS_AIR, NOLABEL, 0, 8, 1},
  // F8 used to arm three-digit root-note entry; the status bar's note picker
  // does that job now.
  {-1, 0, 0, "F8", FILLER, 0, 9, 1},
  {kVK_F9, F9, 0, "F9", "DCN", "DRUM\nPICKS", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_DRUM_CHOOSES, NOLABEL, 0, 10, 1},
  {kVK_F10, F10, 0, "F10", "AADD", "ALL\nDOWN", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_ALL_DRUMS_DOWNBEAT, NOLABEL, 0, 11, 1},

  // ---- number row ---------------------------------------------------------
  // ` and 1-9 used to select which endpoint the modifiers act on; that's
  // shift + the endpoint's toggle key now.
  {-1, 0, 0, "`", FILLER, 1, 0, 1},
  {-1, 0, 0, "1", FILLER, 1, 1, 1},
  {-1, 0, 0, "2", FILLER, 1, 2, 1},
  {-1, 0, 0, "3", FILLER, 1, 3, 1},
  {-1, 0, 0, "4", FILLER, 1, 4, 1},
  {-1, 0, 0, "5", FILLER, 1, 5, 1},
  {-1, 0, 0, "6", FILLER, 1, 6, 1},
  {-1, 0, 0, "7", FILLER, 1, 7, 1},
  {-1, 0, 0, "8", FILLER, 1, 8, 1},
  {-1, 0, 0, "9", FILLER, 1, 9, 1},
  {kVK_ANSI_0, '0', 0, "0", "J/R", "JIG", GROUP_GLOBAL, LIT_GLOBAL_FLAG,
   GLOBAL_JIG, NOLABEL, 1, 10, 1},
  {kVK_ANSI_Minus, '-', 0, "-", "-", "VOL−", GROUP_MODIFIER, LIT_VOLUME,
   -1, NOLABEL, 1, 11, 1},
  {kVK_ANSI_Equal, '=', 0, "=", "+", "VOL+", GROUP_MODIFIER, LIT_VOLUME, 1,
   NOLABEL, 1, 12, 1},
  // delete used to arm three-digit manual volume entry.
  {-1, 0, 0, "del", FILLER, 1, 13, 2},

  // ---- qwerty row: turn endpoints on and off, shift to select one ---------
  {kVK_Tab, TAB, '`', "tab", "d", "Drum", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_DRUM, NOLABEL, 2, 0, 1.5},
  {kVK_ANSI_Q, 'Q', '1', "Q", "JH", "Jaw\nharp", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_JAWHARP, NOLABEL, 2, 1.5, 1},
  {kVK_ANSI_W, 'W', '2', "W", "FB", "Foot\nBass", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_FOOTBASS, NOLABEL, 2, 2.5, 1},
  {kVK_ANSI_E, 'E', '3', "E", "ARP", "Arp", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_ARP, NOLABEL, 2, 3.5, 1},
  {kVK_ANSI_R, 'R', '4', "R", "FX", "Flex", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_FLEX, NOLABEL, 2, 4.5, 1},
  {kVK_ANSI_T, 'T', '5', "T", "L", "Low", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_LOW, NOLABEL, 2, 5.5, 1},
  {kVK_ANSI_Y, 'Y', '6', "Y", "H", "Hi", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_HI, NOLABEL, 2, 6.5, 1},
  {kVK_ANSI_U, 'U', '7', "U", "Ov", "Over\nlay", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_OVERLAY, NOLABEL, 2, 7.5, 1},
  {kVK_ANSI_I, 'I', '8', "I", "Db", "Drone\nBass", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_DRONE_BASS, NOLABEL, 2, 8.5, 1},
  {kVK_ANSI_O, 'O', '9', "O", "Dc", "Drone\nChord", GROUP_TOGGLE, LIT_EP_ON,
   ENDPOINT_DRONE_CHORD, NOLABEL, 2, 9.5, 1},
  {kVK_ANSI_P, 'P', 0, "P", "II", "DOUB\nLED", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_DOUBLED, NOLABEL, 2, 10.5, 1},
  {kVK_ANSI_LeftBracket, '[', 0, "[", "Q", "PRE\nUNIQ", GROUP_MODIFIER,
   LIT_EP_FLAG, FLAG_PRE_UNIQUE, NOLABEL, 2, 11.5, 1},
  {kVK_ANSI_RightBracket, ']', 0, "]", "↑", "OCT+", GROUP_MODIFIER,
   LIT_OCTAVE, 1, NOLABEL, 2, 12.5, 1},
  {kVK_ANSI_Backslash, '\\', 0, "\\", "↓", "OCT−", GROUP_MODIFIER,
   LIT_OCTAVE, -1, NOLABEL, 2, 13.5, 1.5},

  // ---- home row: voices, then note-shape modifiers ------------------------
  {-1, 0, 0, "caps", FILLER, 3, 0, 1.75},
  {kVK_ANSI_A, 'A', 0, "A", NULL, "SynBass\n2", GROUP_VOICE, LIT_VOICE, 39,
   "Rim", LIT_DRUM_VOICE, KIT_RIM, 3, 1.75, 1},
  {kVK_ANSI_S, 'S', 0, "S", NULL, "SynBass\n1", GROUP_VOICE, LIT_VOICE, 38,
   BLANK_ON_DRUM, 3, 2.75, 1},
  {kVK_ANSI_D, 'D', 0, "D", NULL, "Acou\nBass", GROUP_VOICE, LIT_VOICE, 32,
   BLANK_ON_DRUM, 3, 3.75, 1},
  {kVK_ANSI_F, 'F', 0, "F", NULL, "Draw\nbar", GROUP_VOICE, LIT_VOICE, 16,
   BLANK_ON_DRUM, 3, 4.75, 1},
  {kVK_ANSI_G, 'G', 0, "G", NULL, "Fret\nless", GROUP_VOICE, LIT_VOICE, 35,
   BLANK_ON_DRUM, 3, 5.75, 1},
  {kVK_ANSI_H, 'H', 0, "H", NULL, "Rock\nOrgan", GROUP_VOICE, LIT_VOICE, 18,
   BLANK_ON_DRUM, 3, 6.75, 1},
  {kVK_ANSI_J, 'J', 0, "J", "DB", "DOWN\nBEAT", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_DOWNBEAT, NOLABEL, 3, 7.75, 1},
  {kVK_ANSI_K, 'K', 0, "K", "UB", "UP\nBEAT", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_UPBEAT, NOLABEL, 3, 8.75, 1},
  {kVK_ANSI_L, 'L', 0, "L", "UH", "UP\nHIGH", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_UPBEAT_HIGH, NOLABEL, 3, 9.75, 1},
  {kVK_ANSI_Semicolon, ';', 0, ";", "S", "SHORT\nISH", GROUP_MODIFIER,
   LIT_EP_FLAG, FLAG_SHORTISH, NOLABEL, 3, 10.75, 1},
  {kVK_ANSI_Quote, '\'', 0, "'", "SS", "SHORT\nER", GROUP_MODIFIER,
   LIT_EP_FLAG, FLAG_SHORTER, NOLABEL, 3, 11.75, 1},
  {-1, 0, 0, "return", FILLER, 3, 12.75, 2.25},

  // ---- bottom row: more voices, then note modifiers -----------------------
  {-1, 0, 0, "shift", FILLER, 4, 0, 2.25},
  {kVK_ANSI_Z, 'Z', 0, "Z", NULL, "Pan\nFlute", GROUP_VOICE, LIT_VOICE, 75,
   "808 A", LIT_DRUM_VOICE, KIT_808_A, 4, 2.25, 1},
  {kVK_ANSI_X, 'X', 0, "X", NULL, "Vox\nLead", GROUP_VOICE, LIT_VOICE, 85,
   "808 B", LIT_DRUM_VOICE, KIT_808_B, 4, 3.25, 1},
  {kVK_ANSI_C, 'C', 0, "C", NULL, "E.\nPiano", GROUP_VOICE, LIT_VOICE, 4,
   "Room 2", LIT_DRUM_VOICE, KIT_ROOM2, 4, 4.25, 1},
  {kVK_ANSI_V, 'V', 0, "V", NULL, "Bari\nSax", GROUP_VOICE, LIT_VOICE, 67,
   "Room 6", LIT_DRUM_VOICE, KIT_ROOM6, 4, 5.25, 1},
  {kVK_ANSI_B, 'B', 0, "B", NULL, "Saw\nLead", GROUP_VOICE, LIT_VOICE, 81,
   BLANK_ON_DRUM, 4, 6.25, 1},
  {kVK_ANSI_N, 'N', 0, "N", NULL, "Bass\nLead", GROUP_VOICE, LIT_VOICE, 87,
   BLANK_ON_DRUM, 4, 7.25, 1},
  {kVK_ANSI_M, 'M', 0, "M", NULL, "Tub\nBells", GROUP_VOICE, LIT_VOICE, 15,
   BLANK_ON_DRUM, 4, 8.25, 1},
  {kVK_ANSI_Comma, ',', 0, ",", "C", "CHORD", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_CHORD, NOLABEL, 4, 9.25, 1},
  {kVK_ANSI_Period, '.', 0, ".", "V", "VEL", GROUP_MODIFIER, LIT_EP_FLAG,
   FLAG_VEL, NOLABEL, 4, 10.25, 1},
  {kVK_ANSI_Slash, '/', 0, "/", "f", "FADE\nOUT", GROUP_GLOBAL,
   LIT_GLOBAL_FLAG, GLOBAL_FADED, NOLABEL, 4, 11.25, 1},
  {-1, 0, 0, "shift", FILLER, 4, 12.25, 2.75},

  // ---- bottom row: unused modifiers, then the arrows pick the mode --------
  {-1, 0, 0, "fn", FILLER, 5, 0, 1},
  {-1, 0, 0, "ctrl", FILLER, 5, 1, 1},
  {-1, 0, 0, "opt", FILLER, 5, 2, 1},
  {-1, 0, 0, "cmd", FILLER, 5, 3, 1.25},
  {-1, 0, 0, "space", FILLER, 5, 4.25, 5},
  {-1, 0, 0, "cmd", FILLER, 5, 9.25, 1.25},
  {-1, 0, 0, "opt", FILLER, 5, 10.5, 0.75},
  {kVK_LeftArrow, LEFT, 0, "←", NULL, "MIXO", GROUP_GLOBAL, LIT_MODE,
   MODE_MIXO, NOLABEL, 5, 11.25, 1.25},
  {kVK_UpArrow, UP, 0, "↑", NULL, "MAJOR", GROUP_GLOBAL, LIT_MODE,
   MODE_MAJOR, NOLABEL, 5, 12.5, 1.25, 0.5},
  {kVK_DownArrow, DOWN, 0, "↓", NULL, "MINOR", GROUP_GLOBAL, LIT_MODE,
   MODE_MINOR, NOLABEL, 5.5, 12.5, 1.25, 0.5},
  // MODE_BETH_COHENS is the Freygish mode.
  {kVK_RightArrow, RIGHT, 0, "→", NULL, "FRAYG", GROUP_GLOBAL, LIT_MODE,
   MODE_BETH_COHENS, NOLABEL, 5, 13.75, 1.25},
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
