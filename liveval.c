// liveval -- plays VSL Mixer recordings of calling numbers back through the
// fast number recognizer (numrec.h) and the landing rule (speechtiming.h),
// as jammer would have heard them live, and says when each call would land:
//
//   ./liveval [-v] [name=value ...] REC_DIR[=INTENDED] ...
//
// REC_DIR is a VSL Mixer recording made with jammer linked: its "09 In 1
// (jack).wav" is the microphone, and its "Jammer MIDI.mid" has the pedal
// beats (note-ons on channel 10) and what jammer did live (CC 22 on channel
// 16, a spoken chord number, logged as it landed).
//
// The model is learned the way jammer learns it at startup: every numtrain.h
// session and every kept numheard.h clip in
// ~/Library/Application Support/com.jefftk.jammer/numbers/.  Then the
// microphone is streamed through numrec.h as speech.h's fast path streams
// it, and each number taken lands by speech_landing_time against the real
// pedal beats.  Apple's recognizer isn't simulated: what the fast path left
// is reported as left to Apple.
//
// INTENDED says which beats you meant the changes to land on, counting pedal
// beats from 0, so each call can be scored on time, early or late:
//
//   P/F       every P beats from beat F, e.g. 8/12
//   a,b,c...  just those beats
//
// Without it, each call is set against where it landed live.  Either way,
// a call is the number that landed (live) nearest after it; when there's no
// INTENDED, a call's beat is where it landed live.
//
// name=value sets a parameter of NrParams for the detector (both learning
// and listening, as a change to nr_default_params would), e.g. noise_db=15;
// The landing rule's (speechtiming.h) can be tried too: lead=1.75 for
// SPEECH_BEATS_AFTER_ONSET, amb=0.25 SPEECH_BORDERLINE, conf=4
// SPEECH_CONFIDENT, chord=4 SPEECH_CHORD_BEATS, shift=2 SPEECH_SHIFT_AFTER,
// memory=128 SPEECH_MEMORY_BEATS, bvotes=1 SPEECH_BORDERLINE_VOTES, and
// bar=0 to land by time alone, without where changes have been falling in
// the bar.  LIVEVAL_DEBUG=1 in the environment prints each landing's pedal
// beat and the bar's phase.  vowel=15 lands from where the
// word comes within 15dB of its loudest -- about where its first vowel
// starts -- rather than from where it began, which jammer doesn't do (yet).
// -v lists every
// call, not just the ones that missed.
//
// To find what you meant, listen to the recording, or look at its chords:
// the drone on "01 Jammer L" and an instrument on its own input.

#include <dirent.h>

#include "numrec.h"

static double beats_after_onset;  // lead=, or speechtiming.h's
static bool use_recent = true;    // bar=0 decides by time alone
static double anchor_db = 0;      // vowel=: land from the vowel, not onset
#define SPEECH_BEATS_AFTER_ONSET beats_after_onset
static double borderline;  // amb=, or speechtiming.h's
#define SPEECH_BORDERLINE borderline
static int chord_beats;  // chord=, or speechtiming.h's
#define SPEECH_CHORD_BEATS chord_beats
static int memory_beats;  // memory=, or speechtiming.h's
#define SPEECH_MEMORY_BEATS memory_beats
static int borderline_votes = 0;  // bvotes=
#define SPEECH_BORDERLINE_VOTES borderline_votes
static int shift_after;  // shift=, or speechtiming.h's
#define SPEECH_SHIFT_AFTER shift_after
static int confident;  // conf=, or speechtiming.h's
#define SPEECH_CONFIDENT confident
#include "speechtiming.h"

#define NS 1000000000.0

// ---------------------------------------------------------------------------
// Reading the recording
// ---------------------------------------------------------------------------

// A 16-, 24- or 32-bit integer or 32-bit float WAV, mono, as floats.
static float* read_wav_any(const char* path, long long* n, double* rate) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* b = malloc((size_t)size);
  if (fread(b, 1, (size_t)size, f) != (size_t)size) size = 0;
  fclose(f);
  int format = 0, bits = 0, channels = 0;
  long data = 0, len = 0;
  for (long off = 12; off + 8 <= size;) {
    unsigned l = b[off + 4] | b[off + 5] << 8 | b[off + 6] << 16 |
                 (unsigned)b[off + 7] << 24;
    if (!memcmp(b + off, "fmt ", 4)) {
      format = b[off + 8] | b[off + 9] << 8;
      channels = b[off + 10] | b[off + 11] << 8;
      *rate = b[off + 12] | b[off + 13] << 8 | b[off + 14] << 16 |
              (unsigned)b[off + 15] << 24;
      bits = b[off + 22] | b[off + 23] << 8;
    } else if (!memcmp(b + off, "data", 4)) {
      data = off + 8;
      len = l;
      if (data + len > size) len = size - data;
      break;
    }
    off += 8 + l + (l & 1);
  }
  if (!data || channels != 1 || !(bits == 16 || bits == 24 || bits == 32)) {
    free(b);
    return NULL;
  }
  int bytes = bits / 8;
  *n = len / bytes;
  float* x = malloc(sizeof(float) * (size_t)(*n > 0 ? *n : 1));
  for (long long i = 0; i < *n; i++) {
    const unsigned char* p = b + data + i * bytes;
    if (format == 3) {
      memcpy(&x[i], p, 4);
    } else if (bits == 16) {
      x[i] = (int16_t)(p[0] | p[1] << 8) / 32768.0f;
    } else if (bits == 24) {
      int v = p[0] | p[1] << 8 | p[2] << 16;
      if (v & 0x800000) v -= 0x1000000;
      x[i] = v / 8388608.0f;
    } else {
      x[i] = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24) /
             2147483648.0f;
    }
  }
  free(b);
  return x;
}

typedef struct { double t; int number; } Landed;

// The pedal beats and what landed live, in seconds, from a Standard MIDI
// File as VSL Mixer writes it.  Everything before jammer's history block
// ends (CC 118 back to 0) is history, not the set.
static bool read_midi(const char* path, double* beats, int* nb, int max_beats,
                      Landed* landed, int* nl, int max_landed) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* b = malloc((size_t)size);
  if (fread(b, 1, (size_t)size, f) != (size_t)size) size = 0;
  fclose(f);
  if (size < 14 || memcmp(b, "MThd", 4)) { free(b); return false; }
  int division = b[12] << 8 | b[13];
  double sec_per_tick = 0.5 / division;  // 120 bpm unless told otherwise
  *nb = *nl = 0;
  for (long off = 8 + (b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]);
       off + 8 <= size;) {
    long len = b[off + 4] << 24 | b[off + 5] << 16 | b[off + 6] << 8 |
               b[off + 7];
    long p = off + 8, end = p + len;
    if (end > size) end = size;
    off = end;
    double t = 0;
    int status = 0;
    bool history = false;
    while (p < end) {
      long delta = 0;
      while (p < end) { delta = delta << 7 | (b[p] & 0x7f); if (!(b[p++] & 0x80)) break; }
      t += delta * sec_per_tick;
      if (p >= end) break;
      if (b[p] == 0xff) {  // meta
        int type = b[p + 1];
        long l = 0;
        p += 2;
        while (p < end) { l = l << 7 | (b[p] & 0x7f); if (!(b[p++] & 0x80)) break; }
        if (type == 0x51 && l == 3) {
          sec_per_tick = (b[p] << 16 | b[p + 1] << 8 | b[p + 2]) / 1e6 / division;
        }
        p += l;
        continue;
      }
      if (b[p] == 0xf0 || b[p] == 0xf7) {
        long l = 0;
        p++;
        while (p < end) { l = l << 7 | (b[p] & 0x7f); if (!(b[p++] & 0x80)) break; }
        p += l;
        continue;
      }
      if (b[p] & 0x80) status = b[p++];
      int kind = status & 0xf0, ch = status & 0x0f;
      int d1 = p < end ? b[p] : 0, d2 = p + 1 < end ? b[p + 1] : 0;
      p += (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
      if (kind == 0x90 && d2 > 0 && ch == 9 && *nb < max_beats) {
        beats[(*nb)++] = t;
      } else if (kind == 0xb0 && ch == 15 && d1 == 118) {
        history = d2 > 0;
      } else if (kind == 0xb0 && ch == 15 && d1 == 22 && !history &&
                 *nl < max_landed) {
        landed[(*nl)++] = (Landed){t, d2};
      }
    }
  }
  free(b);
  // Merged from every track, so in order.
  for (int i = 1; i < *nb; i++) {
    for (int j = i; j > 0 && beats[j] < beats[j - 1]; j--) {
      double s = beats[j]; beats[j] = beats[j - 1]; beats[j - 1] = s;
    }
  }
  for (int i = 1; i < *nl; i++) {
    for (int j = i; j > 0 && landed[j].t < landed[j - 1].t; j--) {
      Landed s = landed[j]; landed[j] = landed[j - 1]; landed[j - 1] = s;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Learning, as speech_fast_learn does
// ---------------------------------------------------------------------------

static NrModel model;
static NrParams params;  // the detector's, with any name=value

static bool contains(const char* s, const char* t) { return strstr(s, t); }

static void learn(void) {
  char dir[1024];
  snprintf(dir, sizeof(dir),
           "%s/Library/Application Support/com.jefftk.jammer/numbers",
           getenv("HOME"));
  int sessions = 0, clips = 0;
  for (int pass = 0; pass < 2; pass++) {
    char d[1100];
    snprintf(d, sizeof(d), pass ? "%s/heard" : "%s", dir);
    DIR* D = opendir(d);
    for (struct dirent* e; D && (e = readdir(D));) {
      size_t len = strlen(e->d_name);
      if (len < 5 || strcmp(e->d_name + len - 4, ".wav")) continue;
      char wav[1400], tsv[1400];
      snprintf(wav, sizeof(wav), "%s/%s", d, e->d_name);
      snprintf(tsv, sizeof(tsv), "%.*s.tsv", (int)strlen(wav) - 4, wav);
      bool clip = !strncmp(e->d_name, "heard-", 6);
      static char text[1 << 16];
      text[0] = '\n';
      FILE* f = fopen(tsv, "r");
      size_t k = f ? fread(text + 1, 1, sizeof(text) - 2, f) : 0;
      text[k + 1] = 0;
      if (f) fclose(f);
      // numheard.h's nh_unreviewed: not learned until you've said.
      bool reviewed = contains(text, "\n# reviewed\t");
      if (clip && !reviewed && (!contains(text, "\n# fast\tnothing\n") ||
                                contains(text, "\n# maybe\t"))) {
        continue;
      }
      long long n;
      double rate;
      float* x = nr_read_wav(wav, &n, &rate);
      NrPrompt* prompts = NULL;
      NrReview* reviews = NULL;
      float gate_db = -20, room_db = params.room_db;
      int n_prompts = nr_read_prompts(tsv, &prompts, &gate_db, &room_db);
      int n_reviews = nr_read_reviews(tsv, &reviews);
      if (x && n_prompts > 0) {
        NrParams sp = params;
        sp.hangover = nr_default_params(-20).hangover;
        sp.trigger_db = gate_db - 6;
        sp.room_db = room_db;
        nr_learn_session(&model, x, n, rate, prompts, n_prompts, reviews,
                         n_reviews, clip && reviewed, &sp, sessions++);
        if (clip) clips++;
      }
      free(x);
      free(prompts);
      free(reviews);
    }
    if (D) closedir(D);
  }
  NrParams p = params;
  p.hangover = nr_default_params(-20).hangover;
  nr_model_finish(&model, &p);
  fprintf(stderr, "learned %d recordings of words from %d sessions "
          "(%d of them kept clips)\n", model.n, sessions - clips, clips);
}

// ---------------------------------------------------------------------------
// Listening, as speech_fast_utterance does
// ---------------------------------------------------------------------------

typedef struct {
  double onset, end;  // seconds
  double anchor;      // what it lands from: onset, or the vowel's start
  double heard;       // when the fast path took it, or -1
  int number;         // 1-7, or 0
  bool overlong;
  NrResult r;
} Call;

#define MAX_CALLS 2048
static Call calls[MAX_CALLS];
static int n_calls;

// The first frame within anchor_db of the utterance's loudest, in seconds:
// about where its first vowel starts, past an "f" or an "s".
static double vowel_start(const NrStream* s, long long onset, long long end) {
  double hop = s->f.hop / 48000.0;
  if (anchor_db <= 0) return onset * hop;
  float peak = -200;
  for (long long i = onset; i <= end; i++) {
    if (NR_AT(s, rms, i) > peak) peak = NR_AT(s, rms, i);
  }
  for (long long i = onset; i <= end; i++) {
    if (NR_AT(s, rms, i) >= peak - anchor_db) return i * hop;
  }
  return onset * hop;
}

static bool on_utterance(void* ctx, NrStream* s, long long onset,
                         long long end, bool clear, int quiet, bool final) {
  (void)ctx;
  if (!clear || n_calls == MAX_CALLS) return final;
  float feat[NR_MAX_FRAMES + 2 * NR_PAD][NR_DIM];
  int n = nr_utterance(s, onset, end, feat);
  NrResult r = nr_classify(&model, feat, n, &s->p, -1, 0, 0);
  double hop = s->f.hop / 48000.0;
  if (r.label >= 0 && quiet >= model.wait[r.label]) {
    // A tick on top, as speech.h counts it: the audio's wait in the ring.
    calls[n_calls++] = (Call){onset * hop, (end + 1) * hop,
                              vowel_start(s, onset, end),
                              (end + 1 + quiet) * hop + 0.010, r.label + 1,
                              false, r};
    return true;
  }
  if (final) {
    calls[n_calls++] = (Call){onset * hop, (end + 1) * hop, onset * hop, -1,
                              0, false, r};
  }
  return final;
}

static bool on_overlong(void* ctx, NrStream* s, long long onset,
                        long long end, bool clear, int quiet, bool final) {
  (void)ctx; (void)clear; (void)quiet; (void)final;
  double hop = s->f.hop / 48000.0;
  if (n_calls < MAX_CALLS) {
    calls[n_calls++] = (Call){onset * hop, (end + 1) * hop, onset * hop, -1,
                              0, true, {0}};
  }
  return true;
}

// ---------------------------------------------------------------------------
// Landing, as speech_land does
// ---------------------------------------------------------------------------

// Where the pedals have got to at `t`: the last hit, and the beat as the
// last few hits give it.
static bool pedals_at(const double* beats, int nb, double t, double* last,
                      double* beat, int* index) {
  int k = -1;
  while (k + 1 < nb && beats[k + 1] <= t) k++;
  if (k < 1) return false;
  int from = k >= 4 ? k - 4 : 0;
  *beat = (beats[k] - beats[from]) / (k - from);
  *last = beats[k];
  if (index) *index = k;
  return t - *last < *beat * 1.5;
}

// Which pedal beat a time falls on: the first at or after it, less the
// quarter beat early a pedal can take a landing in.
static int beat_at(const double* beats, int nb, double t, double beat) {
  for (int i = 0; i < nb; i++) {
    if (beats[i] >= t - SPEECH_EARLY_FRACTION * beat) return i;
  }
  return nb;
}

// Whether the pedals stopped between `a` and `b`: a gap of more than a
// beat and a half, as speech_beat_now counts them stopped.
static bool beats_stopped_between(const double* beats, int nb, double a,
                                  double b) {
  for (int i = 1; i < nb; i++) {
    if (beats[i] <= a || beats[i - 1] >= b) continue;
    int from = i >= 5 ? i - 5 : 0;
    double beat = (beats[i - 1] - beats[from]) / (i - 1 - from > 0 ? i - 1 - from : 1);
    if (beats[i] - beats[i - 1] > 1.5 * beat) return true;
  }
  return false;
}

static double position(const double* beats, int nb, double t) {
  int k = 0;
  while (k + 1 < nb && beats[k + 1] <= t) k++;
  if (k + 1 >= nb) return k;
  return k + (t - beats[k]) / (beats[k + 1] - beats[k]);
}

typedef struct { int on_time, early, late, wrong, apple, calls; } Score;

// INTENDED, as a list of beats.
static int parse_intended(const char* spec, int nb, int* out, int max) {
  int n = 0;
  const char* slash = strchr(spec, '/');
  if (slash && !strchr(spec, ',')) {
    int period = atoi(spec), first = atoi(slash + 1);
    for (int b = first; b < nb && n < max && period > 0; b += period) out[n++] = b;
    return n;
  }
  for (const char* p = spec; *p && n < max;) {
    out[n++] = atoi(p);
    p = strchr(p, ',');
    if (!p) break;
    p++;
  }
  return n;
}

static void run(const char* arg, bool verbose, Score* total) {
  char dir[1024], spec[4096] = "";
  const char* eq = strchr(arg, '=');
  snprintf(dir, sizeof(dir), "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg),
           arg);
  if (eq) snprintf(spec, sizeof(spec), "%s", eq + 1);
  char path[1200];
  snprintf(path, sizeof(path), "%s/09 In 1 (jack).wav", dir);
  long long n;
  double rate;
  float* x = read_wav_any(path, &n, &rate);
  if (!x) { fprintf(stderr, "can't read %s\n", path); return; }
  static double beats[16384];
  static Landed landed[4096];
  int nb, nl;
  snprintf(path, sizeof(path), "%s/Jammer MIDI.mid", dir);
  if (!read_midi(path, beats, &nb, 16384, landed, &nl, 4096)) {
    fprintf(stderr, "can't read %s\n", path);
    free(x);
    return;
  }
  static int intended[4096];
  int ni = spec[0] ? parse_intended(spec, nb, intended, 4096) : 0;
  printf("%s: %d pedal beats, %d numbers landed live\n", dir, nb, nl);

  NrStream* s = calloc(1, sizeof(NrStream));
  NrParams sp = params;
  sp.hangover = NR_PAD;  // as speech.h listens
  nr_stream_init(s, rate, sp, on_utterance, NULL);
  s->on_overlong = on_overlong;
  n_calls = 0;
  for (long long i = 0; i < n; i += 480) {
    nr_stream_push(s, x + i, (int)(n - i < 480 ? n - i : 480));
  }

  Score sc = {0};
  SpeechRecent recent = {0};
  for (int j = 0; j < nl; j++) {
    const Landed* live = &landed[j];
    // Its call: the last thing said, more than a click, that began between
    // 3.5s and 0.4s before it landed.
    const Call* c = NULL;
    for (int i = 0; i < n_calls; i++) {
      if (calls[i].onset > live->t - 3.5 && calls[i].onset < live->t - 0.4 &&
          calls[i].end - calls[i].onset >= 0.2) {
        c = &calls[i];
      }
    }
    double last, beat;
    if (!pedals_at(beats, nb, live->t, &last, &beat, NULL)) {
      speech_recent_clear(&recent);
      continue;
    }
    // The pedals stopped since the last change: what came before is over.
    long prev = recent.n ? recent.beat[recent.n - 1] : -1;
    if (prev >= 0 && prev < nb &&
        beats_stopped_between(beats, nb, beats[prev], live->t)) {
      speech_recent_clear(&recent);
    }
    int live_beat = beat_at(beats, nb, live->t, beat);
    int want = live_beat;
    if (ni) {
      want = -1;
      int best = 3;
      for (int k = 0; k < ni; k++) {
        int d = abs(intended[k] - live_beat);
        if (d < best) { best = d; want = intended[k]; }
      }
      if (want < 0) continue;
    }
    sc.calls++;
    // Where this change lands, for the ones after it: the simulated landing
    // if there is one, else where it landed live.
    long landed_beat = live_beat;
    bool borderline = false;
    if (!c) {
      speech_recent_add(&recent, landed_beat, false);
      sc.apple++;
      printf("  %7.2fs %d for beat %3d (live: %3d): nothing found said\n",
             live->t, live->number, want, live_beat);
      continue;
    }
    double lead = want - position(beats, nb, c->onset);
    char what[64];
    int lands = -1;
    if (c->heard < 0) {
      sc.apple++;
      snprintf(what, sizeof(what), "left to apple%s",
               c->overlong ? " (too long)" : "");
    } else if (c->number != live->number) {
      sc.wrong++;
      snprintf(what, sizeof(what), "WRONG: heard %d", c->number);
    } else {
      double plast, pbeat;
      int pindex;
      if (!pedals_at(beats, nb, c->heard, &plast, &pbeat, &pindex)) {
        pedals_at(beats, nb, live->t, &plast, &pbeat, &pindex);
      }
      SpeechLandingTime l = speech_landing_time(
        (uint64_t)(c->anchor * NS), (uint64_t)(pbeat * NS),
        (uint64_t)(plast * NS), pindex, (uint64_t)(c->heard * NS),
        use_recent ? &recent : NULL);
      lands = beat_at(beats, nb, l.due / NS, pbeat);
      landed_beat = lands;
      borderline = l.borderline;
      if (getenv("LIVEVAL_DEBUG")) {
        printf("    debug: pedal %d at %.3f, heard %.3f, beat %.3f: lands "
               "beat %ld (index %d)%s, phase %d\n", pindex, plast, c->heard,
               pbeat, l.beat, lands, l.borderline ? ", borderline" : "",
               use_recent ? speech_bar_phase(&recent, l.beat, NULL) : -1);
      }
      if (lands == want) {
        sc.on_time++;
        snprintf(what, sizeof(what), "on time");
      } else {
        if (lands < want) sc.early++; else sc.late++;
        snprintf(what, sizeof(what), "%s, at %d", lands < want ? "EARLY" :
                 "LATE", lands);
      }
    }
    speech_recent_add(&recent, landed_beat, borderline);
    if (verbose || lands != want) {
      printf("  %7.2fs %d for beat %3d (live: %3d)  started %.2f beats "
             "ahead, %3.0fms long, heard %3.0fms after it began: %s\n",
             c->onset, live->number, want, live_beat, lead,
             (c->end - c->onset) * 1000,
             c->heard < 0 ? 0 : (c->heard - c->onset) * 1000, what);
    }
  }
  printf("  %d calls: %d on time, %d early, %d late, %d heard wrong, %d left "
         "to apple\n", sc.calls, sc.on_time, sc.early, sc.late, sc.wrong,
         sc.apple);
  total->calls += sc.calls;
  total->on_time += sc.on_time;
  total->early += sc.early;
  total->late += sc.late;
  total->wrong += sc.wrong;
  total->apple += sc.apple;
  free(x);
  nr_stream_free(s);
  free(s);
}

static bool set_param(NrParams* p, const char* arg) {
  const char* eq = strchr(arg, '=');
  if (!eq) return false;
  double v = atof(eq + 1);
  size_t len = (size_t)(eq - arg);
#define P(name, field) \
  if (len == strlen(name) && strncmp(arg, name, len) == 0) { \
    p->field = (__typeof__(p->field))v; return true; }
  P("trigger_db", trigger_db) P("floor_db", floor_db) P("noise_db", noise_db)
  P("rel_db", rel_db) P("end_hangover", end_hangover) P("pre_quiet", pre_quiet)
  P("max_back", max_back) P("accept", accept) P("margin", margin)
#undef P
  if (len == 4 && !strncmp(arg, "lead", 4)) { beats_after_onset = v; return true; }
  if (len == 3 && !strncmp(arg, "bar", 3)) { use_recent = v != 0; return true; }
  if (len == 3 && !strncmp(arg, "amb", 3)) { borderline = v; return true; }
  if (len == 5 && !strncmp(arg, "vowel", 5)) { anchor_db = v; return true; }
  if (len == 5 && !strncmp(arg, "chord", 5)) { chord_beats = (int)v; return true; }
  if (len == 6 && !strncmp(arg, "memory", 6)) { memory_beats = (int)v; return true; }
  if (len == 6 && !strncmp(arg, "bvotes", 6)) { borderline_votes = (int)v; return true; }
  if (len == 5 && !strncmp(arg, "shift", 5)) { shift_after = (int)v; return true; }
  if (len == 4 && !strncmp(arg, "conf", 4)) { confident = (int)v; return true; }
  return false;
}

int main(int argc, char** argv) {
  params = nr_default_params(-20);
  beats_after_onset = SPEECH_BEATS_AFTER_ONSET_DEFAULT;
  borderline = SPEECH_BORDERLINE_DEFAULT;
  chord_beats = SPEECH_CHORD_BEATS_DEFAULT;
  memory_beats = SPEECH_MEMORY_BEATS_DEFAULT;
  shift_after = SPEECH_SHIFT_AFTER_DEFAULT;
  confident = SPEECH_CONFIDENT_DEFAULT;
  bool verbose = false;
  const char* recordings[256];
  int n_recordings = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) {
      verbose = true;
    } else if (!strchr(argv[i], '/') && set_param(&params, argv[i])) {
      // a parameter
    } else if (n_recordings < 256) {
      recordings[n_recordings++] = argv[i];
    }
  }
  if (!n_recordings) {
    fprintf(stderr, "usage: liveval [-v] [name=value ...] "
            "REC_DIR[=INTENDED] ...\n");
    return 1;
  }
  learn();
  printf("landing %.2f beats after the onset, borderline %.2f%s\n",
         SPEECH_BEATS_AFTER_ONSET, SPEECH_BORDERLINE,
         use_recent ? "" : ", by time alone");
  Score total = {0};
  for (int i = 0; i < n_recordings; i++) run(recordings[i], verbose, &total);
  if (n_recordings > 1) {
    printf("all: %d calls: %d on time, %d early, %d late, %d heard wrong, "
           "%d left to apple\n", total.calls, total.on_time, total.early,
           total.late, total.wrong, total.apple);
  }
  nr_model_free(&model);
  (void)nr_find_unusual;  // numrec-eval's
  return 0;
}
