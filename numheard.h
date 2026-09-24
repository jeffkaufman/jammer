#ifndef JML_NUMHEARD_H
#define JML_NUMHEARD_H

// Learning from the rig in use.  When Apple's recognizer and the fast one
// (numrec.h) disagree about whether something said was a number -- the fast
// path took "four" and Apple heard "for the", or the fast path let something
// go that Apple heard as "six" -- the audio is kept, labeled with what Apple
// heard, for the fast one to learn from:
//
//   ~/Library/Application Support/com.jefftk.jammer/numbers/heard/
//     heard-YYYYMMDD-HHMMSS-mmm.wav   the raw microphone, as numtrain.h's
//     heard-YYYYMMDD-HHMMSS-mmm.tsv   one prompt, at 0: what Apple heard
//
// A clip is laid out as a numtrain.h session with one prompt up throughout,
// so nr_learn_session learns it as it is, and numrec-eval can be pointed at
// the directory.  The prompt is the number Apple heard, or "other: " and
// its words; the .tsv also says what the fast path made of it.
//
// They're learned from, with the sessions, at every startup -- except where
// the fast path took a number and Apple didn't agree, which wait until you've
// said who was right (see "Reviewing" below).  Of the first six of those,
// Apple was wrong every time.  And except the near misses, where the fast path
// was nearly sure of a number but left it to Apple, and Apple didn't hear that
// number: those are kept even where Apple heard nothing, or nothing like a
// number, and wait for you too.  Where Apple wrote a word it keeps writing for
// the number the fast path took -- "next" or "thanks" for six -- that's
// taken as agreeing (nh_misheard_as), and nothing's kept.  They're kept to
// NH_MAX_BYTES in all, the oldest going first.
//
// Only what the fast path judged can disagree with it: whole utterances, with
// F3 on, after a moment of quiet.  And, but for the near misses, only where
// Apple heard something: a word it didn't write at all is no opinion either
// way.  What Apple wrote is
// matched to what the fast path heard by where on the microphone's timeline
// Apple says each word was; if it gives no timestamps, by when the words
// arrived, as belonging to the last utterance that had started by then.
//
// Nothing's kept while numtrain.h is recording, which labels its own.
//
// Speech queue, apart from the files, which are written on nh_queue so a
// slow disk never holds up listening.  Include after speech.h and
// numtrain.h, in jammer-mac.m.

#include <ctype.h>

#define NH_MAX_BYTES (2LL << 30)  // 2GB
#define NH_WAIT_S 3.5      // after a word, for Apple to say what it heard
#define NH_ARRIVAL_S 2.5   // untimed, a word arriving this late isn't it
#define NH_SLACK_S 0.15    // timed, how near a word must be to count
#define NH_PRE_S 1.0       // kept before the word, for the room
#define NH_POST_S 0.3      // and after it: past end_hangover, to end it
#define NH_NEAR 0.3         // a near miss is this far past accept, at most
#define NH_MAX_JUDGED 32
#define NH_MAX_WORDS 128
#define NH_MAX_SAID 16

// An utterance the fast path judged, waiting for Apple.  Microphone samples.
typedef struct {
  long long onset, end;
  long long start;  // where its clip starts
  int number;       // what the fast path took it as, 1-7, or 0
  int maybe;        // not taken, but nearly: what it nearly was, 1-7, or 0
  float dist;       // to the nearest recording
  float gate_db, room_db;
} NhJudged;

// A word Apple wrote, or rewrote.  Microphone samples, from `from` to `to`
// if Apple said when; otherwise `to` is -1 and `from` is when it arrived.
typedef struct {
  long long from, to;
  int task, index;  // which word of which transcript
  int number;       // 1-7 if it was acted on as a number
  char text[32];
} NhWord;

static NhJudged nh_judged[NH_MAX_JUDGED];
static int nh_n_judged;
static NhWord nh_words[NH_MAX_WORDS];  // a ring
static int nh_next_word;
static int nh_n_words;

static dispatch_queue_t nh_queue(void) {
  static dispatch_queue_t queue;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    queue = dispatch_queue_create("jammer.numheard", DISPATCH_QUEUE_SERIAL);
  });
  return queue;
}

static NSURL* nh_dir(void) {
  NSURL* support = [[NSFileManager.defaultManager
    URLsForDirectory:NSApplicationSupportDirectory
           inDomains:NSUserDomainMask] firstObject];
  return [support
    URLByAppendingPathComponent:@"com.jefftk.jammer/numbers/heard"];
}

// ---------------------------------------------------------------------------
// Keeping to NH_MAX_BYTES
// ---------------------------------------------------------------------------

static long long nh_bytes = -1;  // -1 until counted

// nh_queue.  Counts it all the first time, and again whenever it's over, when
// the oldest clips go -- a .wav and its .tsv together -- until it isn't.
static void nh_prune(void) {
  if (nh_bytes >= 0 && nh_bytes <= NH_MAX_BYTES) return;
  NSMutableDictionary<NSString*, NSMutableArray<NSURL*>*>* clips =
    [NSMutableDictionary dictionary];
  NSMutableDictionary<NSString*, NSNumber*>* sizes =
    [NSMutableDictionary dictionary];
  long long total = 0;
  for (NSURL* file in [NSFileManager.defaultManager
         contentsOfDirectoryAtURL:nh_dir()
       includingPropertiesForKeys:@[NSURLFileSizeKey]
                          options:NSDirectoryEnumerationSkipsHiddenFiles
                            error:nil]) {
    NSString* stem = file.lastPathComponent.stringByDeletingPathExtension;
    if (![stem hasPrefix:@"heard-"]) continue;
    NSNumber* size = nil;
    [file getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    if (!clips[stem]) clips[stem] = [NSMutableArray array];
    [clips[stem] addObject:file];
    sizes[stem] = @(sizes[stem].longLongValue + size.longLongValue);
    total += size.longLongValue;
  }
  // The names are when they were kept, so this is oldest first.
  int removed = 0;
  for (NSString* stem in
       [clips.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
    if (total <= NH_MAX_BYTES) break;
    for (NSURL* file in clips[stem]) {
      [NSFileManager.defaultManager removeItemAtURL:file error:nil];
    }
    total -= sizes[stem].longLongValue;
    removed++;
  }
  if (removed) {
    printf("heard: over %lldMB of clips; removed the oldest %d\n",
           NH_MAX_BYTES >> 20, removed);
    fflush(stdout);
  }
  nh_bytes = total;
}

// ---------------------------------------------------------------------------
// Keeping a clip
// ---------------------------------------------------------------------------

// Speech queue: cut it from the microphone's last few seconds, and hand it
// over to be written.  A near miss to ask about has "# maybe" in its .tsv,
// what it nearly was.
static void nh_keep(const NhJudged* c, const char* label, const char* said,
                    bool ask) {
  double rate = speech_format.sampleRate;
  long long from = c->start < 0 ? 0 : c->start;
  long long to = c->end + (long long)(NH_POST_S * rate);
  if (to > speech_mic_samples) to = speech_mic_samples;
  if (speech_mic_samples - from > speech_mic_mask + 1 || to <= from) return;
  long long n = to - from;
  float* x = malloc(sizeof(float) * (size_t)n);
  for (long long i = 0; i < n; i++) {
    x[i] = speech_mic[(from + i) & speech_mic_mask];
  }
  NSString* maybe = ask ? [NSString stringWithFormat:
    @"# maybe\t%s\n# dist\t%.2f\n", NR_WORDS[c->maybe - 1], c->dist] : @"";
  NSString* tsv = [NSString stringWithFormat:
    @"# rate\t%.0f\n# gate_db\t%.0f\n# room_db\t%.1f\n# fast\t%s\n%@"
    @"# apple\t%s\n0\t%s\n", rate, c->gate_db, c->room_db,
    c->number ? NR_WORDS[c->number - 1] : "nothing", maybe, said, label];

  dispatch_async(nh_queue(), ^{
    NSURL* dir = nh_dir();
    [NSFileManager.defaultManager createDirectoryAtURL:dir
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
    NSDateFormatter* format = [NSDateFormatter new];
    format.dateFormat = @"yyyyMMdd-HHmmss-SSS";
    NSString* base = [dir URLByAppendingPathComponent:
      [@"heard-" stringByAppendingString:
        [format stringFromDate:[NSDate date]]]].path;
    NSString* wav = [base stringByAppendingString:@".wav"];
    FILE* f = fopen(wav.UTF8String, "wb");
    if (f) {
      nt_wav_header(f, rate, n);
      fwrite(x, sizeof(float), (size_t)n, f);
      fclose(f);
      [tsv writeToFile:[base stringByAppendingString:@".tsv"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
      if (nh_bytes >= 0) {
        nh_bytes += 44 + n * 4 +
          (long long)[tsv lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
      }
      printf("heard: kept %s\n", wav.lastPathComponent.UTF8String);
    } else {
      printf("heard: can't keep %s: %s\n", wav.UTF8String, strerror(errno));
    }
    fflush(stdout);
    free(x);
    nh_prune();
  });
}

// ---------------------------------------------------------------------------
// Setting the two side by side
// ---------------------------------------------------------------------------

// The fast path took an utterance as `number`, or, with 0, as nothing, having
// matched it as `r`.  The stream's frames, put on the microphone's timeline.
static void nh_fast_judged(const NrStream* s, long long onset, long long end,
                           int number, const NrResult* r) {
  if (speech_record_hook || nh_n_judged == NH_MAX_JUDGED) return;
  double rate = speech_format.sampleRate;
  long long hop = s->f.hop;
  NhJudged* c = &nh_judged[nh_n_judged++];
  c->onset = onset * hop + speech_fast_offset;
  c->end = (end + 1) * hop + speech_fast_offset;
  // Back far enough to hear the room, but not into whatever came before.
  c->start = c->onset - (long long)(NH_PRE_S * rate);
  long long before = (s->last_end + 10) * hop + speech_fast_offset;
  if (before > c->start) c->start = before;
  long long quiet = s->p.pre_quiet * hop;
  if (c->start > c->onset - quiet) c->start = c->onset - quiet;
  c->number = number;
  // Nearly a number: too close to call between two, or only a little too far
  // from the nearest.  Of the recordings that weren't numbers, the few
  // nearest a number were 1.41 to 1.91 from it, with accept at 1.6.
  bool near = !number && r->nearest >= 0 && r->nearest != NR_OTHER &&
              r->dist <= s->p.accept + NH_NEAR;
  c->maybe = near ? r->nearest + 1 : 0;
  c->dist = r->dist;
  c->gate_db = s->p.trigger_db;
  c->room_db = s->noise;
}

// Apple wrote word `index` of the current transcript, `number` if that's
// what it was acted on as.
static void nh_apple_word(int index, const char* text, int number) {
  NhWord* w = &nh_words[nh_next_word];
  nh_next_word = (nh_next_word + 1) % NH_MAX_WORDS;
  if (nh_n_words < NH_MAX_WORDS) nh_n_words++;
  w->task = speech_task_serial;
  w->index = index;
  w->number = number;
  w->from = speech_word_from[index];
  w->to = speech_word_to[index];
  if (w->from < 0) w->from = speech_mic_samples;
  snprintf(w->text, sizeof(w->text), "%s", text);
}

// Whether Apple's word `w` is about the fast path's judged[i].
static bool nh_matches(int i, const NhWord* w) {
  double rate = speech_format.sampleRate;
  const NhJudged* c = &nh_judged[i];
  if (w->to >= 0) {
    long long slack = (long long)(NH_SLACK_S * rate);
    return w->from <= c->end + slack && w->to >= c->onset - slack;
  }
  if (w->from < c->onset ||
      w->from > c->end + (long long)(NH_ARRIVAL_S * rate)) {
    return false;
  }
  for (int j = i + 1; j < nh_n_judged; j++) {
    if (nh_judged[j].onset <= w->from) return false;  // that one's later
  }
  return true;
}

// Whether `word` is how Apple keeps writing `number`, so that the fast path
// taking that number is agreement, not something to ask about.  Of the clips
// where the fast path took six and Apple wrote "next" or "thanks", every one
// was six.  Not acted on as numbers, though (nw_number_for_word): "next" and
// "thanks" said to the room shouldn't change the chord.
static bool nh_misheard_as(const char* word, int number) {
  static const struct { int number; const char* heard; } MISHEARD[] = {
    {6, "next"}, {6, "thanks"},
  };
  char clean[SW_NAME_MAX];
  sw_normalize(word, clean, sizeof(clean));
  for (int i = 0; i < (int)(sizeof(MISHEARD) / sizeof(MISHEARD[0])); i++) {
    if (MISHEARD[i].number == number &&
        strcmp(clean, MISHEARD[i].heard) == 0) {
      return true;
    }
  }
  return false;
}

// Apple's had long enough: what did it make of judged[i]?
static void nh_resolve(int i) {
  const NhJudged* c = &nh_judged[i];
  int apple = 0;
  // Its words, the latest writing of each.
  struct { int task, index; const char* text; } said[NH_MAX_SAID];
  int n_said = 0;
  for (int k = 0; k < nh_n_words; k++) {
    const NhWord* w =
      &nh_words[(nh_next_word - nh_n_words + k + NH_MAX_WORDS) %
                NH_MAX_WORDS];
    if (!nh_matches(i, w)) continue;
    if (w->number) apple = w->number;
    int j = 0;
    while (j < n_said &&
           (said[j].task != w->task || said[j].index != w->index)) {
      j++;
    }
    if (j == NH_MAX_SAID) continue;
    if (j == n_said) n_said++;
    said[j].task = w->task;
    said[j].index = w->index;
    said[j].text = w->text;
  }
  // A near miss Apple didn't hear as that number is asked about, whatever
  // Apple made of it.
  bool ask = c->maybe && apple != c->maybe;
  if (!ask && (n_said == 0 || apple == c->number)) return;  // or agreed
  if (n_said == 1 && c->number && nh_misheard_as(said[0].text, c->number)) {
    printf("heard: fast path %s, apple \"%s\", as it often writes it\n",
           NR_WORDS[c->number - 1], said[0].text);
    fflush(stdout);
    return;
  }

  char words[128] = "";
  for (int j = 0; j < n_said; j++) {
    size_t len = strlen(words);
    snprintf(words + len, sizeof(words) - len, "%s%s", j ? " " : "",
             said[j].text);
  }
  for (char* p = words; *p; p++) *p = (char)tolower((unsigned char)*p);
  // Never a bare number word, which would be learned as that number.
  char label[64];
  snprintf(label, sizeof(label), "%s%s", apple ? "" : "other: ",
           apple ? NR_WORDS[apple - 1] : words);
  if (ask) {
    printf("heard: fast path maybe %s (%.2f), apple %s (\"%s\"); keeping "
           "it to ask about\n", NR_WORDS[c->maybe - 1], c->dist,
           apple ? NR_WORDS[apple - 1] : "not a number", words);
  } else {
    printf("heard: fast path %s, apple %s (\"%s\"); keeping it\n",
           c->number ? NR_WORDS[c->number - 1] : "nothing",
           apple ? NR_WORDS[apple - 1] : "not a number", words);
  }
  fflush(stdout);
  nh_keep(c, label, words, ask);
}

// Every drain: settle whatever Apple's had long enough to hear.
static void nh_tick(void) {
  long long wait = (long long)(NH_WAIT_S * speech_format.sampleRate);
  int kept = 0;
  for (int i = 0; i < nh_n_judged; i++) {
    if (speech_mic_samples < nh_judged[i].end + wait) continue;
    nh_resolve(i);
    nh_judged[i].onset = -1;  // done
  }
  for (int i = 0; i < nh_n_judged; i++) {
    if (nh_judged[i].onset >= 0) nh_judged[kept++] = nh_judged[i];
  }
  nh_n_judged = kept;
}

// ---------------------------------------------------------------------------
// Reviewing
//
// Speech Recognition > Review Number Clips... goes through the clips where
// the fast path took a number and Apple didn't agree, which are the ones
// where it's anybody's guess who was right: Apple writing "siri" for "three"
// or you saying "siri".  Each plays as it comes up, and you say what it was;
// that becomes its label, and "# reviewed" in its .tsv keeps it from coming
// up again.  So do the near misses Apple didn't hear as that number, which
// are as much a guess.  The other clips where Apple heard a number the fast
// path let go aren't asked about, since there the fast path was only unsure.
//
// Then the recordings that are especially unusual, and that nobody has
// reviewed: ones that sound more like a different word than like any other
// recording of their own (nr_find_unusual), from numtrain.h's sessions as
// well as the clips kept here.  A said-wrong prompt or a cough, learned as
// the word that was up.  They're found while you go through the rest, since
// it's every recording against every other.  A kept clip is relabeled as
// above; a word in a session gets a "# review" line in the session's .tsv
// (NrReview), so it's learned as what you said -- or, deleted, not at all --
// and isn't asked about again.  What's changed is learned when the window
// closes.
// ---------------------------------------------------------------------------

// Whether you've said what a clip was.
static bool nh_reviewed(NSString* tsv) {
  return [tsv containsString:@"\n# reviewed\t"];
}

// Whether a clip's .tsv is one to review, and so not to learn from yet: the
// fast path took a number, or nearly did, Apple didn't agree, and you haven't
// said.
static bool nh_unreviewed(NSString* tsv) {
  return !nh_reviewed(tsv) &&
         (![tsv containsString:@"\n# fast\tnothing\n"] ||
          [tsv containsString:@"\n# maybe\t"]);
}

// The most unusual recordings to ask about at once.
#define NH_MAX_UNUSUAL 30

// Something to ask about: a whole kept clip, or one word of a session.
@interface NhReviewItem : NSObject
@property(copy) NSString* tsv;
@property BOOL word;          // one word of a session, not a whole clip
@property long long sample;   // a word's start and end, in the recording's
@property long long end;      // samples
@property(copy) NSString* learned;  // unusual: what it's been learned as
@property(copy) NSString* nearest;  // and what it sounds most like
@end

@implementation NhReviewItem
@end

// Give the word starting at `sample` in the session at `tsv` the label
// `label`, NrReview's: a number word, "other" or "drop".
static void nh_review_word(NSString* tsv, long long sample, NSString* label) {
  NSString* text = [NSString stringWithContentsOfFile:tsv
                                             encoding:NSUTF8StringEncoding
                                                error:nil] ?: @"";
  if (text.length && ![text hasSuffix:@"\n"]) {
    text = [text stringByAppendingString:@"\n"];
  }
  text = [text stringByAppendingFormat:@"# review\t%lld\t%@\n", sample,
          label];
  [text writeToFile:tsv atomically:YES encoding:NSUTF8StringEncoding
              error:nil];
}

static NSString* nh_word_name(int label) {
  return label == NR_OTHER ? @"not a number" : @(NR_WORDS[label]);
}

// The unusual recordings nobody has reviewed, worst first.  Off the main
// thread: it learns everything, then compares every recording with every
// other.
static NSArray<NhReviewItem*>* nh_find_unusual(void) {
  NSMutableArray<NSString*>* sources = [NSMutableArray array];
  int sessions, clips;
  NrModel* model = speech_fast_learn_all(sources, &sessions, &clips);
  NrParams p = nr_default_params(SPEECH_GATE_DEFAULT_DB);
  NrUnusual unusual[NH_MAX_UNUSUAL];
  int n = nr_find_unusual(model, &p, unusual, NH_MAX_UNUSUAL);
  NSMutableArray<NhReviewItem*>* out = [NSMutableArray array];
  for (int i = 0; i < n; i++) {
    const NrTemplate* t = &model->t[unusual[i].index];
    NhReviewItem* item = [NhReviewItem new];
    item.tsv = sources[t->session];
    item.word = ![item.tsv.lastPathComponent hasPrefix:@"heard-"];
    item.sample = t->sample;
    item.end = t->end;
    item.learned = nh_word_name(t->label);
    item.nearest = nh_word_name(unusual[i].nearest);
    [out addObject:item];
  }
  printf("speech review: %d unusual recordings to ask about\n", n);
  fflush(stdout);
  nr_model_free(model);
  free(model);
  return out;
}

// Every clip still to review, oldest first: .tsv paths.
static NSArray<NSString*>* nh_to_review(void) {
  NSMutableArray<NSString*>* out = [NSMutableArray array];
  NSArray<NSURL*>* files = [NSFileManager.defaultManager
    contentsOfDirectoryAtURL:nh_dir() includingPropertiesForKeys:nil
                     options:NSDirectoryEnumerationSkipsHiddenFiles
                       error:nil];
  for (NSURL* file in files) {
    if (![file.pathExtension isEqualToString:@"tsv"] ||
        ![file.lastPathComponent hasPrefix:@"heard-"]) {
      continue;
    }
    NSString* text = [NSString stringWithContentsOfURL:file
                                              encoding:NSUTF8StringEncoding
                                                 error:nil];
    if (text && nh_unreviewed(text)) [out addObject:file.path];
  }
  return [out sortedArrayUsingSelector:@selector(compare:)];
}

// A header line's value from a clip's .tsv, or "".
static NSString* nh_tsv_value(NSString* text, NSString* key) {
  for (NSString* line in [text componentsSeparatedByString:@"\n"]) {
    NSString* prefix = [NSString stringWithFormat:@"# %@\t", key];
    if ([line hasPrefix:prefix]) return [line substringFromIndex:prefix.length];
  }
  return @"";
}

// Give the clip at `tsv` the label `label`, and mark it reviewed.
static void nh_relabel(NSString* tsv, NSString* label) {
  NSString* text = [NSString stringWithContentsOfFile:tsv
                                             encoding:NSUTF8StringEncoding
                                                error:nil];
  NSMutableArray<NSString*>* lines = [NSMutableArray array];
  for (NSString* line in [text componentsSeparatedByString:@"\n"]) {
    if (line.length == 0 || [line hasPrefix:@"0\t"]) continue;
    [lines addObject:line];
  }
  [lines addObject:[@"# reviewed\t" stringByAppendingString:label]];
  [lines addObject:[@"0\t" stringByAppendingString:label]];
  [[[lines componentsJoinedByString:@"\n"] stringByAppendingString:@"\n"]
    writeToFile:tsv atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

@interface NhReviewController : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* heard;
@property(strong) NSTextField* status;
@property(strong) NSMutableArray<NSButton*>* answers;
@property(strong) AVAudioPlayer* player;
@property(strong) NSMutableArray<NhReviewItem*>* clips;
@property int index;
@property BOOL changed;
@property BOOL searching;  // still looking for unusual recordings
@property int playing;     // counts plays, so a stale stop is ignored
@end

@implementation NhReviewController

- (NSButton*)button:(NSString*)title key:(NSString*)key tag:(NSInteger)tag
                  at:(NSRect)frame {
  NSButton* b = [NSButton buttonWithTitle:title target:self
                                   action:@selector(answer:)];
  b.keyEquivalent = key;
  b.tag = tag;
  b.frame = frame;
  [self.window.contentView addSubview:b];
  return b;
}

- (void)show {
  if (self.window) {
    [self.window makeKeyAndOrderFront:nil];
    return;
  }
  self.window = [[NSWindow alloc]
    initWithContentRect:NSMakeRect(0, 0, 720, 280)
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                backing:NSBackingStoreBuffered
                  defer:NO];
  self.window.title = @"Review Number Clips";
  self.window.releasedWhenClosed = NO;
  self.window.delegate = self;
  NSView* content = self.window.contentView;

  self.heard = [NSTextField labelWithString:@""];
  self.heard.font = [NSFont systemFontOfSize:28 weight:NSFontWeightSemibold];
  self.heard.alignment = NSTextAlignmentCenter;
  self.heard.frame = NSMakeRect(20, 180, 680, 70);
  self.heard.lineBreakMode = NSLineBreakByWordWrapping;
  [content addSubview:self.heard];

  self.status = [NSTextField labelWithString:@""];
  self.status.font = [NSFont systemFontOfSize:13];
  self.status.textColor = NSColor.secondaryLabelColor;
  self.status.alignment = NSTextAlignmentCenter;
  self.status.frame = NSMakeRect(20, 150, 680, 22);
  [content addSubview:self.status];

  NSTextField* ask = [NSTextField labelWithString:@"What did you say?"];
  ask.alignment = NSTextAlignmentCenter;
  ask.frame = NSMakeRect(20, 115, 680, 22);
  [content addSubview:ask];

  // The answer is its tag: 1-7, 0 for not a number, -1 delete, -2 skip,
  // -3 play again.
  self.answers = [NSMutableArray array];
  for (int i = 0; i < 7; i++) {
    [self.answers addObject:
      [self button:[NSString stringWithFormat:@"%d %s", i + 1, NR_WORDS[i]]
               key:[NSString stringWithFormat:@"%d", i + 1] tag:i + 1
                at:NSMakeRect(20 + i * 97, 70, 92, 32)]];
  }
  [self.answers addObject:[self button:@"(N)ot a number" key:@"n" tag:0
                                    at:NSMakeRect(20, 26, 160, 32)]];
  [self.answers addObject:[self button:@"(D)elete" key:@"d" tag:-1
                                    at:NSMakeRect(190, 26, 110, 32)]];
  [self.answers addObject:[self button:@"(S)kip" key:@"s" tag:-2
                                    at:NSMakeRect(310, 26, 100, 32)]];
  [self.answers addObject:[self button:@"Play again (space)" key:@" "
                                   tag:-3
                                    at:NSMakeRect(530, 26, 170, 32)]];

  self.clips = [NSMutableArray array];
  for (NSString* tsv in nh_to_review()) {
    NhReviewItem* item = [NhReviewItem new];
    item.tsv = tsv;
    [self.clips addObject:item];
  }
  self.index = 0;
  self.changed = NO;
  self.searching = YES;
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [self showClip];

  // The unusual recordings, after the rest, once they're found.
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSArray<NhReviewItem*>* unusual = nh_find_unusual();
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!self.window) return;
      bool waiting = self.index >= (int)self.clips.count;
      [self.clips addObjectsFromArray:unusual];
      self.searching = NO;
      if (waiting) {
        [self showClip];
      } else {
        [self showStatus];
      }
    });
  });
}

- (void)showStatus {
  NhReviewItem* item = self.clips[self.index];
  NSString* name =
    item.tsv.lastPathComponent.stringByDeletingPathExtension;
  if (item.word) {
    name = [name stringByAppendingFormat:@" at %.1fs",
            item.sample / (self.player.format.sampleRate ?: 48000)];
  }
  self.status.stringValue = [NSString stringWithFormat:@"%d of %d%@   %@",
    self.index + 1, (int)self.clips.count,
    self.searching ? @", looking for unusual recordings" : @"", name];
}

// Play the item: a whole clip, or a word with a little either side of it.
- (void)play {
  NhReviewItem* item = self.clips[self.index];
  [self.player stop];
  int playing = ++self.playing;
  if (!item.word) {
    self.player.currentTime = 0;
    [self.player play];
    return;
  }
  double rate = self.player.format.sampleRate ?: 48000;
  double from = fmax(0, item.sample / rate - 0.25);
  double to = item.end / rate + 0.25;
  self.player.currentTime = from;
  [self.player play];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               (int64_t)((to - from) * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
    if (self.playing == playing) [self.player stop];
  });
}

- (void)showClip {
  [self.player stop];
  if (self.index >= (int)self.clips.count) {
    self.heard.stringValue = self.searching
      ? @"Looking for unusual recordings…"
      : self.clips.count ? @"All reviewed" : @"Nothing to review";
    self.status.stringValue = @"";
    for (NSButton* b in self.answers) b.enabled = NO;
    return;
  }
  for (NSButton* b in self.answers) b.enabled = YES;
  NhReviewItem* item = self.clips[self.index];
  NSString* text = [NSString stringWithContentsOfFile:item.tsv
                                             encoding:NSUTF8StringEncoding
                                                error:nil] ?: @"";
  if (item.nearest) {
    self.heard.stringValue = [NSString stringWithFormat:
      @"Unusual: learned as “%@”, but sounds most like “%@”", item.learned,
      item.nearest];
  } else {
    NSString* maybe = nh_tsv_value(text, @"maybe");
    NSString* apple = nh_tsv_value(text, @"apple");
    self.heard.stringValue = [NSString stringWithFormat:
      @"fast recognizer: %@      Apple: %@",
      maybe.length ? [NSString stringWithFormat:@"nearly %@", maybe]
                   : nh_tsv_value(text, @"fast"),
      apple.length ? [NSString stringWithFormat:@"“%@”", apple]
                   : @"nothing"];
  }
  NSURL* wav = [NSURL fileURLWithPath:[item.tsv.stringByDeletingPathExtension
                                       stringByAppendingPathExtension:@"wav"]];
  self.player = [[AVAudioPlayer alloc] initWithContentsOfURL:wav error:nil];
  [self showStatus];
  [self play];
}

- (void)answer:(NSButton*)sender {
  if (self.index >= (int)self.clips.count) return;
  NhReviewItem* item = self.clips[self.index];
  NSString* tsv = item.tsv;
  NSInteger tag = sender.tag;
  if (tag == -3) {
    [self play];
    return;
  }
  if (item.word && tag != -2) {
    // One word of a session: the session stays, and the word is learned
    // as what you said, or not at all.
    nh_review_word(tsv, item.sample,
                   tag == -1 ? @"drop" : tag ? @(NR_WORDS[tag - 1])
                                             : @"other");
    self.changed = YES;
  } else if (tag == -1) {
    [NSFileManager.defaultManager removeItemAtPath:tsv error:nil];
    [NSFileManager.defaultManager removeItemAtPath:
      [tsv.stringByDeletingPathExtension stringByAppendingPathExtension:@"wav"]
                                             error:nil];
    dispatch_async(nh_queue(), ^{ nh_bytes = -1; });  // count afresh
    self.changed = YES;
  } else if (tag >= 0) {
    NSString* text = [NSString stringWithContentsOfFile:tsv
                                               encoding:NSUTF8StringEncoding
                                                  error:nil] ?: @"";
    NSString* label = tag ? @(NR_WORDS[tag - 1]) :
      [@"other: " stringByAppendingString:nh_tsv_value(text, @"apple")];
    nh_relabel(tsv, label);
    self.changed = YES;
  }
  self.index++;
  [self showClip];
}

- (void)windowWillClose:(NSNotification*)note {
  self.playing++;
  [self.player stop];
  self.player = nil;
  self.window = nil;
  if (self.changed) speech_fast_learn();
}

@end

#endif
