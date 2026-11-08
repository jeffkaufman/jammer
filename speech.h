#ifndef JML_SPEECH_H
#define JML_SPEECH_H

// Speaking to the rig.  With number recognition on (F3), say "four" and it
// goes to the IV; with speech recognition on (F8), "press foot bass" and that
// button is struck, "change key to A" or "change mode to minor".  Either, both
// or neither: it's the same recognizers listening for both, and the keys say
// which of what they hear is acted on.  See
// nashville_picks_chord for what each number means, speechwords.h for how the
// words are read, and key_spoken_names for what the buttons answer to.
//
// Apple's speech recognizer, on the device, listening to the same microphone
// as the whistle rather than the system's default input:
//
//   whistle_capture -> whistle_input_tap -> speech ring -> 50ms timer ->
//     SFSpeechAudioBufferRecognitionRequest -> result handler -> the lock
//
// It's given a dictionary two ways: a custom language model built from every
// phrase it should expect (speechmodel.swift, compiled here at startup), and
// the same phrases as hint words.  The model is the one that matters; the
// hints are for when it can't be had.
//
// It only hears what's loud enough to be said right into the microphone (the
// gate in speechgate.h, set from the Speech Recognition menu).  A number it
// hears lands on a beat about two after the word began (see "Landing"), so
// it lands in time however long the recognizer took; anything else is done the
// moment it's heard.  A chord change has to land on a beat, but a button, a
// key or a mode is wanted now, not a beat or two later.
//
// It only listens while F3 or F8 is on.  Partial results are
// acted on as they arrive, so a number lands a moment after it's said rather
// than after a pause long enough for the recognizer to call the sentence
// done.
//
// Numbers have a faster way in too: numrec.h, which knows only one to seven,
// in your voice, and hears them within about a tenth of a second of the word
// ending.  See "The fast path" below.  Where the two disagree, numheard.h
// keeps the audio for the fast one to learn from.
//
// Everything that touches the recognizer runs on speech_queue: the timer, and
// the result handler, via the recognizer's own queue.
//
// Include after jammermidilib.h and whistle.h, and after LOCK is defined.

#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

#include "speechwords.h"
#include "speechgate.h"
#include "speechtiming.h"
#include "numrec.h"

// ---------------------------------------------------------------------------
// The ring
//
// The microphone's own thread writes, speech_queue reads.  Five seconds at
// 48kHz, which is far more than a 50ms timer ever lets build up; if it does
// fill, the newest audio is dropped rather than blocking a realtime thread.
// ---------------------------------------------------------------------------

#define SPEECH_RING_FRAMES (1u << 18)

static float speech_ring[SPEECH_RING_FRAMES];
static _Atomic unsigned speech_ring_write;
static _Atomic unsigned speech_ring_read;
static _Atomic int speech_listening;  // the tap only copies while this is set
static _Atomic int speech_recording;  // ... or this: numtrain.h is recording
// Everything the microphone sends, ungated, as it's drained: numtrain.h's
// recorder while it's recording.  Speech queue.
static void (*speech_record_hook)(const float* samples, int n) = NULL;
// The clock as each block arrives, and how many samples the ring has been
// handed by then, so a place on the microphone's timeline can be put on
// now()'s clock (speech_sample_ns).  speech_drain counts the same samples.
static _Atomic long long speech_tap_total;
static _Atomic uint64_t speech_tap_ns;

// The last dozen seconds of the microphone, ungated, as drained, for
// numheard.h to cut clips from.  speech_mic_samples counts everything drained
// so far, and is the one timeline that the fast path's frames and Apple's
// words are both put on.  Speech queue.
static float* speech_mic;
static long long speech_mic_mask;  // its length, a power of two, less one
static long long speech_mic_samples;

static void speech_tap(const float* samples, int frames) {
  if (!atomic_load_explicit(&speech_listening, memory_order_relaxed) &&
      !atomic_load_explicit(&speech_recording, memory_order_relaxed)) {
    return;
  }
  unsigned write = atomic_load_explicit(&speech_ring_write,
                                        memory_order_relaxed);
  unsigned read = atomic_load_explicit(&speech_ring_read,
                                       memory_order_acquire);
  if ((unsigned)frames > SPEECH_RING_FRAMES - (write - read) - 1) return;
  for (int i = 0; i < frames; i++) {
    speech_ring[(write + (unsigned)i) & (SPEECH_RING_FRAMES - 1)] =
      samples[i];
  }
  atomic_store_explicit(&speech_ring_write, write + (unsigned)frames,
                        memory_order_release);
  atomic_store_explicit(&speech_tap_ns, now(), memory_order_relaxed);
  atomic_store_explicit(&speech_tap_total,
                        atomic_load_explicit(&speech_tap_total,
                                             memory_order_relaxed) + frames,
                        memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The recognizer
// ---------------------------------------------------------------------------

// How often the ring is drained into the recognizer.  Short, because audio
// sitting here waiting is audio the recognizer hears late, and that's time
// straight off how soon a change can land.
#define SPEECH_TICK_MS 10
// A recognition task doesn't run forever -- Apple's limit is about a minute
// -- so start a fresh one before that, and whenever one ends on its own.
#define SPEECH_TASK_MAX_S 50
// A task whose transcription has grown this long is restarted early, so the
// word list the result handler walks stays small.
#define SPEECH_MAX_WORDS 200
// How long without a new word before a button's name that could still be
// the start of a longer one is taken as it stands, and a lead-in followed by
// nothing it names is let go.  Longer than it looks like it needs to be: the
// recognizer's words can arrive the better part of a second apart.  (Keys
// don't wait on it: they're all one word -- see sw_key_vocab.)
#define SPEECH_SETTLE_MS 700
#define SPEECH_MAX_NAMES 256


static SpeechGate speech_gate;  // speech queue only
static dispatch_queue_t speech_queue;
static dispatch_source_t speech_timer;  // held so it lives as long as the app
static SFSpeechRecognizer* speech_recognizer;
static SFSpeechAudioBufferRecognitionRequest* speech_request;
static SFSpeechRecognitionTask* speech_task;
static AVAudioFormat* speech_format;
static uint64_t speech_task_started;
static int speech_task_serial;          // counts tasks, for numheard.h
static long long speech_task_mic_start; // speech_mic_samples when it started
static int speech_consumed;    // words in this task's transcript acted on
static NSArray<NSString*>* speech_words;  // this task's transcript so far
// Where on the microphone's timeline each word was said, from Apple's
// timestamps, or -1 where it gave none.
static long long speech_word_from[SPEECH_MAX_WORDS];
static long long speech_word_to[SPEECH_MAX_WORDS];
static uint64_t speech_words_at;          // when it last changed
static bool speech_words_settled;         // and it's been read as settled
static bool speech_asked;      // authorization has been requested
static bool speech_authorized;
static NSArray<NSString*>* speech_hints;  // every phrase, for contextualStrings
// The compiled dictionary, once it's ready, and if not, why not.
static SFSpeechLanguageModelConfiguration* speech_dictionary
  API_AVAILABLE(macos(14));
static char speech_dictionary_state[128] = "no dictionary";

// ---------------------------------------------------------------------------
// What the window shows
//
// Written on speech_queue and read by the window, both under the lock.  So
// that "it isn't working" can be told apart: not listening at all, listening
// to silence, or hearing words it doesn't act on.
// ---------------------------------------------------------------------------

// Why it is or isn't listening, for the speech row: empty when it's off
// or listening normally, since NR and SR already say that much.
static char speech_state[160] = "";
static char speech_heard_text[200];     // the transcript, the latest end
static char speech_last_action[64];     // what it last did
static float speech_level;              // peak fed to it since last read
static uint64_t speech_error_at;        // so an error stays up long enough
static bool speech_gate_open;           // something loud, just now

// The gate's threshold, peak dBFS, from the Speech Recognition menu.  Strict
// to start with: talking right into the microphone clears it, the room
// doesn't.
#define SPEECH_GATE_DEFAULT_DB -20.0
#define SPEECH_GATE_MIN_DB -60.0
#define SPEECH_GATE_MAX_DB 0.0
static double speech_gate_db = SPEECH_GATE_DEFAULT_DB;  // under the lock

static void speech_set_state(const char* state) {
  LOCK();
  snprintf(speech_state, sizeof(speech_state), "%s", state);
  UNLOCK();
}


// What an action is, in words, for the speech row.  Caller must hold the
// lock, for the key's name.
static void speech_describe_locked(const SwAction* action, char* out,
                                   int size) {
  static const char* KEYS_BY_PITCH[12] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
  };
  static const char* MODES[] = {"?", "major", "mixolydian", "minor",
                                "freygish"};
  switch (action->kind) {
  case SW_NUMBER: snprintf(out, size, "%d", action->value); return;
  case SW_KEY: snprintf(out, size, "key %s", KEYS_BY_PITCH[action->value]);
    return;
  case SW_MODE: snprintf(out, size, "mode %s", MODES[action->value]); return;
  default: break;
  }
  char title[48];
  key_spoken_title(&KEYS[action->value], title, sizeof(title));
  snprintf(out, size, "%s %s", action->kind == SW_SELECT ? "select" : "press",
           title);
}

// Draws the flash on a key struck by voice.  Set by jammer-mac.m, which owns
// the view.
static void (*speech_flash)(int key) = NULL;

// Told of what's done, to log it: numbers, keys and modes.  A press or a
// select is a strike, which key_strike_hook (keypad.h) already hears.
// Caller holds the lock.
static void (*speech_action_hook)(const SwAction* action) = NULL;

// Do it.  Caller must hold the lock -- this runs inside the beat, just before
// its notes, so that they're the new ones.
static void speech_apply_locked(const SwAction* action) {
  if (speech_action_hook && action->kind != SW_PRESS &&
      action->kind != SW_SELECT) {
    speech_action_hook(action);
  }
  char text[64];
  speech_describe_locked(action, text, sizeof(text));
  snprintf(speech_last_action, sizeof(speech_last_action), "%s", text);
  printf("heard %s\n", text);
  fflush(stdout);

  switch (action->kind) {
  case SW_NUMBER: nashville_picks_chord(action->value); break;
  case SW_KEY: change_key(action->value); break;
  case SW_MODE: musical_mode = action->value; break;  // as the arrow keys
  case SW_PRESS:
  case SW_SELECT: {
    const Key* key = &KEYS[action->value];
    bool selecting = action->kind == SW_SELECT;
    if (selecting && !key_selects(key)) break;  // it would only be a click
    strike_key_locked(key, selecting);
    if (speech_flash) speech_flash(action->value);
    break;
  }
  default: break;
  }
}

// ---------------------------------------------------------------------------
// Landing
//
// A number lands on the pedal beat nearest SPEECH_BEATS_AFTER_ONSET beats
// after you started saying it (speechtiming.h) -- the same for both
// recognizers -- and without pedals, that many beats of SPEECH_DEFAULT_BPM
// after the onset.  So with the feet going, start a number around the "and"
// of the beat two before the change and the chord comes in on the change,
// however long the word took or the recognizer did.  If that
// has gone by -- a long word at a fast tempo, or Apple taking its time --
// it's the next beat, never the moment it's heard, which is a different
// time for every word.
//
// On the beat itself, not near it.  The beat here is the pedal: each hit is
// when the rhythm parts play, so a change that arrives a few milliseconds
// after it is heard only on the next note, half a beat or more late.  So
// once a landing is within a quarter beat, the next pedal beat makes it,
// inside the beat before its notes (before_beat_hook, from arpeggiate); and
// if no pedal has come by its time, it's made then, so a pedal a hair late
// still plays the new chord.
//
// The onset is the fast recognizer's: where its utterance detector found
// the word rising out of the quiet (numrec.h), on the microphone's
// timeline, which speech_tap stamps with the clock as each block arrives
// (speech_sample_ns).  Apple's words are set against the same utterances,
// by its timestamps for them, so a number it hears lands as the fast
// path's would have.  Both recognizers hearing one word is one landing.
// ---------------------------------------------------------------------------

#define SPEECH_DEFAULT_BPM 116
#define SPEECH_MAX_WAITING 16
// Two numbers whose onsets are this close are the same word, heard twice.
#define SPEECH_SAME_WORD_S 0.4
// How near Apple's timestamps for a word must come to an utterance to be it.
#define SPEECH_WORD_SLACK_S 0.15
// The longest an utterance can have been over and still be a word Apple has
// only now written.  Apple writes a word within about a second of its end;
// without this, a word whose utterance went unnoticed took the one before,
// seconds earlier, and landed on whatever beat came next.
#define SPEECH_WORD_MAX_AGE_S 2.0

// A number waiting for its time.  Under the lock.
typedef struct {
  int number;
  uint64_t onset_ns;
  uint64_t due;       // when it lands
  uint64_t earliest;  // or a pedal beat from here on makes it
  bool fast;          // the fast recognizer's, else Apple's
  bool borderline;    // its beat chosen by the changes before it
  long beat;          // which pedal beat, as speech_pedal_beats counts
} SpeechLanding;
static SpeechLanding speech_waiting[SPEECH_MAX_WAITING];
static int speech_n_waiting;
// Where changes have landed since the pedals last started, for the next
// (speech_landing_time): cleared whenever they stop.  Under the lock.
static SpeechRecent speech_recent;
// Pedal beats so far, counted in speech_before_beat.  Under the lock.
static long speech_pedal_beats;

// A place on the microphone's timeline, as speech_drain counts it, on
// now()'s clock: from the clock at the latest block and how far back of it
// the sample is.
static uint64_t speech_sample_ns(long long sample) {
  long long total = atomic_load_explicit(&speech_tap_total,
                                         memory_order_relaxed);
  uint64_t at = atomic_load_explicit(&speech_tap_ns, memory_order_relaxed);
  double behind = (double)(total - sample) / speech_format.sampleRate;
  if (behind < 0) behind = 0;
  return at - (uint64_t)(behind * 1e9);
}

// The beat, and whether it's the pedals': theirs if a pedal hit has come
// within the last beat and a half, else the default tempo's.
// `pedal_beat` is which beat the last hit was, counting them.
static uint64_t speech_beat_now(bool* pedals, uint64_t* pedal_ns,
                                long* pedal_beat) {
  LOCK();
  uint64_t beat = current_beat_ns, last = last_downbeat_ns;
  long count = speech_pedal_beats;
  UNLOCK();
  bool going = beat > 0 && last > 0 && now() - last < beat * 3 / 2;
  if (pedals) *pedals = going;
  if (pedal_ns) *pedal_ns = going ? last : 0;
  if (pedal_beat) *pedal_beat = count;
  return going ? beat : 60 * NS_PER_SEC / SPEECH_DEFAULT_BPM;
}

// When the talking last stopped (speech_drain), for the log.
static uint64_t speech_last_loud_ns;

// Milliseconds since, for the log.
static long speech_ms_since_stop(void) {
  return (long)(((int64_t)now() - (int64_t)speech_last_loud_ns) / 1000000);
}

// Make the landing at `i`, and forget it.  Caller holds the lock.
static void speech_land_apply_locked(int i, const char* how) {
  SpeechLanding w = speech_waiting[i];
  speech_n_waiting--;
  memmove(&speech_waiting[i], &speech_waiting[i + 1],
          sizeof(SpeechLanding) * (size_t)(speech_n_waiting - i));
  uint64_t t = now();
  printf("timing: %d landed %s, %+ldms from its time, %ldms after the "
         "onset\n", w.number, how,
         (long)(((int64_t)t - (int64_t)w.due) / 1000000),
         (long)((t - w.onset_ns) / 1000000));
  speech_recent_add(&speech_recent, w.beat, w.borderline);
  SwAction action = {SW_NUMBER, w.number};
  speech_apply_locked(&action);
}

// The next beat's notes are about to play: make whatever's within reach
// first.  Called by arpeggiate with the lock held.
static void speech_before_beat(void) {
  speech_pedal_beats++;  // this one, before last_downbeat_ns says so
  uint64_t t = now();
  for (int i = 0; i < speech_n_waiting;) {
    if (t >= speech_waiting[i].earliest) {
      speech_land_apply_locked(i, "on the beat");
    } else {
      i++;
    }
  }
}

// Whatever's time has come, with no beat having made it.  Speech queue: on
// a timer set for each landing, and every tick.
static void speech_land_due(void) {
  LOCK();
  uint64_t t = now();
  for (int i = 0; i < speech_n_waiting;) {
    if (t >= speech_waiting[i].due) {
      speech_land_apply_locked(i, "on its timer");
    } else {
      i++;
    }
  }
  UNLOCK();
}

// A number heard, said from `onset_ns`: set it to land, and the voice-led
// drones gliding there.  True if it's a new word rather than the other
// recognizer's again; the fast recognizer's word wins a disagreement.
static bool speech_land(int number, uint64_t onset_ns, bool fast,
                        const char* heard_by) {
  uint64_t t = now();
  bool pedals;
  uint64_t pedal_ns;
  long pedal_beat;
  uint64_t beat = speech_beat_now(&pedals, &pedal_ns, &pedal_beat);
  LOCK();
  SpeechRecent recent = speech_recent;
  UNLOCK();
  SpeechLandingTime when = speech_landing_time(onset_ns, beat, pedal_ns,
                                               pedal_beat, t, &recent);
  bool is_new = true;
  LOCK();
  for (int i = 0; i < speech_n_waiting; i++) {
    SpeechLanding* w = &speech_waiting[i];
    uint64_t gap = w->onset_ns > onset_ns ? w->onset_ns - onset_ns
                                          : onset_ns - w->onset_ns;
    if (gap >= SPEECH_SAME_WORD_S * NS_PER_SEC) continue;
    is_new = false;
    if (w->number == number || w->fast) {
      UNLOCK();
      printf("%s heard %d too; %s\n", heard_by, number,
             w->number == number ? "already waiting"
                                 : "the fast recognizer's word stands");
      fflush(stdout);
      return false;
    }
    // Apple's word, waiting, and the fast recognizer disagrees: its word,
    // at the time already set.
    w->number = number;
    w->fast = true;
    when.due = w->due;
    when.earliest = w->earliest;
    when.beat = w->beat;
    when.borderline = w->borderline;
    break;
  }
  if (is_new) {
    if (speech_n_waiting == SPEECH_MAX_WAITING) {
      speech_land_apply_locked(0, "to make room");
    }
    speech_waiting[speech_n_waiting++] =
      (SpeechLanding){number, onset_ns, when.due, when.earliest, fast,
                      when.borderline, when.beat};
  }
  nashville_leads(number, when.due);
  snprintf(speech_last_action, sizeof(speech_last_action), "%d …", number);
  UNLOCK();
  printf("timing: %s %d, %ldms after the onset; lands %d beats after it, "
         "%+ldms from now (%.0f bpm%s)", heard_by, number,
         (long)((t - onset_ns) / 1000000), when.beats,
         (long)(((int64_t)when.due - (int64_t)t) / 1000000),
         60.0 * NS_PER_SEC / beat, pedals ? ", the pedals'" : "");
  if (pedals) {
    printf(", pedal beat %ld%s", when.beat,
           when.borderline ? ", borderline: by the changes before" : "");
  }
  printf("\n");
  fflush(stdout);
  if (when.due > t) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(when.due - t)),
                   speech_queue, ^{ speech_land_due(); });
  } else {
    speech_land_due();
  }
  return is_new;
}

// ---------------------------------------------------------------------------
// The fast path
//
// numrec.h hears a bare number within about a tenth of a second of the word
// ending, having learned your voice from numtrain.h's recordings.  It's fed
// the same microphone as Apple's recognizer, before the gate -- it has its
// own, set from the same menu -- and only while number recognition (F3) is
// on.  What it hears lands as above, from the onset its detector found.
// Apple's recognizer hears the same word a while later: a number from it
// within SPEECH_FAST_CLAIM_S of one the fast path took is that word again,
// and is dropped.  A number the fast path wasn't sure of it leaves to Apple,
// whose word lands from the same onset -- the utterances the detector found
// are kept for that (speech_utterances), whether or not it took them.
// ---------------------------------------------------------------------------

#define SPEECH_FAST_CLAIM_S 3

// numheard.h, which keeps the audio when the two recognizers disagree.
static void nh_fast_judged(const NrStream* s, long long onset, long long end,
                           int number, const NrResult* r);
static void nh_apple_word(int index, const char* text, int number);
static void nh_tick(void);
static bool nh_unreviewed(NSString* tsv);
static bool nh_reviewed(NSString* tsv);

static NrModel* speech_fast_model;  // speech queue; NULL until learned
static int speech_fast_generation;  // counts the models it's had
static NrStream* speech_fast_stream;
// Where the stream's sample 0 would be on the microphone's timeline: the
// stream only hears while F3 is on, so this moves on each time it's off.
static long long speech_fast_offset;
static long long speech_fast_pushed;
static char speech_fast_state[64] = "fast: learning";
static int speech_fast_unclaimed;   // fast numbers Apple hasn't reported yet
static uint64_t speech_fast_at;

// The last few utterances the detector found, on the microphone's timeline,
// for putting Apple's words on their onsets.  Speech queue.
typedef struct { long long onset, end; } SpeechUtterance;
#define SPEECH_UTTERANCES 8
static SpeechUtterance speech_utterances[SPEECH_UTTERANCES];
static int speech_n_utterances;  // all told; a ring

static void speech_note_utterance(long long onset, long long end) {
  int n = speech_n_utterances < SPEECH_UTTERANCES ? speech_n_utterances
                                                  : SPEECH_UTTERANCES;
  for (int k = 0; k < n; k++) {
    if (speech_utterances[k].onset == onset) {  // again, now it's over
      speech_utterances[k].end = end;
      return;
    }
  }
  speech_utterances[speech_n_utterances++ % SPEECH_UTTERANCES] =
    (SpeechUtterance){onset, end};
}

// Where the word Apple wrote as word `index` of its transcript was said: the
// onset of the utterance found there, or -1 if none fits.  By Apple's
// timestamps for the word when it gives them; else by when it arrived, as
// the latest utterance.  The one still going counts too, since Apple can be
// quicker than the detector's end of it.  Either way, never one that ended
// more than SPEECH_WORD_MAX_AGE_S ago.
static long long speech_onset_for_word(int index) {
  long long from = speech_word_from[index], to = speech_word_to[index];
  double rate = speech_format.sampleRate;
  long long slack = (long long)(SPEECH_WORD_SLACK_S * rate);
  long long oldest_end =
    atomic_load_explicit(&speech_tap_total, memory_order_relaxed) -
    (long long)(SPEECH_WORD_MAX_AGE_S * rate);
  long long best = -1;
  int n = speech_n_utterances < SPEECH_UTTERANCES ? speech_n_utterances
                                                  : SPEECH_UTTERANCES;
  for (int k = 0; k < n; k++) {
    const SpeechUtterance* u = &speech_utterances[k];
    bool fits = to < 0 || (from <= u->end + slack && to >= u->onset - slack);
    if (fits && u->end >= oldest_end && u->onset > best) best = u->onset;
  }
  const NrStream* s = speech_fast_stream;
  if (s->active) {
    long long onset = s->onset * s->f.hop + speech_fast_offset;
    if ((to < 0 || to >= onset - slack) && onset > best) best = onset;
  }
  return best;
}

// A number from Apple's recognizer that the fast path already took.  Before
// F3 is asked, so a number the fast path took just before F3 went off still
// isn't done twice.
static bool speech_fast_claims(const SwAction* action) {
  if (action->kind != SW_NUMBER || speech_fast_unclaimed == 0) return false;
  if (now() - speech_fast_at > SPEECH_FAST_CLAIM_S * NS_PER_SEC) {
    speech_fast_unclaimed = 0;
    return false;
  }
  speech_fast_unclaimed--;
  printf("apple heard %d too; the fast path already took it\n",
         action->value);
  fflush(stdout);
  return true;
}

// Heard: a number lands from its onset; anything else is done now.  `word`
// is which word of Apple's transcript it was, for its onset.
static void speech_queue_action(const SwAction* action, int word) {
  if (speech_fast_claims(action)) return;
  // Numbers only with F3, everything else only with F8.
  LOCK();
  bool wanted = action->kind == SW_NUMBER ? speech_chooses_notes
                                          : speech_commands_on;
  UNLOCK();
  if (!wanted) return;
  if (action->kind != SW_NUMBER) {
    LOCK();
    speech_apply_locked(action);
    UNLOCK();
    printf("timing: made %ldms after you stopped\n", speech_ms_since_stop());
    fflush(stdout);
    return;
  }
  long long onset = speech_onset_for_word(word);
  if (onset < 0) {
    printf("timing: apple heard %d, but nothing was found said; counting "
           "from now\n", action->value);
  }
  speech_land(action->value, onset >= 0 ? speech_sample_ns(onset) : now(),
              false, "apple heard");
}

// The fast recognizer took `number`, said from `onset_sample` on the
// microphone's timeline; `said_ms` since the word began and `quiet_ms` since
// it ended, for the log.  False if F3 has just gone off.
static bool speech_fast_take(int number, long long onset_sample, int said_ms,
                             int quiet_ms) {
  LOCK();
  bool numbers = speech_chooses_notes;
  if (numbers) {
    snprintf(speech_heard_text, sizeof(speech_heard_text), "%d (fast)",
             number);
  }
  UNLOCK();
  if (!numbers) return false;
  printf("fast: heard %d, %dms after the word began and %dms after it "
         "ended\n", number, said_ms, quiet_ms);
  if (speech_land(number, speech_sample_ns(onset_sample), true, "fast heard")) {
    speech_fast_unclaimed++;
    speech_fast_at = now();
  }
  return true;
}

static bool speech_fast_utterance(void* ctx, NrStream* s, long long onset,
                                  long long end, bool clear, int quiet,
                                  bool final) {
  (void)ctx;
  long long hop = s->f.hop;
  long long onset_sample = onset * hop + speech_fast_offset;
  long long end_sample = (end + 1) * hop + speech_fast_offset;
  // Kept whether or not it's taken, or even matched: Apple may hear a
  // number in it, and that lands from this onset.
  if (final) speech_note_utterance(onset_sample, end_sample);
  if (!speech_fast_model || !clear) return final;
  // Asked again every 10ms until its word's wait is up, but while it's quiet
  // the utterance doesn't change, so neither does what it's nearest: match it
  // once, and after that it's only the wait.  Different if the talking
  // started again and moved its end, or there's a new model.
  static long long matched_onset = -1, matched_end = -1;
  static int matched_n, matched_generation;
  static NrResult r;
  float feat[NR_MAX_FRAMES + 2 * NR_PAD][NR_DIM];
  int n = nr_utterance(s, onset, end, feat);
  if (onset != matched_onset || end != matched_end || n != matched_n ||
      speech_fast_generation != matched_generation) {
    r = nr_classify(speech_fast_model, feat, n, &s->p, -1, 0, 0);
    matched_onset = onset;
    matched_end = end;
    matched_n = n;
    matched_generation = speech_fast_generation;
  }
  if (r.label >= 0 && quiet >= speech_fast_model->wait[r.label]) {
    speech_note_utterance(onset_sample, end_sample);
    // A tick's worth on top: that's how long the audio waited in the ring.
    // Frames are 10ms, and the one just finished is the quiet's last.
    int quiet_ms = quiet * 10 + SPEECH_TICK_MS;
    int said_ms = (int)(end - onset + 1) * 10 + quiet_ms;
    if (speech_fast_take(r.label + 1, onset_sample, said_ms, quiet_ms)) {
      nh_fast_judged(s, onset, end, r.label + 1, &r);
    }
    return true;
  }
  if (final) nh_fast_judged(s, onset, end, 0, &r);
  if (final && r.nearest >= 0 && r.nearest != NR_OTHER) {
    printf("fast: maybe %s (%.2f, runner-up %.2f), but %s; leaving it to "
           "apple\n", NR_WORDS[r.nearest], r.dist, r.runner_up,
           r.why ? r.why : "?");
    fflush(stdout);
  }
  return final;
}

// Something said that was too long to be a number.  Still kept as an
// utterance, so that if Apple hears a number in it, that lands from its
// onset rather than from an older one's; and still set beside what Apple
// heard (numheard.h), as the fast path's "nothing".
static bool speech_fast_overlong(void* ctx, NrStream* s, long long onset,
                                 long long end, bool clear, int quiet,
                                 bool final) {
  (void)ctx; (void)clear; (void)quiet; (void)final;
  long long hop = s->f.hop;
  speech_note_utterance(onset * hop + speech_fast_offset,
                        (end + 1) * hop + speech_fast_offset);
  printf("fast: %lldms, too long to be a number; leaving it to apple\n",
         (end - onset + 1) * 10);
  fflush(stdout);
  NrResult none = {.label = -1, .nearest = -1, .dist = INFINITY,
                   .runner_up = INFINITY, .why = "too long"};
  nh_fast_judged(s, onset, end, 0, &none);
  return true;
}

// Everything numtrain.h has recorded and numheard.h has kept, learned into a
// model, finished.  `sources`, if given, gets the .tsv each session number
// came from, so numheard.h's review can find a recording again.  Reads them
// all: never on the audio or speech queues.
static NrModel* speech_fast_learn_all(NSMutableArray<NSString*>* sources,
                                      int* sessions, int* clips) {
  NSURL* support = [[NSFileManager.defaultManager
    URLsForDirectory:NSApplicationSupportDirectory
           inDomains:NSUserDomainMask] firstObject];
  NSURL* dir =
    [support URLByAppendingPathComponent:@"com.jefftk.jammer/numbers"];
  NSArray<NSURL*>* files = [NSFileManager.defaultManager
    contentsOfDirectoryAtURL:dir includingPropertiesForKeys:nil
                     options:0 error:nil];
  // numheard.h's clips, which are laid out as sessions of their own.
  files = [files arrayByAddingObjectsFromArray:
    [NSFileManager.defaultManager
      contentsOfDirectoryAtURL:[dir URLByAppendingPathComponent:@"heard"]
    includingPropertiesForKeys:nil options:0 error:nil] ?: @[]];
  NrModel* model = calloc(1, sizeof(NrModel));
  NrParams p = nr_default_params(SPEECH_GATE_DEFAULT_DB);
  int session = 0;
  *clips = 0;
  for (NSURL* file in files) {
    if (![file.pathExtension isEqualToString:@"wav"]) continue;
    NSURL* tsv =
      [file.URLByDeletingPathExtension URLByAppendingPathExtension:@"tsv"];
    bool clip = [file.lastPathComponent hasPrefix:@"heard-"];
    NSString* text = [NSString stringWithContentsOfURL:tsv
                                              encoding:NSUTF8StringEncoding
                                                 error:nil] ?: @"";
    if (clip && nh_unreviewed(text)) continue;
    long long n;
    double rate;
    float* x = nr_read_wav(file.path.UTF8String, &n, &rate);
    NrPrompt* prompts = NULL;
    NrReview* reviews = NULL;
    float gate_db = SPEECH_GATE_DEFAULT_DB, room_db = p.room_db;
    int n_prompts = nr_read_prompts(tsv.path.UTF8String, &prompts, &gate_db,
                                    &room_db);
    int n_reviews = nr_read_reviews(tsv.path.UTF8String, &reviews);
    if (x && n_prompts > 0) {
      // A little below the gate it was recorded with, so the quieter
      // words are learned too: which prompt was up says what they were.
      NrParams sp = p;
      sp.trigger_db = gate_db - 6;
      sp.room_db = room_db;
      // A clip you've said the word of is reviewed as a whole.
      nr_learn_session(model, x, n, rate, prompts, n_prompts, reviews,
                       n_reviews, clip && nh_reviewed(text), &sp,
                       session++);
      [sources addObject:tsv.path];
      if (clip) (*clips)++;
    }
    free(x);
    free(prompts);
    free(reviews);
  }
  *sessions = session;
  nr_model_finish(model, &p);
  return model;
}

// Learn from every recording numtrain.h has made, off the speech queue since
// it reads them all, then hand the result over.  At startup, and again after
// each new recording.
static void speech_fast_learn(void) {
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    int session, clips;
    NrModel* model = speech_fast_learn_all(nil, &session, &clips);
    int numbers = 0;
    for (int i = 0; i < model->n; i++) {
      numbers += model->t[i].label != NR_OTHER;
    }
    dispatch_async(speech_queue, ^{
      if (speech_fast_model) {
        nr_model_free(speech_fast_model);
        free(speech_fast_model);
      }
      speech_fast_model = numbers > 0 ? model : NULL;
      speech_fast_generation++;
      if (numbers > 0) {
        snprintf(speech_fast_state, sizeof(speech_fast_state),
                 "fast: %d numbers learned", numbers);
      } else {
        nr_model_free(model);
        free(model);
        snprintf(speech_fast_state, sizeof(speech_fast_state),
                 "fast: no recordings");
      }
      printf("speech %s from %d sessions and %d kept clips\n",
             speech_fast_state, session - clips, clips);
      fflush(stdout);
    });
  });
}

static void speech_end_task(void);
static void speech_start_task(void);

// Act on whatever in the transcript hasn't been acted on.  `settled` when
// the speaker has stopped, so a name that could still have grown longer is
// taken as it stands.
static void speech_read(bool settled) {
  int n = (int)MIN(speech_words.count, (NSUInteger)SPEECH_MAX_WORDS);
  const char* words[SPEECH_MAX_WORDS];
  for (int i = 0; i < n; i++) words[i] = speech_words[i].UTF8String ?: "";

  // What the buttons are called depends on what's selected, which the last
  // press may have just changed, so ask again for each action.
  while (true) {
    static char names[SPEECH_MAX_NAMES][SW_NAME_MAX];
    static int keys[SPEECH_MAX_NAMES];
    LOCK();
    int n_names = key_spoken_names(names, keys, SPEECH_MAX_NAMES);
    UNLOCK();
    SwVocab buttons = {(const char (*)[SW_NAME_MAX])names, keys, n_names};
    SwVocab modes = sw_mode_vocab(MODE_MAJOR, MODE_MINOR, MODE_MIXO,
                                  MODE_BETH_COHENS);
    SwAction action = sw_next_action(words, n, &speech_consumed, &buttons,
                                     &modes, settled);
    if (action.kind == SW_NONE) break;
    if (action.kind == SW_NUMBER) {
      // A bare number is the one word just before where it's got to.
      nh_apple_word(speech_consumed - 1, words[speech_consumed - 1],
                    action.value);
    }
    speech_queue_action(&action, speech_consumed - 1);
  }

  // Everything heard has been dealt with: start listening afresh, so the next
  // thing said is word one of a new transcript.  The recognizer starts its
  // transcript over by itself after a pause, and a count of words already
  // acted on would otherwise point past the end of the new one and miss it.
  // Straight away rather than on the next tick, so no audio falls between.
  if (n > 0 && speech_consumed >= n) {
    speech_end_task();
    speech_start_task();
  }
}

static void speech_end_task(void) {
  [speech_request endAudio];
  [speech_task cancel];
  speech_request = nil;
  speech_task = nil;
  speech_words = nil;
}

static void speech_heard(SFSpeechRecognitionResult* result) {
  NSMutableArray<NSString*>* words = [NSMutableArray array];
  double rate = speech_format.sampleRate;
  for (SFTranscriptionSegment* segment in result.bestTranscription.segments) {
    int i = (int)words.count;
    if (i < SPEECH_MAX_WORDS) {
      // What the request heard is the microphone, gated, sample for sample,
      // from when the task started.
      bool timed = segment.duration > 0;
      speech_word_from[i] = timed ? speech_task_mic_start +
        (long long)(segment.timestamp * rate) : -1;
      speech_word_to[i] = timed ? speech_task_mic_start +
        (long long)((segment.timestamp + segment.duration) * rate) : -1;
    }
    [words addObject:segment.substring];
  }
  if (![words isEqualToArray:speech_words ?: @[]]) {
    // Each word that's new or revised, for numheard.h to set against what
    // the fast path made of the same moment.
    for (int i = 0; i < (int)MIN(words.count, (NSUInteger)SPEECH_MAX_WORDS);
         i++) {
      if (i >= (int)speech_words.count ||
          [words[i] caseInsensitiveCompare:speech_words[i]] != NSOrderedSame) {
        nh_apple_word(i, words[i].UTF8String ?: "", 0);
      }
    }
    // Has the recognizer started over?  If the words already acted on aren't
    // at the start any more, it's either that or a revision of them -- "foot
    // bass" becoming "football" -- and a revision comes quickly, while a new
    // sentence comes after a pause.  Getting that wrong one way misses a
    // command; the other way presses a button twice.
    bool same_start = (int)words.count >= speech_consumed;
    for (int i = 0; same_start && i < speech_consumed; i++) {
      same_start = [words[i] caseInsensitiveCompare:speech_words[i]] ==
                   NSOrderedSame;
    }
    if (!same_start) {
      if (now() - speech_words_at > NS_PER_SEC) {
        speech_consumed = 0;
      } else if (speech_consumed > (int)words.count) {
        speech_consumed = (int)words.count;
      }
    }
    speech_words = words;
    speech_words_at = now();
    speech_words_settled = false;
    // The end of it, since that's where anything new is.
    NSString* text = [words componentsJoinedByString:@" "].lowercaseString;
    const char* tail = text.UTF8String ?: "";
    size_t len = strlen(tail), room = sizeof(speech_heard_text) - 1;
    if (len > room) tail += len - room;
    LOCK();
    snprintf(speech_heard_text, sizeof(speech_heard_text), "%s", tail);
    UNLOCK();
    printf("words: %s\n", tail);
    fflush(stdout);
  }
  speech_read(result.isFinal);
  if ((int)words.count >= SPEECH_MAX_WORDS) speech_end_task();
}

static void speech_start_task(void) {
  SFSpeechAudioBufferRecognitionRequest* request =
    [[SFSpeechAudioBufferRecognitionRequest alloc] init];
  request.shouldReportPartialResults = YES;
  // "Short commands", which is what these are.
  request.taskHint = SFSpeechRecognitionTaskHintConfirmation;
  request.contextualStrings = speech_hints;
  if (speech_recognizer.supportsOnDeviceRecognition) {
    request.requiresOnDeviceRecognition = YES;
  }
  if (@available(macOS 14, *)) {
    // A custom model is on-device only, which the line above asks for.
    if (speech_dictionary) request.customizedLanguageModel = speech_dictionary;
  }
  if (@available(macOS 13, *)) request.addsPunctuation = NO;

  speech_request = request;
  speech_consumed = 0;
  speech_task_started = now();
  speech_task_serial++;
  speech_task_mic_start = speech_mic_samples;
  speech_task = [speech_recognizer
    recognitionTaskWithRequest:request
                 resultHandler:^(SFSpeechRecognitionResult* result,
                                 NSError* error) {
      if (request != speech_request) return;  // from a task since replaced
      if (result) speech_heard(result);
      // Cancelling a task reports an error too, but that's always from one
      // already replaced, so anything that gets here is real.
      if (error) {
        char state[160];
        snprintf(state, sizeof(state), "error: %s",
                 error.localizedDescription.UTF8String ?: "?");
        printf("speech %s\n", state);
        fflush(stdout);
        speech_set_state(state);
        speech_error_at = now();
      }
      // The next tick starts another, if it's still wanted.
      if (error || result.isFinal) speech_end_task();
    }];
}

// Hand whatever the microphone has sent since last time to the recognizer,
// or throw it away if nothing's listening.
static void speech_drain(void) {
  unsigned read = atomic_load_explicit(&speech_ring_read,
                                       memory_order_relaxed);
  unsigned write = atomic_load_explicit(&speech_ring_write,
                                        memory_order_acquire);
  unsigned available = write - read;
  if (available == 0) return;

  static float* in = NULL;
  static float* out = NULL;
  static unsigned room = 0;
  if (available + (unsigned)speech_gate.window > room) {
    room = available + (unsigned)speech_gate.window + 4096;
    in = realloc(in, room * sizeof(float));
    out = realloc(out, room * sizeof(float));
  }
  float peak = 0;
  for (unsigned i = 0; i < available; i++) {
    in[i] = speech_ring[(read + i) & (SPEECH_RING_FRAMES - 1)];
    float v = fabsf(in[i]);
    if (v > peak) peak = v;
  }
  atomic_store_explicit(&speech_ring_read, write, memory_order_release);
  if (speech_record_hook) speech_record_hook(in, (int)available);
  for (unsigned i = 0; i < available; i++) {
    speech_mic[(speech_mic_samples + i) & speech_mic_mask] = in[i];
  }

  LOCK();
  if (peak > speech_level) speech_level = peak;
  speech_gate.threshold = (float)pow(10, speech_gate_db / 20);
  speech_fast_stream->p.trigger_db = (float)speech_gate_db;
  UNLOCK();
  LOCK();
  bool numbers = speech_chooses_notes;
  UNLOCK();
  // With or without a model, for the utterances it finds: Apple's numbers
  // land from their onsets.
  if (numbers) {
    speech_fast_offset = speech_mic_samples - speech_fast_pushed;
    nr_stream_push(speech_fast_stream, in, (int)available);
    speech_fast_pushed += available;
  }
  speech_mic_samples += available;
  nh_tick();

  long long loud_before = speech_gate.last_loud;
  int n_out = sg_process(&speech_gate, in, (int)available, out);
  bool open = sg_open(&speech_gate);
  if (speech_gate.last_loud != loud_before) {
    // When that loud window actually was: how far back it is from the end
    // of what just came in.
    uint64_t back_frames =
      (uint64_t)sg_windows_since_loud(&speech_gate) * speech_gate.window +
      (uint64_t)speech_gate.partial_n;
    speech_last_loud_ns =
      now() - back_frames * NS_PER_SEC / (uint64_t)speech_format.sampleRate;
  }
  LOCK();
  speech_gate_open = open;
  UNLOCK();

  if (speech_request && n_out > 0) {
    AVAudioPCMBuffer* buffer =
      [[AVAudioPCMBuffer alloc] initWithPCMFormat:speech_format
                                    frameCapacity:(AVAudioFrameCount)n_out];
    memcpy(buffer.floatChannelData[0], out, sizeof(float) * (size_t)n_out);
    buffer.frameLength = (AVAudioFrameCount)n_out;
    [speech_request appendAudioPCMBuffer:buffer];
  }
}

static void speech_tick(void) {
  speech_land_due();
  // The pedals have stopped: the changes before are over (speech_recent).
  bool pedals;
  speech_beat_now(&pedals, NULL, NULL);
  if (!pedals) {
    LOCK();
    speech_recent_clear(&speech_recent);
    UNLOCK();
  }

  LOCK();
  bool wanted = (speech_chooses_notes || speech_commands_on) &&
                whistle_available;
  bool numbers = speech_chooses_notes, commands = speech_commands_on;
  bool switched_on = numbers || commands;
  if (!numbers) speech_n_waiting = 0;  // F3 off: nothing's to land
  UNLOCK();

  // Asked at launch, so the prompt doesn't interrupt the first time F3 or F8
  // is pressed mid-song.
  if (!speech_asked) {
    speech_asked = true;
    [SFSpeechRecognizer requestAuthorization:^(
        SFSpeechRecognizerAuthorizationStatus status) {
      dispatch_async(speech_queue, ^{
        speech_authorized =
          status == SFSpeechRecognizerAuthorizationStatusAuthorized;
        printf("speech recognition %s\n",
               speech_authorized ? "authorized"
                                 : "not authorized; numbers won't be heard");
      });
    }];
  }
  bool on = wanted;
  wanted = wanted && speech_authorized && speech_recognizer.isAvailable;

  // Only what's worth knowing mid-song: what's keeping it from hearing you.
  // Which modes are on is on the NR and SR keys, and the details of the
  // dictionary and the fast recognizer go to stdout.
  if (!on) {
    speech_set_state(switched_on ? "no microphone" : "");
  } else if (!speech_authorized) {
    speech_set_state(speech_asked
      ? "not authorized: System Settings > Privacy & Security > Speech "
        "Recognition" : "asking permission");
  } else if (!speech_recognizer.isAvailable) {
    speech_set_state("recognizer unavailable");
  } else if (speech_task && now() - speech_error_at > 5 * NS_PER_SEC) {
    speech_set_state(speech_dictionary ? "listening"
      : strncmp(speech_dictionary_state, "loading", 7) == 0
        ? "listening: loading" : "listening: no dict");
  }

  atomic_store_explicit(&speech_listening, wanted ? 1 : 0,
                        memory_order_relaxed);
  if (!wanted) {
    if (speech_task) speech_end_task();
    speech_drain();  // to nowhere
    return;
  }

  // Nothing new for a moment: whatever was being said has been said.
  if (speech_words && !speech_words_settled &&
      now() - speech_words_at > SPEECH_SETTLE_MS * 1000000LL) {
    speech_words_settled = true;
    speech_read(/*settled=*/true);
  }

  if (speech_task &&
      now() - speech_task_started > SPEECH_TASK_MAX_S * NS_PER_SEC) {
    speech_end_task();
  }
  if (!speech_task) speech_start_task();
  speech_drain();
}

// Phrases it should expect, as hint words: the numbers, the modes, and the
// buttons' names, as far as they go -- Apple takes no more than a hundred,
// and the buttons alone are past that now.  The dictionary
// (speech_prepare_dictionary) has every one of them; this is only for when
// it can't be had.
static NSArray<NSString*>* speech_make_hints(void) {
  NSMutableArray<NSString*>* hints = [NSMutableArray arrayWithArray:@[
    @"one", @"two", @"three", @"four", @"five", @"six", @"seven",
    @"major", @"minor", @"mixolydian", @"freygish",
  ]];
  static char phrases[256][48];
  int n = all_spoken_phrases(phrases, 256);
  for (int i = 0; i < n && hints.count < 100; i++) {
    [hints addObject:@(phrases[i])];
  }
  return hints;
}

// Where the dictionary's training data is: in the app's Resources, or next to
// a bare jammer-mac, or where it's run from.  Built by `make speech-model.bin`.
static NSString* speech_model_path(void) {
  NSFileManager* files = NSFileManager.defaultManager;
  NSString* bundled = [NSBundle.mainBundle pathForResource:@"speech-model"
                                                    ofType:@"bin"];
  if (bundled) return bundled;
  NSString* beside = [[NSBundle.mainBundle.executablePath
    stringByDeletingLastPathComponent]
    stringByAppendingPathComponent:@"speech-model.bin"];
  if ([files fileExistsAtPath:beside]) return beside;
  if ([files fileExistsAtPath:@"speech-model.bin"]) return @"speech-model.bin";
  return nil;
}

// Compiling the dictionary: who does it, and where it goes.
//
// macOS compiles the training data (speech-model.bin) into a language model
// on the Mac that uses it, the first time and whenever it changes -- a
// directory named by a hash of the data, with the compiled model in it.
// As of macOS 27.2 the speech service will only do that for a process in
// the App Sandbox ("Caller lacks write access to outDirectory", in its log,
// for any directory at all), and jammer isn't one: it reads the soundfont
// from wherever it is, shares memory with VSL Mixer, and so on.  So the
// compiling is done by speechcompile, a small sandboxed helper beside the
// executable (speechcompile.swift).  A sandboxed process can only read and
// write its own container, so jammer puts the training data there, under
// the helper's Library/Caches, and reads the model back from beside it;
// nothing stops jammer reading a container.  Without the helper -- a build
// without it -- it's compiled in-process, into jammer's own caches, which
// is all it took before 27.2.

// The helper beside the executable, or nil.
static NSString* speech_helper_path(void) {
  NSString* dir =
    NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent;
  NSString* path = [dir stringByAppendingPathComponent:@"speechcompile"];
  return [NSFileManager.defaultManager isExecutableFileAtPath:path] ? path
                                                                    : nil;
}

// Run the helper with these arguments, and give back what it printed,
// trimmed, or nil with *why set if it failed.  Waits for it: not for the
// speech queue.
static NSString* speech_run_helper(NSString* helper, NSArray<NSString*>* args,
                                   NSString** why) {
  NSTask* task = [NSTask new];
  task.executableURL = [NSURL fileURLWithPath:helper];
  task.arguments = args;
  NSPipe* pipe = [NSPipe pipe];
  task.standardOutput = pipe;
  task.standardError = pipe;
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) {
    if (why) *why = error.localizedDescription ?: @"couldn't run speechcompile";
    return nil;
  }
  NSData* data = [pipe.fileHandleForReading readDataToEndOfFile];
  [task waitUntilExit];
  NSString* text = [[[NSString alloc] initWithData:data
                                          encoding:NSUTF8StringEncoding]
    stringByTrimmingCharactersInSet:
      NSCharacterSet.whitespaceAndNewlineCharacterSet] ?: @"";
  if (task.terminationStatus != 0) {
    if (why) *why = text.length ? text : @"speechcompile failed";
    return nil;
  }
  return text;
}

// Whether a compiled model is at `dir`.
static bool speech_dictionary_at(NSURL* dir) {
  NSFileManager* files = NSFileManager.defaultManager;
  return [files fileExistsAtPath:[dir URLByAppendingPathComponent:@"lm"].path] &&
    [files fileExistsAtPath:[dir URLByAppendingPathComponent:@"vocab"].path];
}

// The compiled model at `dir` is the dictionary from now on.  Speech queue.
static void speech_dictionary_ready(NSURL* dir) API_AVAILABLE(macos(14)) {
  speech_dictionary = [[SFSpeechLanguageModelConfiguration alloc]
    initWithLanguageModel:[dir URLByAppendingPathComponent:@"lm"]
               vocabulary:[dir URLByAppendingPathComponent:@"vocab"]];
  snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
           "with dictionary");
  if (speech_task) speech_end_task();  // restart with it
  printf("speech: %s, from %s\n", speech_dictionary_state, dir.path.UTF8String);
  fflush(stdout);
}

static void speech_dictionary_failed(const char* why) {
  snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
           "no dictionary: %s", why);
  printf("speech: %s\n", speech_dictionary_state);
  fflush(stdout);
}

// Compile the dictionary, in the background, and use it once it's ready.
static void speech_prepare_dictionary(void) {
  if (@available(macOS 14, *)) {
    NSString* path = speech_model_path();
    if (!path) {
      snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
               "no dictionary (make speech-model.bin)");
      return;
    }
    NSData* training = [NSData dataWithContentsOfFile:path];
    uint64_t hash = 1469598103934665603ULL;
    const uint8_t* bytes = training.bytes;
    for (NSUInteger i = 0; i < training.length; i++) {
      hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    NSString* name = [NSString stringWithFormat:
      @"com.jefftk.jammer/speech-%016llx", (unsigned long long)hash];
    snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
             "loading dictionary");

    NSString* helper = speech_helper_path();
    if (helper) {
      dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSString* why = nil;
        NSString* home = speech_run_helper(helper, @[@"--home"], &why);
        NSURL* dir = nil;
        bool ok = false;
        if (home) {
          dir = [NSURL fileURLWithPath:[[home
            stringByAppendingPathComponent:@"Library/Caches"]
            stringByAppendingPathComponent:name]];
          ok = speech_dictionary_at(dir);
          if (!ok) {
            [NSFileManager.defaultManager createDirectoryAtURL:dir
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:nil];
            [training writeToURL:[dir URLByAppendingPathComponent:@"model.bin"]
                      atomically:YES];
            ok = speech_run_helper(helper, @[dir.path], &why) != nil &&
                 speech_dictionary_at(dir);
            if (!ok && !why) why = @"speechcompile left no model";
          }
        }
        dispatch_async(speech_queue, ^{
          if (ok) speech_dictionary_ready(dir);
          else speech_dictionary_failed(why.UTF8String ?: "?");
        });
      });
      return;
    }

    // No helper: in-process, which works before macOS 27.2.
    NSURL* caches = [[NSFileManager.defaultManager
      URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask]
      firstObject];
    NSURL* dir = [caches URLByAppendingPathComponent:name];
    if (speech_dictionary_at(dir)) {
      speech_dictionary_ready(dir);
      return;
    }
    [NSFileManager.defaultManager createDirectoryAtURL:dir
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
    SFSpeechLanguageModelConfiguration* config =
      [[SFSpeechLanguageModelConfiguration alloc]
        initWithLanguageModel:[dir URLByAppendingPathComponent:@"lm"]
                   vocabulary:[dir URLByAppendingPathComponent:@"vocab"]];
    [SFSpeechLanguageModel
      prepareCustomLanguageModelForUrl:[NSURL fileURLWithPath:path]
                         configuration:config
                            completion:^(NSError* error) {
      dispatch_async(speech_queue, ^{
        if (error) {
          speech_dictionary_failed(error.localizedDescription.UTF8String
                                   ?: "?");
        } else {
          speech_dictionary_ready(dir);
        }
      });
    }];
  } else {
    snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
             "no dictionary (needs macOS 14)");
  }
}

// Called once at startup, after the synth is running.
static void speech_start(double sample_rate) {
  speech_queue = dispatch_queue_create("jammer.speech",
                                       DISPATCH_QUEUE_SERIAL);
  speech_recognizer = [[SFSpeechRecognizer alloc]
    initWithLocale:[NSLocale localeWithLocaleIdentifier:@"en-US"]];
  NSOperationQueue* results = [[NSOperationQueue alloc] init];
  results.underlyingQueue = speech_queue;
  results.maxConcurrentOperationCount = 1;
  speech_recognizer.queue = results;
  speech_format = [[AVAudioFormat alloc]
    initWithCommonFormat:AVAudioPCMFormatFloat32
              sampleRate:sample_rate
                channels:1
             interleaved:NO];
  sg_init(&speech_gate, sample_rate);
  long long mic = 1;
  while (mic < (long long)(12 * sample_rate)) mic <<= 1;
  speech_mic = calloc((size_t)mic, sizeof(float));
  speech_mic_mask = mic - 1;
  speech_fast_stream = malloc(sizeof(NrStream));
  // Asked from NR_PAD frames of quiet rather than `hangover`: the utterance
  // is whole by then, padding and all, so it's matched a frame before any
  // word's wait can be up (the waits were set with `hangover` itself) and
  // the matching is done in time for it.  See speech_fast_utterance.
  NrParams stream_params = nr_default_params(SPEECH_GATE_DEFAULT_DB);
  stream_params.hangover = NR_PAD;
  nr_stream_init(speech_fast_stream, sample_rate, stream_params,
                 speech_fast_utterance, NULL);
  speech_fast_stream->on_overlong = speech_fast_overlong;
  speech_fast_learn();
  whistle_input_tap = speech_tap;
  before_beat_hook = speech_before_beat;
  speech_hints = speech_make_hints();
  dispatch_async(speech_queue, ^{ speech_prepare_dictionary(); });

  speech_timer =
    dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, speech_queue);
  dispatch_source_set_timer(speech_timer, DISPATCH_TIME_NOW,
                            SPEECH_TICK_MS * NSEC_PER_MSEC,
                            5 * NSEC_PER_MSEC);
  dispatch_source_set_event_handler(speech_timer, ^{ speech_tick(); });
  dispatch_resume(speech_timer);
}

#endif
