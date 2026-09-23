#ifndef JML_NUMREC_H
#define JML_NUMREC_H

// The fast number recognizer: hears "one" to "seven", in your voice, a
// moment after the word ends, where Apple's recognizer can take the better
// part of a second.  It knows nothing else, and speech.h still has Apple's
// for everything else; see speech_fast_heard.
//
// It works by comparison with recordings of you (numtrain.h makes them):
//
//   microphone -> 10ms frames -> is someone talking? -> the utterance ->
//     compared with every recording -> the nearest one's word, if near enough
//
// Each 10ms frame is described by its MFCCs -- the shape of its spectrum on
// a pitch-like scale, which is what tells vowels and consonants apart -- and
// its loudness.  An utterance starts when a frame is loud enough to get
// through the speech gate, reaching back to where it rose out of the quiet,
// and ends at the last frame that isn't quiet once end_hangover frames of
// quiet have followed.  Quiet is relative: well below the utterance's own
// peak, or down near the room's noise.
//
// Only a whole word is ever taken: the utterance is matched end to end
// against whole recorded words, by dynamic time warping, which lines two
// sayings of a word up however their parts were stretched.  The recordings
// are cut at end_hangover, long enough that no word is cut off at a gap
// inside it -- the "k" of "six" is up to 150ms of nothing.
//
// Waiting that long after every word would be slow, though, and most words
// have no gap inside them.  So each word waits as long as it needs to: once
// `hangover` frames of quiet have gone by, what's been heard is matched
// every 10ms, and it's taken once the nearest word's own wait is up -- a bit
// more than the longest gap inside most of your recordings of it.  "two"
// goes almost at once; "si" of "six" is nearest to "six", which waits for
// its "s", and then it's the whole word that's matched.
//
// It's the nearest recording that decides, and some recordings are of words
// that aren't numbers ("fine", "set", "sing", "press"...), so a word that
// sounds more like one of those than like any number is nothing.  And the
// utterance must follow NR_PRE_QUIET of quiet, so the "two" of "press room
// two" can't count even if there's a gap before it.
//
// Plain C, so numrec-eval.c can run recordings through exactly this.  Not
// realtime: it runs on the speech queue.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NR_MELS 26
#define NR_CEPS 12
#define NR_DIM (NR_CEPS + 1)  // loudness, then the cepstra
#define NR_RING 512           // frames kept: 5s
#define NR_MAX_FRAMES 120     // longer than this isn't one number
#define NR_MIN_FRAMES 8       // shorter than this is a click
#define NR_PAD 3              // frames either side of the utterance
#define NR_OTHER 7            // the label for "not a number"
#define NR_N_LABELS 8

// A frame's features padded out to 16 floats, for matching four at a time.
typedef float NrV4 __attribute__((vector_size(16)));
typedef struct { NrV4 v[4]; } NrFrame;
_Static_assert(NR_DIM <= 16, "a frame's features must fit an NrFrame");

static const char* NR_WORDS[NR_N_LABELS] = {
  "one", "two", "three", "four", "five", "six", "seven", "other",
};

typedef struct {
  float trigger_db;  // a frame's peak above this starts an utterance
  float floor_db;    // a frame's RMS below this is always quiet
  float noise_db;    // ... and below the room's noise plus this
  float rel_db;      // ... and below the utterance's peak minus this
  int hangover;      // frames of quiet before anything is decided
  int end_hangover;  // frames of quiet that end an utterance for certain
  int pre_quiet;     // frames of quiet needed before one
  int max_back;      // how far before the trigger an utterance can start
  float accept;      // farther than this from every number is nothing
  float margin;      // the runner-up must be at least this much farther
  float room_db;     // the room's noise, RMS dBFS, before anything's heard
} NrParams;

static NrParams nr_default_params(float trigger_db) {
  return (NrParams){
    .trigger_db = trigger_db, .floor_db = -60, .noise_db = 10, .rel_db = 30,
    .hangover = 4, .end_hangover = 18, .pre_quiet = 30, .max_back = 30,
    .accept = 1.6f, .margin = 0.05f, .room_db = -70,
  };
}

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------

typedef struct {
  int win, hop, nfft;
  float* window;
  float* history;  // the last `win` samples, pre-emphasized, a ring
  int head;        // where the oldest of them is
  int filled;      // samples since the last frame
  float last_in;   // for pre-emphasis
  int mel_lo[NR_MELS], mel_hi[NR_MELS];
  float* mel_w;    // NR_MELS x (nfft/2+1)
  float dct[NR_CEPS][NR_MELS];
  float* re;       // FFT scratch
  float* im;
} NrFeatures;

static void nr_fft(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len;
    float wr = (float)cos(ang), wi = (float)sin(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        float xr = re[b] * cr - im[b] * ci;
        float xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr; im[a] += xi;
        float t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = t;
      }
    }
  }
}

static double nr_mel(double hz) { return 2595 * log10(1 + hz / 700); }
static double nr_hz(double mel) { return 700 * (pow(10, mel / 2595) - 1); }

// 25ms windows every 10ms, whatever the sample rate.
static void nr_features_init(NrFeatures* f, double rate) {
  memset(f, 0, sizeof(*f));
  f->win = (int)lround(rate * 0.025);
  f->hop = (int)lround(rate * 0.010);
  f->nfft = 1;
  while (f->nfft < f->win) f->nfft <<= 1;
  f->window = malloc(sizeof(float) * (size_t)f->win);
  for (int i = 0; i < f->win; i++) {
    f->window[i] = (float)(0.54 - 0.46 * cos(2 * M_PI * i / (f->win - 1)));
  }
  f->history = calloc((size_t)f->win, sizeof(float));
  int bins = f->nfft / 2 + 1;
  f->mel_w = calloc((size_t)NR_MELS * (size_t)bins, sizeof(float));
  double lo = nr_mel(80), hi = nr_mel(fmin(7600, rate / 2));
  for (int m = 0; m < NR_MELS; m++) {
    double l = nr_hz(lo + (hi - lo) * m / (NR_MELS + 1));
    double c = nr_hz(lo + (hi - lo) * (m + 1) / (NR_MELS + 1));
    double r = nr_hz(lo + (hi - lo) * (m + 2) / (NR_MELS + 1));
    f->mel_lo[m] = bins;
    f->mel_hi[m] = 0;
    for (int b = 0; b < bins; b++) {
      double hz = b * rate / f->nfft;
      double w = hz <= l || hz >= r ? 0 : hz < c ? (hz - l) / (c - l)
                                                 : (r - hz) / (r - c);
      if (w <= 0) continue;
      f->mel_w[m * bins + b] = (float)w;
      if (b < f->mel_lo[m]) f->mel_lo[m] = b;
      if (b + 1 > f->mel_hi[m]) f->mel_hi[m] = b + 1;
    }
  }
  for (int k = 0; k < NR_CEPS; k++) {
    for (int m = 0; m < NR_MELS; m++) {
      f->dct[k][m] = (float)cos(M_PI / NR_MELS * (m + 0.5) * (k + 1));
    }
  }
  f->re = malloc(sizeof(float) * (size_t)f->nfft);
  f->im = malloc(sizeof(float) * (size_t)f->nfft);
}

static void nr_features_free(NrFeatures* f) {
  free(f->window); free(f->history); free(f->mel_w); free(f->re);
  free(f->im);
}

// One frame from the last `win` samples: its features, and the loudness of
// its newest 10ms, as RMS and peak in dBFS.
static void nr_frame(NrFeatures* f, float* feat, float* rms_db,
                     float* peak_db, float hop_sq, float hop_peak) {
  memset(f->im, 0, sizeof(float) * (size_t)f->nfft);
  memset(f->re, 0, sizeof(float) * (size_t)f->nfft);
  double energy = 0;
  for (int i = 0; i < f->win; i++) {
    int k = f->head + i;
    if (k >= f->win) k -= f->win;
    float v = f->history[k] * f->window[i];
    f->re[i] = v;
    energy += (double)v * v;
  }
  nr_fft(f->re, f->im, f->nfft);
  int bins = f->nfft / 2 + 1;
  float logmel[NR_MELS];
  for (int m = 0; m < NR_MELS; m++) {
    double sum = 0;
    for (int b = f->mel_lo[m]; b < f->mel_hi[m]; b++) {
      sum += f->mel_w[m * bins + b] *
             ((double)f->re[b] * f->re[b] + (double)f->im[b] * f->im[b]);
    }
    logmel[m] = (float)log(sum + 1e-10);
  }
  feat[0] = (float)log(energy + 1e-10);
  for (int k = 0; k < NR_CEPS; k++) {
    float c = 0;
    for (int m = 0; m < NR_MELS; m++) c += f->dct[k][m] * logmel[m];
    feat[1 + k] = c;
  }
  *rms_db = 10 * log10f(hop_sq / f->hop + 1e-12f);
  *peak_db = 20 * log10f(hop_peak + 1e-9f);
}

// ---------------------------------------------------------------------------
// Utterances
// ---------------------------------------------------------------------------

typedef struct NrStream NrStream;
// An utterance may have ended: frames `onset` to `end`, inclusive, followed
// by `quiet` frames of quiet.  Called every frame from `hangover` frames of
// quiet on, with `final` at end_hangover, when it has ended for certain.
// `clear` if it followed enough quiet to count.  Return true once it's been
// decided, and it won't be asked about again.
typedef bool (*NrUtteranceFn)(void* ctx, NrStream* s, long long onset,
                              long long end, bool clear, int quiet,
                              bool final);

struct NrStream {
  NrParams p;
  NrFeatures f;
  float feat[NR_RING][NR_DIM];
  float rms[NR_RING];
  float peak[NR_RING];
  long long frames;       // frames so far; the newest is frames - 1
  float hop_sq, hop_peak; // the 10ms being filled
  float noise;            // the room, RMS dBFS
  bool active;            // in an utterance
  bool overlong;          // ... one too long to be a number
  bool decided;           // ... one already decided
  long long onset, last_voiced;
  float utt_peak;
  int quiet_run;
  long long last_end;     // where the last utterance ended
  NrUtteranceFn on_utterance;
  void* ctx;
};

static void nr_stream_init(NrStream* s, double rate, NrParams p,
                           NrUtteranceFn fn, void* ctx) {
  memset(s, 0, sizeof(*s));
  s->p = p;
  nr_features_init(&s->f, rate);
  s->noise = p.room_db;
  s->last_end = -1000000;
  s->on_utterance = fn;
  s->ctx = ctx;
}

static void nr_stream_free(NrStream* s) { nr_features_free(&s->f); }

#define NR_AT(s, field, i) ((s)->field[(i) & (NR_RING - 1)])

static float nr_quiet_below(const NrStream* s) {
  float q = fmaxf(s->p.floor_db, s->noise + s->p.noise_db);
  return s->active ? fmaxf(q, s->utt_peak - s->p.rel_db) : q;
}

static void nr_stream_frame(NrStream* s) {
  long long t = s->frames++;
  nr_frame(&s->f, NR_AT(s, feat, t), &NR_AT(s, rms, t), &NR_AT(s, peak, t),
           s->hop_sq, s->hop_peak);
  float rms = NR_AT(s, rms, t);

  if (!s->active) {
    // The room: falls at once, rises slowly, so talking barely moves it.
    s->noise = rms < s->noise ? rms : s->noise + 0.002f * (rms - s->noise);
    if (NR_AT(s, peak, t) < s->p.trigger_db) return;
    s->active = true;
    s->overlong = false;
    s->decided = false;
    s->utt_peak = rms;
    s->quiet_run = 0;
    s->last_voiced = t;
    float quiet = nr_quiet_below(s);
    long long onset = t;
    while (onset - 1 > s->last_end && t - (onset - 1) <= s->p.max_back &&
           NR_AT(s, rms, onset - 1) > quiet) {
      onset--;
    }
    s->onset = onset;
    return;
  }

  if (rms > s->utt_peak) s->utt_peak = rms;
  if (rms > nr_quiet_below(s)) {
    s->last_voiced = t;
    s->quiet_run = 0;
  } else {
    s->quiet_run++;
  }
  if (t - s->onset + 1 > NR_MAX_FRAMES) s->overlong = true;
  if (s->quiet_run < s->p.hangover) return;

  bool final = s->quiet_run >= s->p.end_hangover;
  long long onset = s->onset, end = s->last_voiced;
  bool clear = onset - s->last_end > s->p.pre_quiet;
  bool click = end - onset + 1 < NR_MIN_FRAMES;
  if (!click && !s->overlong && !s->decided && s->on_utterance &&
      s->quiet_run > 0) {
    s->decided = s->on_utterance(s->ctx, s, onset, end, clear, s->quiet_run,
                                 final);
  }
  if (!final) return;
  s->active = false;
  if (!click) s->last_end = end;  // a click is as if unheard
}

// The longest run of quiet inside an utterance, in frames.
static int nr_longest_gap(const NrStream* s, long long onset, long long end) {
  float quiet = nr_quiet_below(s);
  int run = 0, longest = 0;
  for (long long i = onset; i <= end; i++) {
    run = NR_AT(s, rms, i) > quiet ? 0 : run + 1;
    if (run > longest) longest = run;
  }
  return longest;
}

static void nr_stream_push(NrStream* s, const float* in, int n) {
  NrFeatures* f = &s->f;
  for (int i = 0; i < n; i++) {
    float v = in[i] - 0.97f * f->last_in;
    f->last_in = in[i];
    f->history[f->head] = v;
    if (++f->head == f->win) f->head = 0;
    s->hop_sq += in[i] * in[i];
    float a = fabsf(in[i]);
    if (a > s->hop_peak) s->hop_peak = a;
    if (++f->filled == f->hop) {
      nr_stream_frame(s);
      f->filled = 0;
      s->hop_sq = s->hop_peak = 0;
    }
  }
}

// The utterance's frames, padded, into `out` (room for NR_MAX_FRAMES +
// 2 * NR_PAD); the count is returned.  Loudness is made relative to the
// utterance's loudest frame, since how loud you said it isn't what it was.
static int nr_utterance(const NrStream* s, long long onset, long long end,
                        float (*out)[NR_DIM]) {
  long long from = onset - NR_PAD, to = end + NR_PAD;
  if (to > s->frames - 1) to = s->frames - 1;
  if (from < s->frames - NR_RING) from = s->frames - NR_RING;
  if (from < 0) from = 0;
  int n = 0;
  float loudest = -1e30f;
  for (long long i = from; i <= to && n < NR_MAX_FRAMES + 2 * NR_PAD; i++) {
    memcpy(out[n], NR_AT(s, feat, i), sizeof(out[n]));
    if (out[n][0] > loudest) loudest = out[n][0];
    n++;
  }
  for (int i = 0; i < n; i++) out[i][0] -= loudest;
  return n;
}

// ---------------------------------------------------------------------------
// The recordings, and matching against them
// ---------------------------------------------------------------------------

typedef struct {
  int label;          // 0-6 for one to seven, NR_OTHER
  int n;
  float (*feat)[NR_DIM];  // normalized
  NrFrame* frames;    // the same, for matching
  int gap;            // the longest quiet inside it, in frames
  int session;        // where it came from, so a test can leave it out
  long long sample;   // where in that session it starts, and ends
  long long end;
  bool reviewed;      // a person has said what it is: see NrReview
} NrTemplate;

typedef struct {
  NrTemplate* t;
  int n, cap;
  float mean[NR_DIM], sd[NR_DIM];
  int wait[NR_N_LABELS];  // frames of quiet before each word is taken
} NrModel;

static void nr_model_add(NrModel* m, int label, float (*feat)[NR_DIM], int n,
                         int gap, int session, long long sample,
                         long long end, bool reviewed) {
  if (m->n == m->cap) {
    m->cap = m->cap ? m->cap * 2 : 64;
    m->t = realloc(m->t, sizeof(NrTemplate) * (size_t)m->cap);
  }
  NrTemplate* t = &m->t[m->n++];
  t->label = label;
  t->n = n;
  t->feat = malloc(sizeof(float) * NR_DIM * (size_t)n);
  memcpy(t->feat, feat, sizeof(float) * NR_DIM * (size_t)n);
  t->frames = NULL;  // made by nr_model_finish
  t->gap = gap;
  t->session = session;
  t->sample = sample;
  t->end = end;
  t->reviewed = reviewed;
}

static void nr_model_free(NrModel* m) {
  for (int i = 0; i < m->n; i++) {
    free(m->t[i].feat);
    free(m->t[i].frames);
  }
  free(m->t);
  memset(m, 0, sizeof(*m));
}

static void nr_pad_frames(float (*feat)[NR_DIM], int n, NrFrame* out) {
  memset(out, 0, sizeof(NrFrame) * (size_t)n);
  for (int i = 0; i < n; i++) memcpy(&out[i], feat[i], sizeof(feat[i]));
}

static int nr_int_cmp(const void* a, const void* b) {
  return *(const int*)a - *(const int*)b;
}

// Once everything's been added.  Every dimension to mean 0, standard
// deviation 1, over the numbers' frames, so that no one of them counts for
// more just by varying more.  And each word's wait: 20ms more than the
// longest gap in 90% of its recordings, since the longest of all is usually
// a breath or a click after it rather than part of it.
static void nr_model_finish(NrModel* m, const NrParams* p) {
  for (int l = 0; l < NR_N_LABELS; l++) {
    int gaps[1024], n = 0;
    for (int i = 0; i < m->n && n < 1024; i++) {
      if (m->t[i].label == l) gaps[n++] = m->t[i].gap;
    }
    int wait = p->end_hangover;
    if (n > 0 && l != NR_OTHER) {
      qsort(gaps, (size_t)n, sizeof(int), nr_int_cmp);
      wait = gaps[(int)(0.9 * (n - 1))] + 2;
    }
    if (wait < p->hangover) wait = p->hangover;
    if (wait > p->end_hangover) wait = p->end_hangover;
    m->wait[l] = wait;
  }
  double sum[NR_DIM] = {0}, sq[NR_DIM] = {0};
  long long count = 0;
  for (int i = 0; i < m->n; i++) {
    if (m->t[i].label == NR_OTHER) continue;
    for (int j = 0; j < m->t[i].n; j++) {
      for (int d = 0; d < NR_DIM; d++) {
        sum[d] += m->t[i].feat[j][d];
        sq[d] += (double)m->t[i].feat[j][d] * m->t[i].feat[j][d];
      }
      count++;
    }
  }
  for (int d = 0; d < NR_DIM; d++) {
    m->mean[d] = count ? (float)(sum[d] / count) : 0;
    double var = count ? sq[d] / count - (double)m->mean[d] * m->mean[d] : 1;
    m->sd[d] = var > 1e-6 ? (float)sqrt(var) : 1;
  }
  for (int i = 0; i < m->n; i++) {
    for (int j = 0; j < m->t[i].n; j++) {
      for (int d = 0; d < NR_DIM; d++) {
        m->t[i].feat[j][d] = (m->t[i].feat[j][d] - m->mean[d]) / m->sd[d];
      }
    }
    m->t[i].frames = malloc(sizeof(NrFrame) * (size_t)m->t[i].n);
    nr_pad_frames(m->t[i].feat, m->t[i].n, m->t[i].frames);
  }
}

// Average distance per step along the best alignment of the two, end to
// end.  Anything more than twice the other's length isn't the same word.
// Nearly all of the time goes on the distances between frames, which is why
// they're padded to be done four features at a time.
static float nr_dtw(const NrFrame* a, int n, const NrFrame* b, int m) {
  if (n > 2 * m || m > 2 * n) return INFINITY;
  float prev[NR_MAX_FRAMES + 2 * NR_PAD + 1];
  float cur[NR_MAX_FRAMES + 2 * NR_PAD + 1];
  if (m > NR_MAX_FRAMES + 2 * NR_PAD) return INFINITY;
  prev[0] = 0;
  for (int j = 1; j <= m; j++) prev[j] = INFINITY;
  for (int i = 1; i <= n; i++) {
    const NrFrame x = a[i - 1];
    cur[0] = INFINITY;
    for (int j = 1; j <= m; j++) {
      NrV4 d0 = x.v[0] - b[j - 1].v[0], d1 = x.v[1] - b[j - 1].v[1];
      NrV4 d2 = x.v[2] - b[j - 1].v[2], d3 = x.v[3] - b[j - 1].v[3];
      NrV4 sq = d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
      float d = sqrtf(sq[0] + sq[1] + sq[2] + sq[3]);
      float best = prev[j - 1];
      if (prev[j] < best) best = prev[j];
      if (cur[j - 1] < best) best = cur[j - 1];
      cur[j] = d + best;
    }
    memcpy(prev, cur, sizeof(float) * (size_t)(m + 1));
  }
  return prev[m] / (float)(n + m);
}

// ---------------------------------------------------------------------------
// Recordings worth a person's ear
//
// One that sounds more like a different word than like any other recording
// of its own: of all the rest, its nearest is a recording of another word --
// or, for something that isn't a number, a number near enough to be taken
// for it.  Said wrong to its prompt, a cough, or just odd, and either way
// it's moving what the recognizer does, so numheard.h asks you about the
// ones nobody has reviewed yet.  Worst first: how much nearer the other word
// is than its own -- and only where it's clearly nearer, NR_UNUSUAL times,
// since a recording that really is out of place makes the ones it sits among
// look a little out of place too.
// ---------------------------------------------------------------------------

#define NR_UNUSUAL 1.1

typedef struct {
  int index;    // into the model's recordings
  int nearest;  // the other word it's nearest to
  float score;  // its own word's distance over the other's: over NR_UNUSUAL
} NrUnusual;

// Up to `max` of them, worst first, from a finished model.  Every recording
// against every other: a second or two, not for the audio thread.
static int nr_find_unusual(const NrModel* m, const NrParams* p,
                           NrUnusual* out, int max) {
  int found = 0;
  for (int i = 0; i < m->n; i++) {
    const NrTemplate* t = &m->t[i];
    if (t->reviewed) continue;
    float own = INFINITY, other = INFINITY;
    int other_label = -1;
    for (int j = 0; j < m->n; j++) {
      if (j == i) continue;
      float d = nr_dtw(t->frames, t->n, m->t[j].frames, m->t[j].n);
      if (m->t[j].label == t->label) {
        if (d < own) own = d;
      } else if (d < other) {
        other = d;
        other_label = m->t[j].label;
      }
    }
    if (other_label < 0 || !(other * NR_UNUSUAL < own)) continue;
    if (t->label == NR_OTHER && other > p->accept) continue;
    NrUnusual u = {i, other_label, isinf(own) ? INFINITY : own / other};
    // Kept in order, worst first; past `max`, the mildest goes.
    int at = found < max ? found++ : max;
    while (at > 0 && out[at - 1].score < u.score) {
      if (at < max) out[at] = out[at - 1];
      at--;
    }
    if (at < max) out[at] = u;
  }
  return found;
}

typedef struct {
  int label;       // what it was, or -1 for nothing
  int nearest;     // the nearest recording's label, taken or not
  float dist;      // to the nearest
  float runner_up; // to the nearest of any other label
  const char* why; // when nothing: why not
} NrResult;

// Which word `feat` (as nr_utterance gives it, not yet normalized) is.
// Recordings from `skip_session` starting near `skip_sample` are left out,
// so a test doesn't match a recording with itself.
static NrResult nr_classify(const NrModel* m, float (*feat)[NR_DIM], int n,
                            const NrParams* p, int skip_session,
                            long long skip_sample, long long skip_within) {
  float q[NR_MAX_FRAMES + 2 * NR_PAD][NR_DIM];
  for (int i = 0; i < n; i++) {
    for (int d = 0; d < NR_DIM; d++) {
      q[i][d] = (feat[i][d] - m->mean[d]) / m->sd[d];
    }
  }
  NrFrame frames[NR_MAX_FRAMES + 2 * NR_PAD];
  nr_pad_frames(q, n, frames);
  float best[NR_N_LABELS];
  for (int l = 0; l < NR_N_LABELS; l++) best[l] = INFINITY;
  for (int i = 0; i < m->n; i++) {
    const NrTemplate* t = &m->t[i];
    if (t->session == skip_session &&
        llabs(t->sample - skip_sample) < skip_within) {
      continue;
    }
    float d = nr_dtw(frames, n, t->frames, t->n);
    if (d < best[t->label]) best[t->label] = d;
  }
  NrResult r = {.label = -1, .nearest = -1, .dist = INFINITY,
                .runner_up = INFINITY, .why = "no recordings"};
  for (int l = 0; l < NR_N_LABELS; l++) {
    if (best[l] < r.dist) r.nearest = l, r.dist = best[l];
  }
  for (int l = 0; l < NR_N_LABELS; l++) {
    if (l != r.nearest && best[l] < r.runner_up) r.runner_up = best[l];
  }
  if (r.nearest < 0) return r;
  if (r.nearest == NR_OTHER) {
    r.why = "not a number";
  } else if (r.dist > p->accept) {
    r.why = "not close enough";
  } else if (r.runner_up - r.dist < p->margin) {
    r.why = "too close to call";
  } else {
    r.label = r.nearest;
    r.why = NULL;
  }
  return r;
}

// ---------------------------------------------------------------------------
// Learning from numtrain.h's recordings
// ---------------------------------------------------------------------------

typedef struct {
  long long sample;
  char text[64];
} NrPrompt;

typedef struct {
  NrPrompt* prompts;
  int n_prompts;
  long long* onsets;  // each utterance, as found
  long long* ends;
  int* gaps;
  float (**feats)[NR_DIM];
  int* n_feats;
  int n, cap;
  int hop;
} NrCollect;

static bool nr_collect_fn(void* ctx, NrStream* s, long long onset,
                          long long end, bool clear, int quiet, bool final) {
  (void)clear; (void)quiet;
  if (!final) return false;
  NrCollect* c = ctx;
  if (c->n == c->cap) {
    c->cap = c->cap ? c->cap * 2 : 256;
    c->onsets = realloc(c->onsets, sizeof(long long) * (size_t)c->cap);
    c->ends = realloc(c->ends, sizeof(long long) * (size_t)c->cap);
    c->gaps = realloc(c->gaps, sizeof(int) * (size_t)c->cap);
    c->feats = realloc(c->feats, sizeof(void*) * (size_t)c->cap);
    c->n_feats = realloc(c->n_feats, sizeof(int) * (size_t)c->cap);
  }
  float buf[NR_MAX_FRAMES + 2 * NR_PAD][NR_DIM];
  int n = nr_utterance(s, onset, end, buf);
  c->onsets[c->n] = onset;
  c->ends[c->n] = end;
  c->gaps[c->n] = nr_longest_gap(s, onset, end);
  c->feats[c->n] = malloc(sizeof(float) * NR_DIM * (size_t)n);
  memcpy(c->feats[c->n], buf, sizeof(float) * NR_DIM * (size_t)n);
  c->n_feats[c->n] = n;
  c->n++;
  return true;
}

// The label a prompt teaches, or -1 for nothing: a number, or NR_OTHER for
// everything said that isn't one.
static int nr_prompt_label(const char* text) {
  for (int l = 0; l < 7; l++) {
    if (strcmp(text, NR_WORDS[l]) == 0) return l;
  }
  if (strcmp(text, "get ready") == 0) return -1;
  return NR_OTHER;
}

// Which prompt was up at `sample`.
static int nr_prompt_at(const NrPrompt* p, int n, long long sample) {
  int found = -1;
  for (int i = 0; i < n && p[i].sample <= sample; i++) found = i;
  return found;
}

// Read a session's prompts: <sample>\t<text>, and "# gate_db\t<db>" and
// "# room_db\t<db>" (numheard.h's clips, which start mid-performance).
static int nr_read_prompts(const char* path, NrPrompt** out, float* gate_db,
                           float* room_db) {
  FILE* f = fopen(path, "r");
  if (!f) return -1;
  int n = 0, cap = 0;
  NrPrompt* p = NULL;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char* tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = 0;
    if (line[0] == '#') {
      if (strcmp(line, "# gate_db") == 0 && gate_db) *gate_db = atof(tab + 1);
      if (strcmp(line, "# room_db") == 0 && room_db) *room_db = atof(tab + 1);
      continue;
    }
    if (n == cap) {
      cap = cap ? cap * 2 : 128;
      p = realloc(p, sizeof(NrPrompt) * (size_t)cap);
    }
    p[n].sample = atoll(line);
    snprintf(p[n].text, sizeof(p[n].text), "%s", tab + 1);
    n++;
  }
  fclose(f);
  *out = p;
  return n;
}

// What a word in a recording really was, from a person who listened to it
// (numheard.h's review): "# review\t<sample>\t<label>" in the recording's
// .tsv, <sample> where the word starts and <label> a number word, "other",
// or "drop" to learn nothing from it.  It wins over whatever prompt was up.
#define NR_DROP (-2)
#define NR_REVIEW_WITHIN_S 0.15  // how near a word's start a review must be

typedef struct {
  long long sample;
  int label;  // 0-6, NR_OTHER, or NR_DROP
} NrReview;

// A review's label, or -1 if it isn't one.
static int nr_review_label(const char* text) {
  for (int l = 0; l < 7; l++) {
    if (strcmp(text, NR_WORDS[l]) == 0) return l;
  }
  if (strcmp(text, "other") == 0) return NR_OTHER;
  if (strcmp(text, "drop") == 0) return NR_DROP;
  return -1;
}

// A recording's reviews, from its .tsv; none if it has no .tsv.
static int nr_read_reviews(const char* path, NrReview** out) {
  *out = NULL;
  FILE* f = fopen(path, "r");
  if (!f) return 0;
  int n = 0, cap = 0;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, "# review\t", 9) != 0) continue;
    char* tab = strchr(line + 9, '\t');
    if (!tab) continue;
    int label = nr_review_label(tab + 1);
    if (label == -1) continue;
    if (n == cap) {
      cap = cap ? cap * 2 : 16;
      *out = realloc(*out, sizeof(NrReview) * (size_t)cap);
    }
    (*out)[n].sample = atoll(line + 9);
    (*out)[n].label = label;
    n++;
  }
  fclose(f);
  return n;
}

// numtrain.h's WAVs: 44-byte header, then 32-bit float mono.
static float* nr_read_wav(const char* path, long long* n, double* rate) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  unsigned char h[44];
  if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4) != 0 ||
      h[20] != 3 || h[22] != 1) {
    fclose(f);
    return NULL;
  }
  *rate = h[24] | h[25] << 8 | h[26] << 16 | (unsigned)h[27] << 24;
  fseek(f, 0, SEEK_END);
  long long bytes = ftell(f) - 44;
  fseek(f, 44, SEEK_SET);
  *n = bytes / 4;
  float* x = malloc(sizeof(float) * (size_t)(*n > 0 ? *n : 1));
  *n = (long long)fread(x, sizeof(float), (size_t)*n, f);
  fclose(f);
  return x;
}

// Learn every word in a session.  A number counts only if it was the one
// thing said while its prompt was up; anything else said -- the words to
// ignore, the talking -- is NR_OTHER.  Except where a person has reviewed
// the word, when it's what they said it was.  `all_reviewed` for a
// recording a person has already labeled as a whole (numheard.h's clips).
// Returns how many were learned.
static int nr_learn_session(NrModel* m, const float* x, long long n_samples,
                            double rate, const NrPrompt* prompts,
                            int n_prompts, const NrReview* reviews,
                            int n_reviews, bool all_reviewed,
                            const NrParams* p, int session) {
  NrCollect c = {.prompts = (NrPrompt*)prompts, .n_prompts = n_prompts};
  NrStream* s = malloc(sizeof(NrStream));
  nr_stream_init(s, rate, *p, nr_collect_fn, &c);
  c.hop = s->f.hop;
  for (long long i = 0; i < n_samples; i += 4096) {
    nr_stream_push(s, x + i, (int)(n_samples - i < 4096 ? n_samples - i
                                                        : 4096));
  }
  int learned = 0;
  long long within = (long long)(NR_REVIEW_WITHIN_S * rate);
  for (int i = 0; i < c.n; i++) {
    long long sample = c.onsets[i] * c.hop;
    const NrReview* review = NULL;
    for (int r = 0; r < n_reviews; r++) {
      if (llabs(reviews[r].sample - sample) <= within &&
          (!review || llabs(reviews[r].sample - sample) <
                      llabs(review->sample - sample))) {
        review = &reviews[r];
      }
    }
    int label;
    if (review) {
      if (review->label == NR_DROP) continue;
      label = review->label;
    } else {
      int k = nr_prompt_at(prompts, n_prompts, sample);
      label = k < 0 ? -1 : nr_prompt_label(prompts[k].text);
      if (label < 0) continue;
      if (label != NR_OTHER) {
        int same = 0;
        for (int j = 0; j < c.n; j++) {
          same += nr_prompt_at(prompts, n_prompts, c.onsets[j] * c.hop) == k;
        }
        if (same != 1) continue;
      }
    }
    nr_model_add(m, label, c.feats[i], c.n_feats[i], c.gaps[i], session,
                 sample, (c.ends[i] + 1) * c.hop,
                 all_reviewed || review != NULL);
    learned++;
  }
  for (int i = 0; i < c.n; i++) free(c.feats[i]);
  free(c.onsets); free(c.ends); free(c.gaps); free(c.feats); free(c.n_feats);
  nr_stream_free(s);
  free(s);
  return learned;
}

#endif
