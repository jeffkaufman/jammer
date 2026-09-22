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
// gate in speechgate.h, set from the Speech Recognition menu), and what it
// hears takes effect two beats after the talking stops, so it lands in time
// however long the recognizer took.
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
// How long without a new word before "change key to B" is taken to mean B
// rather than the start of "B flat".  Longer than it looks like it needs to
// be: the recognizer's words can arrive the better part of a second apart.
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

static char speech_state[160] = "off";  // why it is or isn't listening
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

// Do it.  Caller must hold the lock -- this runs inside the beat, just before
// its notes, so that they're the new ones.
static void speech_apply_locked(const SwAction* action) {
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
// On the beat
//
// What's heard doesn't happen when the recognizer gets round to it, which
// could be anywhere from a quarter of a second to a second later, but on the
// beat two beats after the talking stopped: say "four" ending on the one and
// the IV comes in on the three.
//
// On the beat itself, not near it.  The beat here is the pedal: each hit is
// when the rhythm parts play, so a change that arrives a few milliseconds
// after it is heard only on the next note, half a beat or more late.  So a
// change is armed half a beat before it's due and then made by the next beat,
// inside it, before its notes (before_beat_hook) -- which is to say a hair
// ahead of them.
//
// Which beat is decided on the pedals' grid, not the voice's: the change is
// due two pedal beats after the beat the call ended on.  That's the last beat
// at or before the end of the talking -- not the nearest -- since the end as
// measured here, the last moment above the gate, comes 140-320ms *after* the
// beat a call is ended on: the word is placed on the beat and rings on past
// it.  At 120 BPM that's around the middle of the beat, where "nearest" was a
// coin toss between landing two beats later and three.  A call that ends a
// little early, up to SPEECH_EARLY_FRACTION of a beat, still counts.
//
// If the feet have stopped -- no pedal hit in the last beat and a half --
// there's no beat to land on, so it's made the moment it's heard, talking or
// not.  If they stop in the half beat after it's armed, it's made then.
// ---------------------------------------------------------------------------

#define SPEECH_BEATS_AFTER 2
#define SPEECH_DEFAULT_BPM 116
#define SPEECH_MAX_PENDING 16
#define SPEECH_EARLY_FRACTION 0.15

// Heard, waiting to be armed.  Speech queue only.
static SwAction speech_pending[SPEECH_MAX_PENDING];
static int speech_n_pending;
static uint64_t speech_last_loud_ns;  // when the talking last stopped
// When a change heard too late for its beat has been moved on to a later
// one, that beat, so it stays moved.  0 otherwise.
static uint64_t speech_moved_due;
static uint64_t speech_scheduled_for; // a precise wakeup already asked for

// Armed, for the next beat to make.  Under the lock.
static SwAction speech_armed[SPEECH_MAX_PENDING];
static int speech_n_armed;
static uint64_t speech_armed_due;       // the beat it's meant for, roughly
static uint64_t speech_armed_deadline;  // made anyway if no beat by then

// The next beat's notes are about to play: make whatever's armed first.
// Called by arpeggiate with the lock held.
static int speech_fast_armed;  // a fast number for the next beat, or 0
static void speech_before_beat(void) {
  if (speech_fast_armed) {
    SwAction action = {SW_NUMBER, speech_fast_armed};
    printf("timing: fast number made on the beat\n");
    speech_apply_locked(&action);
    speech_fast_armed = 0;
  }
  if (speech_n_armed == 0) return;
  printf("timing: made on the beat, %ldms from when it was due\n",
         (long)(((int64_t)now() - (int64_t)speech_armed_due) / 1000000));
  for (int i = 0; i < speech_n_armed; i++) {
    speech_apply_locked(&speech_armed[i]);
  }
  speech_n_armed = 0;
}

static uint64_t speech_beat_ns(bool* known) {
  LOCK();
  uint64_t beat = current_beat_ns;
  UNLOCK();
  if (known) *known = beat != 0;
  return beat ? beat : 60 * NS_PER_SEC / SPEECH_DEFAULT_BPM;
}

// The beat a change is for: two pedal beats after the one nearest the end of
// the talking, on the grid the last pedal hit and the tempo make.  Without a
// grid, two default beats after the talking stopped.  *feet_going if pedal
// hits are still coming, so there'll be a real beat to make it in.
static uint64_t speech_due_beat(bool* feet_going) {
  bool known;
  uint64_t beat = speech_beat_ns(&known);
  LOCK();
  uint64_t last = last_downbeat_ns;
  UNLOCK();
  bool grid = known && last > 0;
  *feet_going = grid && now() - last < beat * 3 / 2;
  if (!grid) return speech_last_loud_ns + SPEECH_BEATS_AFTER * beat;
  int64_t from_last = (int64_t)speech_last_loud_ns - (int64_t)last;
  int64_t k = (int64_t)floor(
    ((double)from_last + SPEECH_EARLY_FRACTION * (double)beat) /
    (double)beat);
  return (uint64_t)((int64_t)last + (k + SPEECH_BEATS_AFTER) * (int64_t)beat);
}

// Milliseconds since the talking stopped, for the timing in the log.
static long speech_ms_since_stop(void) {
  return (long)(((int64_t)now() - (int64_t)speech_last_loud_ns) / 1000000);
}

// Ask to be woken at `when`, if that's sooner than the next tick would.
static void speech_wake_at(uint64_t when, void (*fn)(void)) {
  uint64_t t = now();
  if (when <= t || when - t >= (SPEECH_TICK_MS + 10) * 1000000ULL ||
      speech_scheduled_for == when) {
    return;
  }
  speech_scheduled_for = when;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(when - t)),
                 speech_queue, ^{ fn(); });
}

static uint64_t speech_fast_deadline;  // made anyway if no beat by then

static void speech_run_pending(void) {
  // An armed change whose beat never came.
  LOCK();
  if (speech_fast_armed && now() >= speech_fast_deadline) {
    printf("timing: the feet stopped before its beat; fast number made "
           "anyway\n");
    SwAction action = {SW_NUMBER, speech_fast_armed};
    speech_apply_locked(&action);
    speech_fast_armed = 0;
  }
  if (speech_n_armed && now() >= speech_armed_deadline) {
    printf("timing: the feet stopped before its beat; made anyway\n");
    for (int i = 0; i < speech_n_armed; i++) {
      speech_apply_locked(&speech_armed[i]);
    }
    speech_n_armed = 0;
  }
  UNLOCK();

  if (speech_n_pending == 0) return;

  // No beat to land on: now.
  bool feet;
  speech_due_beat(&feet);
  if (!feet) {
    printf("timing: no beat; made %ldms after you stopped\n",
           speech_ms_since_stop());
    LOCK();
    for (int i = 0; i < speech_n_pending; i++) {
      speech_apply_locked(&speech_pending[i]);
    }
    UNLOCK();
    speech_n_pending = 0;
    speech_moved_due = 0;
    return;
  }

  if (speech_gate_open) return;  // still talking
  bool known;
  uint64_t beat = speech_beat_ns(&known);
  bool on_grid;
  uint64_t due = speech_moved_due ? speech_moved_due
                                  : speech_due_beat(&on_grid);
  if (speech_moved_due) speech_due_beat(&on_grid);  // just for on_grid

  // Heard too late for its beat: the next beat instead, so that a late change
  // is a beat late but still on a beat rather than somewhere between two.
  // With feet, the beat has gone if a pedal hit has come near it; without,
  // if its moment has passed.
  LOCK();
  uint64_t last_hit = last_downbeat_ns;
  UNLOCK();
  bool missed = on_grid ? last_hit + beat / 2 >= due
                        : now() + 1000000 > due;
  if (missed) {
    uint64_t moved = due;
    if (on_grid) {
      moved = last_hit + beat;
    } else {
      while (moved <= now() + 1000000) moved += beat;
    }
    printf("timing: heard %ldms after its beat; moved to the next one\n",
           (long)(((int64_t)now() - (int64_t)due) / 1000000));
    speech_moved_due = due = moved;
  }

  // With a beat coming to land on, hand over to it from half a beat before;
  // without one, a millisecond before the moment itself.
  uint64_t hand_over = on_grid ? due - beat / 2 : due - 1000000;
  if (now() < hand_over) {
    speech_wake_at(hand_over, speech_run_pending);
    return;
  }

  LOCK();
  if (on_grid) {
    for (int i = 0; i < speech_n_pending && speech_n_armed < SPEECH_MAX_PENDING;
         i++) {
      speech_armed[speech_n_armed++] = speech_pending[i];
    }
    speech_armed_due = due;
    speech_armed_deadline = due + beat / 2;
  } else {
    printf("timing: no feet; made on a timer, %ldms from the beat\n",
           (long)(((int64_t)now() - (int64_t)due) / 1000000));
    for (int i = 0; i < speech_n_pending; i++) {
      speech_apply_locked(&speech_pending[i]);
    }
  }
  UNLOCK();
  speech_n_pending = 0;
  speech_moved_due = 0;
}

static bool speech_fast_claims(const SwAction* action);

// Heard, and to happen on the beat two beats after the talking stops.
static void speech_queue_action(const SwAction* action) {
  if (speech_fast_claims(action)) return;
  // Numbers only with F3, everything else only with F8.
  LOCK();
  bool wanted = action->kind == SW_NUMBER ? speech_chooses_notes
                                          : speech_commands_on;
  UNLOCK();
  if (!wanted) return;
  if (speech_n_pending < SPEECH_MAX_PENDING) {
    speech_pending[speech_n_pending++] = *action;
  }
  char text[64];
  LOCK();
  speech_describe_locked(action, text, sizeof(text));
  snprintf(speech_last_action, sizeof(speech_last_action), "%s …", text);
  UNLOCK();
  bool known;
  uint64_t beat = speech_beat_ns(&known);
  bool feet;
  uint64_t due = speech_due_beat(&feet);
  printf("timing: recognized %s %ldms after you stopped; beat %ldms "
         "(%.0f bpm%s), due on the beat at %ldms%s%s\n", text,
         speech_ms_since_stop(), (long)(beat / 1000000),
         60.0 * NS_PER_SEC / beat, known ? "" : ", default",
         (long)(((int64_t)due - (int64_t)speech_last_loud_ns) / 1000000),
         feet ? "" : ", no feet", speech_gate_open ? "; still talking" : "");
  // Whether one beat would have been enough: recognized before the beat after
  // the one the call ended on.
  int64_t one_beat = (int64_t)due - (SPEECH_BEATS_AFTER - 1) * (int64_t)beat;
  printf("timing: %ldms after the beat you ended on; one beat would have "
         "%s\n",
         (long)(((int64_t)now() - (one_beat - (int64_t)beat)) / 1000000),
         (int64_t)now() < one_beat ? "made it" : "been too late");
  fflush(stdout);
  speech_run_pending();  // in case it's already due
}

// ---------------------------------------------------------------------------
// The fast path
//
// numrec.h hears a bare number within about a tenth of a second of the word
// ending, having learned your voice from numtrain.h's recordings.  It's fed
// the same microphone as Apple's recognizer, before the gate -- it has its
// own, set from the same menu -- and only while number recognition (F3) is
// on.
//
// What it hears goes in on the very next beat, the soonest it can be heard,
// since the rhythm parts only play on beats; or, with the feet stopped, at
// once.  Apple's recognizer hears the same word a while later: a number from
// it within SPEECH_FAST_CLAIM_S of one the fast path took is that word again,
// and is dropped.  A number the fast path wasn't sure of it leaves to Apple.
// ---------------------------------------------------------------------------

#define SPEECH_FAST_CLAIM_S 3

// numheard.h, which keeps the audio when the two recognizers disagree.
static void nh_fast_judged(const NrStream* s, long long onset, long long end,
                           int number);
static void nh_apple_word(int index, const char* text, int number);
static void nh_tick(void);
static bool nh_unreviewed(NSString* tsv);

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

static bool speech_fast_take(int number, int quiet_ms) {
  LOCK();
  bool numbers = speech_chooses_notes;
  UNLOCK();
  if (!numbers) return false;  // F3 just went off
  speech_fast_unclaimed++;
  speech_fast_at = now();
  bool feet;
  speech_due_beat(&feet);
  bool known;
  uint64_t beat = speech_beat_ns(&known);
  SwAction action = {SW_NUMBER, number};
  LOCK();
  snprintf(speech_heard_text, sizeof(speech_heard_text), "%d (fast)",
           number);
  if (feet) {
    speech_fast_armed = number;
    speech_fast_deadline = now() + beat * 3 / 2;
    snprintf(speech_last_action, sizeof(speech_last_action), "%d …",
             number);
  } else {
    speech_apply_locked(&action);
  }
  UNLOCK();
  printf("fast: heard %d, %dms after the word ended%s\n", number, quiet_ms,
         feet ? "; on the next beat" : "; no beat, made now");
  fflush(stdout);
  return true;
}

static bool speech_fast_utterance(void* ctx, NrStream* s, long long onset,
                                  long long end, bool clear, int quiet,
                                  bool final) {
  (void)ctx;
  if (!speech_fast_model || !clear) return true;
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
    // A tick's worth on top: that's how long the audio waited in the ring.
    if (speech_fast_take(r.label + 1, quiet * 10 + SPEECH_TICK_MS)) {
      nh_fast_judged(s, onset, end, r.label + 1);
    }
    return true;
  }
  if (final) nh_fast_judged(s, onset, end, 0);
  if (final && r.nearest >= 0 && r.nearest != NR_OTHER) {
    printf("fast: maybe %s (%.2f, runner-up %.2f), but %s; leaving it to "
           "apple\n", NR_WORDS[r.nearest], r.dist, r.runner_up,
           r.why ? r.why : "?");
    fflush(stdout);
  }
  return final;
}

// Learn from every recording numtrain.h has made, off the speech queue since
// it reads them all, then hand the result over.  At startup, and again after
// each new recording.
static void speech_fast_learn(void) {
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    NSURL* support = [[NSFileManager.defaultManager
      URLsForDirectory:NSApplicationSupportDirectory
             inDomains:NSUserDomainMask] firstObject];
    NSURL* dir =
      [support URLByAppendingPathComponent:@"net.jefftk.jammer/numbers"];
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
    int session = 0, clips = 0;
    for (NSURL* file in files) {
      if (![file.pathExtension isEqualToString:@"wav"]) continue;
      NSURL* tsv =
        [file.URLByDeletingPathExtension URLByAppendingPathExtension:@"tsv"];
      bool clip = [file.lastPathComponent hasPrefix:@"heard-"];
      if (clip && nh_unreviewed([NSString stringWithContentsOfURL:tsv
                                   encoding:NSUTF8StringEncoding
                                      error:nil] ?: @"")) {
        continue;
      }
      long long n;
      double rate;
      float* x = nr_read_wav(file.path.UTF8String, &n, &rate);
      NrPrompt* prompts = NULL;
      float gate_db = SPEECH_GATE_DEFAULT_DB, room_db = p.room_db;
      int n_prompts = nr_read_prompts(tsv.path.UTF8String, &prompts, &gate_db,
                                      &room_db);
      if (x && n_prompts > 0) {
        // A little below the gate it was recorded with, so the quieter
        // words are learned too: which prompt was up says what they were.
        NrParams sp = p;
        sp.trigger_db = gate_db - 6;
        sp.room_db = room_db;
        nr_learn_session(model, x, n, rate, prompts, n_prompts, &sp,
                         session++);
        if (clip) clips++;
      }
      free(x);
      free(prompts);
    }
    int numbers = 0;
    for (int i = 0; i < model->n; i++) {
      numbers += model->t[i].label != NR_OTHER;
    }
    nr_model_finish(model, &p);
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
    speech_queue_action(&action);
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
  if (numbers && speech_fast_model) {
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
  speech_run_pending();

  LOCK();
  bool wanted = (speech_chooses_notes || speech_commands_on) &&
                whistle_available;
  bool numbers = speech_chooses_notes, commands = speech_commands_on;
  UNLOCK();

  // Asked for the first time the mode is switched on, not at launch, so
  // nobody who never uses it is ever prompted.
  if (wanted && !speech_asked) {
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

  if (!on) {
    speech_set_state(whistle_available
                       ? "off: F3 for numbers, F8 for commands"
                       : "off: no whistle microphone");
  } else if (!speech_authorized) {
    speech_set_state(speech_asked
      ? "not authorized: System Settings > Privacy & Security > Speech "
        "Recognition" : "asking permission");
  } else if (!speech_recognizer.isAvailable) {
    speech_set_state("recognizer unavailable");
  } else if (speech_task && now() - speech_error_at > 5 * NS_PER_SEC) {
    char state[160];
    snprintf(state, sizeof(state), "listening for %s, %s%s%s",
             numbers && commands ? "numbers and commands"
               : numbers ? "numbers" : "commands",
             speech_dictionary_state, numbers ? ", " : "",
             numbers ? speech_fast_state : "");
    speech_set_state(state);
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

// Every phrase it should expect, as hint words: the numbers, the modes, and
// every button's name.  Apple asks for no more than a hundred, which this
// fits, just.
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

// Compile the dictionary, in the background, into the caches directory under
// a name that changes when the training data does.  Once it's ready the
// running task is ended, so the next one starts with it.
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
    NSURL* caches = [[NSFileManager.defaultManager
      URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask]
      firstObject];
    NSURL* dir = [caches URLByAppendingPathComponent:
      [NSString stringWithFormat:@"net.jefftk.jammer/speech-%016llx",
                                 (unsigned long long)hash]];
    [NSFileManager.defaultManager createDirectoryAtURL:dir
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
    SFSpeechLanguageModelConfiguration* config =
      [[SFSpeechLanguageModelConfiguration alloc]
        initWithLanguageModel:[dir URLByAppendingPathComponent:@"lm"]
                   vocabulary:[dir URLByAppendingPathComponent:@"vocab"]];
    snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
             "loading dictionary");
    [SFSpeechLanguageModel
      prepareCustomLanguageModelForUrl:[NSURL fileURLWithPath:path]
                         configuration:config
                            completion:^(NSError* error) {
      dispatch_async(speech_queue, ^{
        if (error) {
          snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
                   "no dictionary: %s",
                   error.localizedDescription.UTF8String ?: "?");
        } else {
          speech_dictionary = config;
          snprintf(speech_dictionary_state, sizeof(speech_dictionary_state),
                   "with dictionary");
          if (speech_task) speech_end_task();  // restart with it
        }
        printf("speech: %s\n", speech_dictionary_state);
        fflush(stdout);
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
