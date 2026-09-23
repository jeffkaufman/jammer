// speechphrases.c -- everything the speech recognizer should expect to hear,
// for speechmodel.swift to build its custom language model from.
//
// Printed from the jammer's own tables rather than kept as a second list, so
// renaming a button renames it for the recognizer too:
//
//   button <tab> press-able name, in any state ("foot bass", "warm pad")
//   key    <tab> a key ("B"; only the naturals, which is all it acts on)
//   mode   <tab> a mode ("mixolydian")
//   number <tab> a Nashville number ("four")
//   pron   <tab> word <tab> X-SAMPA pronunciation
//
// Build: make speech-model.bin, which runs this into speechmodel.

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "whistle.h"
#include "speechwords.h"
#include "keylayout.h"
#include "keypad.h"

int main(void) {
  static char phrases[512][48];
  int n = all_spoken_phrases(phrases, 512);
  for (int i = 0; i < n; i++) printf("button\t%s\n", phrases[i]);

  static const char* LETTERS = "CDEFGAB";
  for (const char* l = LETTERS; *l; l++) {
    printf("key\t%c\n", *l);
  }

  SwVocab modes = sw_mode_vocab(MODE_MAJOR, MODE_MINOR, MODE_MIXO,
                                MODE_BETH_COHENS);
  for (int i = 0; i < modes.n; i++) printf("mode\t%s\n", modes.names[i]);

  static const char* NUMBERS[] = {
    "one", "two", "three", "four", "five", "six", "seven",
  };
  for (int i = 0; i < 7; i++) printf("number\t%s\n", NUMBERS[i]);

  for (int i = 0; i < (int)(sizeof(SW_PRONUNCIATIONS) /
                            sizeof(SW_PRONUNCIATIONS[0])); i++) {
    printf("pron\t%s\t%s\n", SW_PRONUNCIATIONS[i].word,
           SW_PRONUNCIATIONS[i].phonemes);
  }
  return 0;
}
