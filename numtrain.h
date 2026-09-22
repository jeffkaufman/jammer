#ifndef JML_NUMTRAIN_H
#define JML_NUMTRAIN_H

// Recording samples of your voice for the fast number recognizer: Speech
// Recognition > Record Number Samples... opens a window that shows one thing
// at a time to say -- mostly the numbers one to seven, in a shuffled order,
// then words it has to learn to ignore, then a stretch of talking and a
// stretch of quiet -- and records the whistle's microphone the whole time.
//
// What's recorded is the raw microphone, before the gate, so that the gate
// can be tried at any level afterwards.  Each session is two files in
// ~/Library/Application Support/net.jefftk.jammer/numbers/:
//
//   session-YYYYMMDD-HHMMSS.wav   32-bit float, mono, the rig's sample rate
//   session-YYYYMMDD-HHMMSS.tsv   when each prompt went up, in samples:
//                                   <sample>\t<prompt>
//
// Prompts are timed by the audio itself: the next one goes up when enough
// samples have been recorded, so the labels line up with the sound exactly
// however late the window draws.  Closing the window stops it; what was
// recorded up to then is kept.
//
// The recording runs on the speech queue, fed by speech_drain, so it works
// whether or not speech recognition (F8) is on.
//
// Include after speech.h, in jammer-mac.m.

#import <Cocoa/Cocoa.h>

typedef struct {
  const char* text;
  double seconds;
} NtPrompt;

#define NT_MAX_PROMPTS 256
#define NT_NUMBER_REPEATS 12
#define NT_WORD_SECONDS 1.5
#define NT_PHRASE_SECONDS 2.5

// Things that start an utterance and aren't a number, which it must not
// mistake for one.  The lead-in words, especially: "set" and "select" start
// like "six" and "seven".
static const char* NT_REJECT_WORDS[] = {
  "press", "change", "select", "set", "okay", "yeah", "hey", "and",
  "let's go", "wait", "stop", "again", "one more time", "last time",
  "sing", "fine",
};
static const char* NT_REJECT_PHRASES[] = {
  "press foot bass", "change key to B flat", "select room two",
  "change mode to minor", "press drum some",
};

// Speech queue only, apart from what the window reads under the lock.
static FILE* nt_wav;
static FILE* nt_tsv;
static long long nt_samples;          // recorded so far this session
static long long nt_next_at;          // when the next prompt goes up
static NtPrompt nt_prompts[NT_MAX_PROMPTS];
static int nt_n_prompts;
static int nt_index;                  // the prompt that's up, or -1
static char nt_path[1024];

// For the window, under the lock.
static char nt_showing[64];
static char nt_status[160];
static bool nt_running;

static void nt_wav_header(FILE* f, double rate, long long samples) {
  uint32_t data = (uint32_t)(samples * 4);
  uint32_t r = (uint32_t)rate;
  uint32_t riff = 36 + data, fmt_len = 16, bytes_per_s = r * 4;
  uint16_t format = 3 /* float */, channels = 1, align = 4, bits = 32;
  fseek(f, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt_len, 4, 1, f);
  fwrite(&format, 2, 1, f); fwrite(&channels, 2, 1, f);
  fwrite(&r, 4, 1, f); fwrite(&bytes_per_s, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fseek(f, 0, SEEK_END);
}

static void nt_shuffle(NtPrompt* p, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = (int)arc4random_uniform((uint32_t)i + 1);
    NtPrompt t = p[i]; p[i] = p[j]; p[j] = t;
  }
}

static void nt_build_prompts(void) {
  static const char* NUMBERS[] = {"one", "two", "three", "four", "five",
                                  "six", "seven"};
  int n = 0;
  nt_prompts[n++] = (NtPrompt){"get ready", 3};
  int start = n;
  for (int r = 0; r < NT_NUMBER_REPEATS; r++) {
    for (int i = 0; i < 7; i++) {
      nt_prompts[n++] = (NtPrompt){NUMBERS[i], NT_WORD_SECONDS};
    }
  }
  nt_shuffle(nt_prompts + start, n - start);
  start = n;
  for (int r = 0; r < 2; r++) {
    for (size_t i = 0; i < sizeof(NT_REJECT_WORDS) / sizeof(char*); i++) {
      nt_prompts[n++] = (NtPrompt){NT_REJECT_WORDS[i], NT_WORD_SECONDS};
    }
    for (size_t i = 0; i < sizeof(NT_REJECT_PHRASES) / sizeof(char*); i++) {
      nt_prompts[n++] = (NtPrompt){NT_REJECT_PHRASES[i], NT_PHRASE_SECONDS};
    }
  }
  nt_shuffle(nt_prompts + start, n - start);
  nt_prompts[n++] = (NtPrompt){"talk, as you would to the band", 20};
  nt_prompts[n++] = (NtPrompt){"stay quiet", 15};
  nt_n_prompts = n;
}

static void nt_set_showing(const char* text, const char* status) {
  LOCK();
  snprintf(nt_showing, sizeof(nt_showing), "%s", text);
  snprintf(nt_status, sizeof(nt_status), "%s", status);
  UNLOCK();
}

// Speech queue.
static void nt_finish(const char* why) {
  if (!nt_wav) return;
  nt_wav_header(nt_wav, speech_format.sampleRate, nt_samples);
  fclose(nt_wav);
  fclose(nt_tsv);
  nt_wav = nt_tsv = NULL;
  speech_record_hook = NULL;
  atomic_store_explicit(&speech_recording, 0, memory_order_relaxed);
  char status[160];
  snprintf(status, sizeof(status), "%s: %.0fs saved as %s", why,
           (double)nt_samples / speech_format.sampleRate,
           [@(nt_path).lastPathComponent UTF8String]);
  printf("number samples %s\n", status);
  fflush(stdout);
  LOCK();
  nt_running = false;
  UNLOCK();
  nt_set_showing(strcmp(why, "done") == 0 ? "thanks!" : "stopped", status);
  speech_fast_learn();  // with this one too
}

// Speech queue, from speech_drain: everything the microphone sent, ungated.
static void nt_record(const float* samples, int n) {
  if (!nt_wav) return;
  fwrite(samples, sizeof(float), (size_t)n, nt_wav);
  nt_samples += n;
  while (nt_samples >= nt_next_at) {
    nt_index++;
    if (nt_index >= nt_n_prompts) {
      nt_finish("done");
      return;
    }
    const NtPrompt* p = &nt_prompts[nt_index];
    fprintf(nt_tsv, "%lld\t%s\n", nt_next_at, p->text);
    fflush(nt_tsv);
    nt_next_at += (long long)(p->seconds * speech_format.sampleRate);
    char status[160];
    snprintf(status, sizeof(status), "%d of %d", nt_index + 1, nt_n_prompts);
    nt_set_showing(p->text, status);
  }
}

// Speech queue.
static void nt_begin(void) {
  if (nt_wav) return;
  LOCK();
  bool mic = whistle_available;
  double gate_db = speech_gate_db;
  UNLOCK();
  if (!mic) {
    nt_set_showing("no microphone",
                   "set up the whistle's microphone first (Whistle menu)");
    return;
  }
  NSURL* support = [[NSFileManager.defaultManager
    URLsForDirectory:NSApplicationSupportDirectory
           inDomains:NSUserDomainMask] firstObject];
  NSURL* dir =
    [support URLByAppendingPathComponent:@"net.jefftk.jammer/numbers"];
  [NSFileManager.defaultManager createDirectoryAtURL:dir
                         withIntermediateDirectories:YES
                                          attributes:nil
                                               error:nil];
  NSDateFormatter* format = [NSDateFormatter new];
  format.dateFormat = @"yyyyMMdd-HHmmss";
  NSString* base = [dir URLByAppendingPathComponent:
    [NSString stringWithFormat:@"session-%@",
                               [format stringFromDate:[NSDate date]]]].path;
  snprintf(nt_path, sizeof(nt_path), "%s.wav", base.UTF8String);
  nt_wav = fopen(nt_path, "wb");
  nt_tsv = fopen([base stringByAppendingString:@".tsv"].UTF8String, "w");
  if (!nt_wav || !nt_tsv) {
    if (nt_wav) fclose(nt_wav);
    if (nt_tsv) fclose(nt_tsv);
    nt_wav = nt_tsv = NULL;
    nt_set_showing("can't record", strerror(errno));
    return;
  }
  nt_wav_header(nt_wav, speech_format.sampleRate, 0);
  fprintf(nt_tsv, "# rate\t%.0f\n# gate_db\t%.0f\n",
          speech_format.sampleRate, gate_db);
  nt_build_prompts();
  nt_samples = 0;
  nt_next_at = 0;
  nt_index = -1;
  LOCK();
  nt_running = true;
  UNLOCK();
  // Whatever was waiting in the ring is from before the window opened.
  atomic_store_explicit(&speech_ring_read,
                        atomic_load_explicit(&speech_ring_write,
                                             memory_order_acquire),
                        memory_order_release);
  speech_record_hook = nt_record;
  atomic_store_explicit(&speech_recording, 1, memory_order_relaxed);
  printf("recording number samples to %s\n", nt_path);
  fflush(stdout);
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

@interface NtWindowController : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* prompt;
@property(strong) NSTextField* status;
@property(strong) NSTimer* timer;
@end

@implementation NtWindowController

- (void)show {
  if (self.window) {
    [self.window makeKeyAndOrderFront:nil];
    return;
  }
  NSRect frame = NSMakeRect(0, 0, 720, 300);
  self.window = [[NSWindow alloc]
    initWithContentRect:frame
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                backing:NSBackingStoreBuffered
                  defer:NO];
  self.window.title = @"Record Number Samples";
  self.window.releasedWhenClosed = NO;
  self.window.delegate = self;
  NSView* content = self.window.contentView;

  self.prompt = [NSTextField labelWithString:@""];
  self.prompt.font = [NSFont systemFontOfSize:64 weight:NSFontWeightSemibold];
  self.prompt.alignment = NSTextAlignmentCenter;
  self.prompt.frame = NSMakeRect(20, 120, 680, 110);
  self.prompt.lineBreakMode = NSLineBreakByWordWrapping;
  [content addSubview:self.prompt];

  self.status = [NSTextField labelWithString:@""];
  self.status.font = [NSFont systemFontOfSize:15];
  self.status.textColor = NSColor.secondaryLabelColor;
  self.status.alignment = NSTextAlignmentCenter;
  self.status.frame = NSMakeRect(20, 60, 680, 40);
  self.status.lineBreakMode = NSLineBreakByWordWrapping;
  [content addSubview:self.status];

  NSTextField* help = [NSTextField labelWithString:
    @"Say each word once, as you would on stage, into the microphone.  "
    @"Close the window to stop; what's recorded so far is kept."];
  help.font = [NSFont systemFontOfSize:13];
  help.textColor = NSColor.tertiaryLabelColor;
  help.alignment = NSTextAlignmentCenter;
  help.frame = NSMakeRect(20, 14, 680, 36);
  help.lineBreakMode = NSLineBreakByWordWrapping;
  [content addSubview:help];

  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  nt_set_showing("", "");
  dispatch_async(speech_queue, ^{ nt_begin(); });
  self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30
                                               repeats:YES
                                                 block:^(NSTimer* t) {
    LOCK();
    NSString* showing = @(nt_showing);
    NSString* status = @(nt_status);
    UNLOCK();
    if (![self.prompt.stringValue isEqualToString:showing]) {
      self.prompt.stringValue = showing;
    }
    if (![self.status.stringValue isEqualToString:status]) {
      self.status.stringValue = status;
    }
  }];
}

- (void)windowWillClose:(NSNotification*)note {
  [self.timer invalidate];
  self.timer = nil;
  self.window = nil;
  dispatch_async(speech_queue, ^{ nt_finish("stopped"); });
}

@end

#endif
