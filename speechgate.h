#ifndef JML_SPEECH_GATE_H
#define JML_SPEECH_GATE_H

// The gate in front of the speech recognizer: only what's said right into the
// microphone gets through, and everything quieter -- a caller across the
// room, the band -- reaches it as silence.
//
// Decided 10ms at a time on the window's peak.  A window passes if anything
// loud is near it: up to SG_HOLD_WINDOWS before it, so the tail of a word
// isn't cut off, and up to SG_PRE_WINDOWS after it, so its quiet start ("f"
// in "four") isn't either.  Looking ahead means holding all the audio back by
// that much, and the recognizer hearing it that much later, which is time
// straight off how soon a change can land -- so it's none, at the risk of a
// soft first consonant.  If words start being misheard for their beginnings,
// a couple of windows here buys them back for 10ms each.
//
// Also where "stopped talking" comes from: the last loud window.
//
// Plain C, so test-keypad.c can reach it.  Not realtime: it runs on the
// speech queue.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SG_PRE_WINDOWS 0    // no look-ahead; see above
#define SG_HOLD_WINDOWS 25  // 250ms

typedef struct {
  int window;         // samples in 10ms
  float threshold;    // peak, linear, that counts as loud
  float* delay;       // SG_PRE_WINDOWS + 1 windows, oldest at delay_head
  int delay_head;
  int delay_filled;
  float* partial;     // the window being filled
  int partial_n;
  long long index;    // windows seen so far
  long long last_loud;
} SpeechGate;

static void sg_init(SpeechGate* g, double sample_rate) {
  memset(g, 0, sizeof(*g));
  g->window = (int)(sample_rate / 100);
  g->delay = calloc((size_t)g->window * (SG_PRE_WINDOWS + 1), sizeof(float));
  g->partial = calloc((size_t)g->window, sizeof(float));
  g->last_loud = -1000000;
}

// Whether the gate is open: something loud within the last hold.
static bool sg_open(const SpeechGate* g) {
  return g->last_loud >= g->index - SG_HOLD_WINDOWS;
}

// How many windows ago the last loud one was.
static long long sg_windows_since_loud(const SpeechGate* g) {
  return g->index - 1 - g->last_loud;
}

// Gate `n` samples.  What comes out is what went in SG_PRE_WINDOWS earlier,
// or silence where the gate was shut; it's written to `out`, which needs room
// for n + one window, and the count is returned.
static int sg_process(SpeechGate* g, const float* in, int n, float* out) {
  int n_out = 0;
  for (int i = 0; i < n; i++) {
    g->partial[g->partial_n++] = in[i];
    if (g->partial_n < g->window) continue;

    float peak = 0;
    for (int s = 0; s < g->window; s++) {
      float v = g->partial[s] < 0 ? -g->partial[s] : g->partial[s];
      if (v > peak) peak = v;
    }
    if (peak >= g->threshold) g->last_loud = g->index;

    // Into the line; then, once it holds more than the look-ahead, the
    // oldest window goes out, which with no look-ahead is this one.
    const int line = SG_PRE_WINDOWS + 1;
    float* slot = g->delay + (size_t)((g->delay_head + g->delay_filled) %
                                      line) * g->window;
    memcpy(slot, g->partial, sizeof(float) * (size_t)g->window);
    g->delay_filled++;
    if (g->delay_filled == line) {
      float* oldest = g->delay + (size_t)g->delay_head * g->window;
      long long k = g->index - SG_PRE_WINDOWS;
      bool pass = g->last_loud >= k - SG_HOLD_WINDOWS;
      for (int s = 0; s < g->window; s++) out[n_out++] = pass ? oldest[s] : 0;
      g->delay_head = (g->delay_head + 1) % line;
      g->delay_filled--;
    }
    g->partial_n = 0;
    g->index++;
  }
  return n_out;
}

#endif
