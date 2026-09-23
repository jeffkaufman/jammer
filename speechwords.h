#ifndef JML_SPEECH_WORDS_H
#define JML_SPEECH_WORDS_H

// What the speech recognizer's words mean.  With number recognition on (F3):
//
//   four                  a bare Nashville number, 1-7, picks a chord
//
// and with speech recognition on (F8):
//
//   press foot bass       strikes that button, as if clicked
//   select foot bass      ... or shift-clicked
//   change key to B       as picking it from the key at the top left
//   change mode to minor  as the arrow keys
//
// Only "change", not "set": "set" sounds too much like "seven" to the fast
// number recognizer (numrec.h).
//
// Plain C, apart from speech.h, so test-keypad.c can reach it.
//
// The lead-in words are what keep talking to the room from doing anything: a
// button's name, a key or a mode on its own does nothing.

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define SW_NAME_MAX 32

// A word as it's compared: lower case, letters and digits only, and number
// words as digits, so "Two", "two," and "2" all come out "2" and "room two"
// matches a button labelled "Room 2".  Sharp and flat signs are spelled out,
// so "B♭" is "bflat" rather than "b".
static void sw_normalize(const char* word, char* out, int out_size) {
  static const char* NUMBERS[] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
    "nine",
  };
  int n = 0;
  out[0] = '\0';
  for (const char* p = word; *p && n < out_size - 1; p++) {
    const char* spelled = NULL;
    if (*p == '#') spelled = "sharp";
    if (strncmp(p, "\u266f", 3) == 0) spelled = "sharp";  // ♯
    if (strncmp(p, "\u266d", 3) == 0) spelled = "flat";   // ♭
    if (spelled) {
      for (const char* q = spelled; *q && n < out_size - 1; q++) out[n++] = *q;
      if (*p != '#') p += 2;
      continue;
    }
    if (isalnum((unsigned char)*p)) out[n++] = (char)tolower(*p);
  }
  out[n] = '\0';
  for (int i = 0; i < 10; i++) {
    if (strcmp(out, NUMBERS[i]) == 0) {
      out[0] = (char)('0' + i);
      out[1] = '\0';
      return;
    }
  }
}

// A word as it's compared after a lead-in -- "press", "change key to" --
// where it can only be part of a name, so a sound-alike can safely stand in
// for the word it sounds like.  Not for bare numbers, where "for" would turn
// talking to the room into chord changes.
static void sw_normalize_named(const char* word, char* out, int out_size) {
  static const struct { const char* heard; const char* meant; } SOUNDS[] = {
    {"base", "bass"}, {"bas", "bass"},
    {"won", "1"}, {"to", "2"}, {"too", "2"}, {"tree", "3"},
    {"for", "4"}, {"fore", "4"}, {"ate", "8"},
    {"hi", "high"}, {"cord", "chord"}, {"chords", "chord"},
  };
  sw_normalize(word, out, out_size);
  for (int i = 0; i < (int)(sizeof(SOUNDS) / sizeof(SOUNDS[0])); i++) {
    if (strcmp(out, SOUNDS[i].heard) == 0) {
      snprintf(out, out_size, "%s", SOUNDS[i].meant);
      return;
    }
  }
}

// How the words the recognizer can't be expected to know are said, for the
// custom language model (see speechphrases.c).  X-SAMPA, as Apple's
// SFCustomLanguageModelData.supportedPhonemes lists it for en-US, with " for
// primary stress and % for secondary.
static const struct { const char* word; const char* phonemes; }
SW_PRONUNCIATIONS[] = {
  {"arpeggiator", "A r p \"E dZ i %e t @r"},
  {"mixolydian", "m %I k s @ l \"I d i @ n"},
  {"freygish", "f r \"e g I S"},
  {"reese", "r \"i s"},
  {"polysynth", "p \"A l i s %I n T"},
  {"octaveless", "\"A k t I v l @ s"},
};

// 1-7 for a number word or digit, else 0.  Deliberately not the homophones
// -- "to", "too", "for", "won" -- since those turn up in anything said into
// the microphone, and a chord change from talking to the room is worse than
// one that needs repeating.
static int nw_number_for_word(const char* word) {
  char clean[SW_NAME_MAX];
  sw_normalize(word, clean, sizeof(clean));
  if (clean[0] >= '1' && clean[0] <= '7' && clean[1] == '\0') {
    return clean[0] - '0';
  }
  return 0;
}

// A button's name as it's compared: its label's words normalized and run
// together, so "Room 2" is "room2" and it doesn't matter whether the
// recognizer heard "foot bass" or "footbass".
static void sw_name(const char* label, char* out, int out_size) {
  int n = 0;
  out[0] = '\0';
  const char* p = label;
  while (*p) {
    while (*p && isspace((unsigned char)*p)) p++;
    char word[SW_NAME_MAX];
    int w = 0;
    while (*p && !isspace((unsigned char)*p)) {
      if (w < (int)sizeof(word) - 1) word[w++] = *p;
      p++;
    }
    word[w] = '\0';
    char clean[SW_NAME_MAX];
    sw_normalize(word, clean, sizeof(clean));
    for (char* c = clean; *c && n < out_size - 1; c++) out[n++] = *c;
  }
  out[n] = '\0';
}

typedef enum {
  SW_NONE, SW_NUMBER, SW_PRESS, SW_SELECT, SW_KEY, SW_MODE,
} SwKind;

typedef struct {
  SwKind kind;
  // The number; the button the name belongs to; the key's pitch class, 0-11
  // from C; or the MODE_* the name is.
  int value;
} SwAction;

// Names (made by sw_name) and what each one means.
typedef struct {
  const char (*names)[SW_NAME_MAX];
  const int* values;
  int n;
} SwVocab;

// The seven natural keys, every way the recognizer might write them: "B",
// "bee".  No sharps or flats, which never come up, so a key is always one
// word and there's never a longer one to wait for.  The letters'
// sound-alikes only count here, after "change key to", where nothing else
// could be meant.
static SwVocab sw_key_vocab(void) {
  static char names[16][SW_NAME_MAX] = {
    "c", "see", "sea", "d", "dee", "e", "f", "ef", "eff", "g", "gee", "a",
    "ay", "b", "be", "bee",
  };
  static int values[16] = {
    0, 0, 0, 2, 2, 4, 5, 5, 5, 7, 7, 9, 9, 11, 11, 11,
  };
  SwVocab vocab = {(const char (*)[SW_NAME_MAX])names, values, 16};
  return vocab;
}

// The modes the arrow keys pick, by name.  MODE_* live in jammermidilib.h,
// which isn't included here, so the caller passes them in.
static SwVocab sw_mode_vocab(int major, int minor, int mixo, int freygish) {
  static char names[4][SW_NAME_MAX] = {
    "major", "minor", "mixolydian", "freygish",
  };
  static int values[4];
  values[0] = major;
  values[1] = minor;
  values[2] = mixo;
  values[3] = freygish;
  SwVocab vocab = {(const char (*)[SW_NAME_MAX])names, values, 4};
  return vocab;
}

typedef enum { SW_NOT_THIS, SW_WAIT, SW_DROP, SW_ACT } SwMatch;

// Whether a normalized word is one of a lead word's "|"-separated
// alternatives, ignoring the "?" that marks one optional.
static bool sw_lead_word(const char* lead, const char* word) {
  size_t len = strlen(word);
  const char* p = lead;
  while (*p) {
    const char* bar = strchr(p, '|');
    size_t alt = bar ? (size_t)(bar - p) : strlen(p);
    if (alt > 0 && p[alt - 1] == '?') alt--;
    if (alt == len && strncmp(p, word, len) == 0) return true;
    if (!bar) break;
    p = bar + 1;
  }
  return false;
}

// Whether the words from `i` are `lead` followed by a name in `vocab`.  A
// lead word can list alternatives with "|", and one ending in "?" is
// optional -- "to?" because the recognizer may drop it, or write "two".
// After the lead, words are compared with sw_normalize_named, so sound-alikes
// count.  On SW_ACT, *value is what the name means and *end the last word of
// it.
//
// A lead with no name after it yet is SW_WAIT, however long it's been: the
// recognizer hands words over one at a time, sometimes a second apart, and a
// "press" dropped for being early loses the "foot bass" that follows it.
// When what's been said is a whole name that could still grow into a longer
// one, it's SW_WAIT until the speaker has stopped (`settled`), and then the
// name as it stands -- unless the names are all one word (`one_word`), when
// the first word after the lead is the whole of it, and it acts at once.
// "E" would otherwise wait to see whether it was the start of "ef".  A lead followed by
// words that can't be a name is SW_DROP -- but only once the speaker has
// stopped, since until then the recognizer is still revising: "press our
// page" becomes "press arpeggiator" a moment later.
static SwMatch sw_match(const char* const* words, int n_words, int i,
                        const char* const* lead, int n_lead,
                        const SwVocab* vocab, bool one_word, bool settled,
                        int* value, int* end) {
  int j = i;
  for (int l = 0; l < n_lead; l++) {
    // Only the first word decides whether this is the phrase at all.
    if (j >= n_words) return l == 0 ? SW_NOT_THIS : SW_WAIT;
    char word[SW_NAME_MAX];
    sw_normalize(words[j], word, sizeof(word));
    bool optional = lead[l][strlen(lead[l]) - 1] == '?';
    if (sw_lead_word(lead[l], word)) {
      j++;
    } else if (!optional) {
      return l == 0 ? SW_NOT_THIS : SW_DROP;
    }
  }

  char said[SW_NAME_MAX * 4] = "";
  int best = -1, best_end = -1;
  bool could_grow = true;  // every word after the lead is a prefix
  int name_start = j;
  for (; j < n_words; j++) {
    if (one_word && j > name_start) {
      could_grow = false;
      break;
    }
    char next[SW_NAME_MAX];
    sw_normalize_named(words[j], next, sizeof(next));
    if (strlen(said) + strlen(next) >= sizeof(said)) {
      could_grow = false;
      break;
    }
    strcat(said, next);
    bool prefix = false;
    for (int k = 0; k < vocab->n; k++) {
      if (strcmp(vocab->names[k], said) == 0) {
        best = k;
        best_end = j;
      }
      if (strncmp(vocab->names[k], said, strlen(said)) == 0) prefix = true;
    }
    if (!prefix) {
      could_grow = false;
      break;
    }
  }
  // Only a longer name that the words so far lead into is worth waiting for;
  // one that's already been passed isn't.  With one-word names, once there's
  // a word there's nothing to wait for.
  if (one_word && j > name_start) could_grow = false;
  if (could_grow) {
    bool longer = false;
    for (int k = 0; k < vocab->n; k++) {
      if (strlen(vocab->names[k]) > strlen(said) &&
          strncmp(vocab->names[k], said, strlen(said)) == 0) {
        longer = true;
      }
    }
    could_grow = longer;
  }
  if (could_grow && (best < 0 || !settled)) return SW_WAIT;
  if (best < 0) return settled ? SW_DROP : SW_WAIT;
  *value = vocab->values[best];
  *end = best_end;
  return SW_ACT;
}

// The recognizer reports the whole transcription so far each time, and it
// revises as it goes: "for" can become "four" a moment later.  So acting on a
// transcription means walking it from *consumed, the first word not yet acted
// on.  Words passed over without being acted on aren't consumed, so one that
// later turns into a number still counts; nothing acted on is acted on twice.
//
// `buttons` is what "press" and "select" choose from, with each name's value
// its index into KEYS; `modes` is sw_mode_vocab's.  Returns the next action,
// advancing *consumed past it, or one of kind SW_NONE if there's nothing to
// do yet.  Call it until it returns SW_NONE.
static SwAction sw_next_action(const char* const* words, int n_words,
                               int* consumed, const SwVocab* buttons,
                               const SwVocab* modes, bool settled) {
  // The lead-ins, and the ways the recognizer has been heard writing them.
  static const char* PRESS[] = {"press|pressed|presses"};
  static const char* SELECT[] = {"select|selects|selected"};
  static const char* KEY[] = {"change", "key|keys", "to|too|2?"};
  static const char* MODE[] = {"change", "mode|modes|mowed", "to|too|2?"};
  SwVocab keys = sw_key_vocab();
  const struct {
    const char* const* lead;
    int n_lead;
    const SwVocab* vocab;
    bool one_word;
    SwKind kind;
  } PHRASES[] = {
    {PRESS, 1, buttons, false, SW_PRESS},
    {SELECT, 1, buttons, false, SW_SELECT},
    {KEY, 3, &keys, true, SW_KEY},
    {MODE, 3, modes, false, SW_MODE},
  };
  SwAction none = {SW_NONE, 0};

  for (int i = *consumed; i < n_words; i++) {
    bool dropped = false;
    for (int p = 0; p < (int)(sizeof(PHRASES) / sizeof(PHRASES[0])); p++) {
      int value, end;
      SwMatch match = sw_match(words, n_words, i, PHRASES[p].lead,
                               PHRASES[p].n_lead, PHRASES[p].vocab,
                               PHRASES[p].one_word, settled, &value, &end);
      if (match == SW_NOT_THIS) continue;
      if (match == SW_WAIT) return none;  // wait for the rest
      if (match == SW_ACT) {
        *consumed = end + 1;
        SwAction action = {PHRASES[p].kind, value};
        return action;
      }
      // Dropped by this phrase, but "change" leads two: the other may yet
      // want it.
      dropped = true;
    }
    if (dropped) {
      *consumed = i + 1;  // a lead-in that came to nothing
      continue;
    }

    int number = nw_number_for_word(words[i]);
    if (number) {
      *consumed = i + 1;
      SwAction action = {SW_NUMBER, number};
      return action;
    }
  }
  return none;
}

#endif
