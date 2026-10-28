#ifndef JML_RENDER_H
#define JML_RENDER_H

// Playing a recording back through jammer, for new audio.
//
//   jammer-mac --render FOLDER [--midi FILE] [--out FOLDER]
//
// FOLDER is one of VSL Mixer's recordings made with Jammer linked: it has
// what came in on the 1818's inputs 1 and 2 ("09 In 1 (jack).wav", "10 In 2
// (jack).wav") and everything played, "Jammer MIDI.mid" -- the pedals, the
// breath controller, the piano, and jammer's own "Jammer Keys" (what was
// struck on the laptop or said; see "Logging what's played on the laptop" in
// jammer-mac.m).  Edit the MIDI, in a DAW or anything else, and --midi it
// back in.  Out come "Jammer L (render).wav" and "Jammer R (render).wav", to
// line up with the recording's own tracks.
//
// It's the live rig, run faster than real time: the same synth, without an
// audio driver, called a 64-frame block at a time, the way fluidsynth's
// driver calls it; the recorded inputs pushed in just before each block, the
// way the microphone's thread would; jml_tick every millisecond, the way the
// tick thread would; and each message handled the way it would have been
// when it arrived.  All on a simulated clock (macapi.h's now()), so that
// everything that keeps time keeps the recording's.  Levels, gates and the
// rest come from jammer's saved settings, as they are now.
//
// Each MIDI track is taken for the device it's named after, by the same
// rules connect_midi_sources() uses, so keep the tracks apart and keep their
// names when editing.  "Jammer Keys" can be edited like the rest: a strike
// is a note on channel 16 (15 to select), its number the key's virtual
// keycode.
//
// What jammer had been told before the recording started -- voices picked,
// things switched on -- came at the start of the Jammer Keys track, between
// two markers (CC 118 = 127 and 0), and is played first, ahead of the
// recording, with two seconds for drones to come in before it starts.
//
// It won't match what was heard exactly: the drums' takes are random by
// design, speech recognition isn't run again (what it did is replayed
// instead), the talkbox's return is what the microphone heard at the time,
// and settings changed from the menus during the set aren't logged.

#include <AudioToolbox/ExtendedAudioFile.h>
#include <dirent.h>

#define RENDER_BLOCK 64
#define RENDER_CHUNK 4096
#define RENDER_PREROLL_S 2
#define RENDER_TRACK_KEYS 100     // not a SourceKind: jammer's own keys
#define RENDER_TRACK_IGNORED 0

typedef struct {
  double at;          // seconds into the recording
  int order;          // the order read, to keep for messages at the same time
  int track;
  int len;
  unsigned char msg[3];
  bool history;       // jammer's log from before the recording started
} RenderEvent;

typedef struct {
  char name[256];
  int kind;           // SourceKind, or RENDER_TRACK_*
} RenderTrack;

static RenderEvent* render_events;
static int render_n_events, render_cap_events;
static RenderTrack render_tracks[64];
static int render_n_tracks;

static void render_add_event(RenderEvent e) {
  if (render_n_events == render_cap_events) {
    render_cap_events = render_cap_events ? 2 * render_cap_events : 4096;
    render_events = realloc(render_events,
                            (size_t)render_cap_events * sizeof *render_events);
    if (!render_events) die("out of memory");
  }
  e.order = render_n_events;
  render_events[render_n_events++] = e;
}

// ---------------------------------------------------------------------------
// The MIDI file
// ---------------------------------------------------------------------------

typedef struct { long tick; double us_per_quarter; } RenderTempo;

static unsigned long render_be(const unsigned char* p, int n) {
  unsigned long v = 0;
  for (int i = 0; i < n; i++) v = v << 8 | p[i];
  return v;
}

static long render_vlq(const unsigned char* p, long len, long* at) {
  long v = 0;
  for (int i = 0; i < 4 && *at < len; i++) {
    unsigned char b = p[(*at)++];
    v = v << 7 | (b & 0x7f);
    if (!(b & 0x80)) break;
  }
  return v;
}

// Ticks to seconds, through every tempo change in any track.
static double render_seconds(long tick, const RenderTempo* tempos, int n,
                             int division) {
  if (division & 0x8000) {   // SMPTE: frames a second, ticks a frame
    int fps = -(signed char)(division >> 8);
    return (double)tick / (fps * (division & 0xff));
  }
  double s = 0, uspq = 500000;
  long from = 0;
  for (int i = 0; i < n && tempos[i].tick < tick; i++) {
    s += (tempos[i].tick - from) * uspq / 1e6 / division;
    from = tempos[i].tick;
    uspq = tempos[i].us_per_quarter;
  }
  return s + (tick - from) * uspq / 1e6 / division;
}

static int render_tempo_cmp(const void* a, const void* b) {
  long x = ((const RenderTempo*)a)->tick, y = ((const RenderTempo*)b)->tick;
  return x < y ? -1 : x > y;
}

// Reads every channel message, each with its track, and the tracks' names.
static bool render_read_midi(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("can't open %s\n", path); return false; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* d = malloc((size_t)size);
  if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) {
    fclose(f);
    free(d);
    return false;
  }
  fclose(f);
  if (size < 14 || memcmp(d, "MThd", 4) != 0) {
    printf("%s isn't a MIDI file\n", path);
    free(d);
    return false;
  }
  int division = (int)render_be(d + 12, 2);
  if (!(division & 0x8000) && division == 0) division = 960;

  // Two passes: tempos first (any track may have them), then the messages.
  RenderTempo tempos[512];
  int n_tempos = 0;
  for (int pass = 0; pass < 2; pass++) {
    long at = 8 + (long)render_be(d + 4, 4);
    int track = 0;
    while (at + 8 <= size) {
      long len = (long)render_be(d + at + 4, 4);
      bool is_track = memcmp(d + at, "MTrk", 4) == 0;
      const unsigned char* t = d + at + 8;
      at += 8 + len;
      if (!is_track || at > size) continue;
      if (pass == 1 && track < 64) render_tracks[track].name[0] = 0;
      long p = 0, tick = 0;
      unsigned char status = 0;
      while (p < len) {
        tick += render_vlq(t, len, &p);
        if (p >= len) break;
        if (t[p] & 0x80) status = t[p++];
        if (status == 0xFF) {
          if (p >= len) break;
          unsigned char type = t[p++];
          long n = render_vlq(t, len, &p);
          if (p + n > len) break;
          if (pass == 0 && type == 0x51 && n == 3 && n_tempos < 512) {
            tempos[n_tempos++] = (RenderTempo){tick, (double)render_be(t + p, 3)};
          }
          if (pass == 1 && type == 0x03 && track < 64) {
            int m = n < 255 ? (int)n : 255;
            memcpy(render_tracks[track].name, t + p, (size_t)m);
            render_tracks[track].name[m] = 0;
          }
          p += n;
          status = 0;
        } else if (status == 0xF0 || status == 0xF7) {
          p += render_vlq(t, len, &p);
          status = 0;
        } else if (status >= 0x80) {
          int n = ((status & 0xE0) == 0xC0) ? 1 : 2;
          if (p + n > len) break;
          if (pass == 1 && track < 64) {
            RenderEvent e = {0};
            e.at = render_seconds(tick, tempos, n_tempos, division);
            e.track = track;
            e.len = n + 1;
            e.msg[0] = status;
            memcpy(e.msg + 1, t + p, (size_t)n);
            render_add_event(e);
          }
          p += n;
        } else {
          break;   // data with no status: give up on this track
        }
      }
      track++;
      if (pass == 1) render_n_tracks = track < 64 ? track : 64;
    }
    if (pass == 0) {
      qsort(tempos, (size_t)n_tempos, sizeof *tempos, render_tempo_cmp);
    }
  }
  free(d);
  return true;
}

// What connect_midi_sources() would have taken each track's device for.
static void render_classify_tracks(void) {
  bool have_piano = false;
  for (int i = 0; i < render_n_tracks; i++) {
    const char* n = render_tracks[i].name;
    int kind = RENDER_TRACK_IGNORED;
    if (strcasestr(n, "Jammer Keys")) kind = RENDER_TRACK_KEYS;
    else if (strcasestr(n, "mio") || strcasestr(n, "DTX")) kind = SRC_FEET;
    else if (strcasestr(n, "Breath Controller")) kind = SRC_BREATH;
    else if (strcasestr(n, "USB MIDI Interface") || strcasestr(n, "Piano") ||
             strcasestr(n, "Roland")) {
      kind = SRC_PIANO;
      have_piano = true;
    }
    render_tracks[i].kind = kind;
  }
  // As live: an unknown device is the piano, if there's no known one.
  for (int i = 0; i < render_n_tracks && !have_piano; i++) {
    const char* n = render_tracks[i].name;
    if (render_tracks[i].kind == RENDER_TRACK_IGNORED && n[0] &&
        !strcasestr(n, "IAC") && !strcasestr(n, "jammer") &&
        !strcasestr(n, "VSL Mixer")) {
      bool notes = false;
      for (int e = 0; e < render_n_events && !notes; e++) {
        notes = render_events[e].track == i;
      }
      if (notes) {
        render_tracks[i].kind = SRC_PIANO;
        have_piano = true;
      }
    }
  }
  for (int i = 0; i < render_n_tracks; i++) {
    const int k = render_tracks[i].kind;
    printf("  track %d, %s: %s\n", i, render_tracks[i].name[0]
             ? render_tracks[i].name : "(no name)",
           k == RENDER_TRACK_KEYS ? "jammer's keys" : k == SRC_FEET ? "feet"
           : k == SRC_BREATH ? "breath controller" : k == SRC_PIANO ? "piano"
           : "ignored");
  }
}

// The history, between its markers on the keys track.
static void render_mark_history(void) {
  bool in = false;
  for (int i = 0; i < render_n_events; i++) {
    RenderEvent* e = &render_events[i];
    if (render_tracks[e->track].kind != RENDER_TRACK_KEYS) continue;
    if (e->msg[0] == (0xB0 | KEYS_LOG_STRIKE_CH) &&
        e->msg[1] == KEYS_LOG_HISTORY_CC) {
      in = e->msg[2] != 0;
      e->len = 0;   // the markers themselves do nothing
      continue;
    }
    e->history = in;
  }
}

static int render_event_cmp(const void* a, const void* b) {
  const RenderEvent* x = a;
  const RenderEvent* y = b;
  if (x->history != y->history) return x->history ? -1 : 1;
  if (!x->history && x->at != y->at) return x->at < y->at ? -1 : 1;
  return x->order - y->order;
}

// ---------------------------------------------------------------------------
// Doing what each message did
// ---------------------------------------------------------------------------

static const Key* render_key_for_vk(int vk) {
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].label && KEYS[i].vk == vk) return &KEYS[i];
  }
  return NULL;
}

static int render_unknown_keys;

static void render_keys_message(const unsigned char* m, int len) {
  if (len < 3) return;
  int status = m[0] & 0xf0, ch = m[0] & 0x0f;
  LOCK();
  if (status == 0x90 && m[2] > 0 &&
      (ch == KEYS_LOG_STRIKE_CH || ch == KEYS_LOG_SELECT_CH)) {
    const Key* key = render_key_for_vk(m[1]);
    if (key) strike_key_locked(key, ch == KEYS_LOG_SELECT_CH);
    else render_unknown_keys++;
  } else if (status == 0xB0 && ch == KEYS_LOG_STRIKE_CH) {
    switch (m[1]) {
    case KEYS_LOG_ROOT_CC: change_key(m[2]); break;
    case KEYS_LOG_MODE_CC: musical_mode = m[2]; break;
    case KEYS_LOG_NUMBER_CC: nashville_picks_chord(m[2]); break;
    }
  }
  UNLOCK();
}

static void render_dispatch(const RenderEvent* e) {
  if (e->len == 0) return;
  int kind = render_tracks[e->track].kind;
  if (kind == RENDER_TRACK_KEYS) {
    render_keys_message(e->msg, e->len);
  } else if (kind != RENDER_TRACK_IGNORED) {
    handle_midi_message((SourceKind)kind, e->msg, e->len);
  }
}

// ---------------------------------------------------------------------------
// Audio files
// ---------------------------------------------------------------------------

static ExtAudioFileRef render_open_wav(const char* path, double* rate,
                                       SInt64* frames) {
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
    NULL, (const UInt8*)path, (CFIndex)strlen(path), false);
  ExtAudioFileRef f = NULL;
  OSStatus err = ExtAudioFileOpenURL(url, &f);
  CFRelease(url);
  if (err) return NULL;
  AudioStreamBasicDescription in;
  UInt32 size = sizeof in;
  ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &size, &in);
  size = sizeof *frames;
  ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileLengthFrames, &size,
                          frames);
  *rate = in.mSampleRate;
  AudioStreamBasicDescription client = {
    .mSampleRate = in.mSampleRate, .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
    .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
    .mChannelsPerFrame = 1, .mBitsPerChannel = 32};
  if (in.mChannelsPerFrame != 1 ||
      ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat,
                              sizeof client, &client)) {
    ExtAudioFileDispose(f);
    return NULL;
  }
  return f;
}

static ExtAudioFileRef render_create_wav(const char* path, double rate) {
  AudioStreamBasicDescription file = {
    .mSampleRate = rate, .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
    .mBytesPerPacket = 3, .mFramesPerPacket = 1, .mBytesPerFrame = 3,
    .mChannelsPerFrame = 1, .mBitsPerChannel = 24};
  AudioStreamBasicDescription client = {
    .mSampleRate = rate, .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
    .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
    .mChannelsPerFrame = 1, .mBitsPerChannel = 32};
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
    NULL, (const UInt8*)path, (CFIndex)strlen(path), false);
  ExtAudioFileRef f = NULL;
  OSStatus err = ExtAudioFileCreateWithURL(url, kAudioFileWAVEType, &file, NULL,
                                           kAudioFileFlags_EraseFile, &f);
  CFRelease(url);
  if (!err) {
    err = ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat,
                                  sizeof client, &client);
  }
  if (err) {
    if (f) ExtAudioFileDispose(f);
    return NULL;
  }
  return f;
}

// Up to `n` frames into `out`, zeros past the end.
static void render_read(ExtAudioFileRef f, float* out, UInt32 n) {
  UInt32 got = n;
  AudioBufferList l = {1, {{1, n * sizeof(float), out}}};
  if (ExtAudioFileRead(f, &got, &l) != noErr) got = 0;
  if (got < n) memset(out + got, 0, (n - got) * sizeof(float));
}

static void render_write(ExtAudioFileRef f, float* in, UInt32 n) {
  AudioBufferList l = {1, {{1, n * sizeof(float), in}}};
  ExtAudioFileWrite(f, n, &l);
}

// The file in `dir` whose name ends with `suffix`, as VSL Mixer names them
// ("09 In 1 (jack).wav", the number depending on the inputs before it).
static bool render_find(const char* dir, const char* suffix, char* out,
                        size_t len) {
  DIR* d = opendir(dir);
  if (!d) return false;
  struct dirent* ent;
  bool found = false;
  size_t sl = strlen(suffix);
  while (!found && (ent = readdir(d))) {
    size_t nl = strlen(ent->d_name);
    if (nl >= sl && strcmp(ent->d_name + nl - sl, suffix) == 0) {
      snprintf(out, len, "%s/%s", dir, ent->d_name);
      found = true;
    }
  }
  closedir(d);
  return found;
}

// ---------------------------------------------------------------------------
// The render
// ---------------------------------------------------------------------------

static uint64_t render_next_tick_ns;

// Moves the clock to `t`, ticking as the tick thread would have on the way.
static void render_advance(uint64_t t) {
  while (render_next_tick_ns <= t) {
    clock_simulated_ns = render_next_tick_ns;
    LOCK();
    jml_tick();
    whistle_breath_tick();
    UNLOCK();
    render_next_tick_ns += TICK_MS * 1000000ULL;
  }
  clock_simulated_ns = t;
}

static void render_usage(void) {
  printf("usage: jammer-mac --render RECORDING_FOLDER [--midi FILE.mid] "
         "[--out FOLDER]\n");
}

static int render_main(int argc, const char** argv, const char* soundfont) {
  const char* dir = argc > 2 ? argv[2] : NULL;
  const char* midi_arg = NULL;
  const char* out_dir = NULL;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (strcmp(argv[i], "--midi") == 0) midi_arg = argv[i + 1];
    else if (strcmp(argv[i], "--out") == 0) out_dir = argv[i + 1];
    else { render_usage(); return 2; }
  }
  if (!dir || (argc > 3 && (argc - 3) % 2)) { render_usage(); return 2; }
  if (!out_dir) out_dir = dir;

  char jack1[1024], jack2[1024], midi[1024];
  if (!render_find(dir, "In 1 (jack).wav", jack1, sizeof jack1) ||
      !render_find(dir, "In 2 (jack).wav", jack2, sizeof jack2)) {
    printf("%s has no \"In 1 (jack).wav\" and \"In 2 (jack).wav\": was it "
           "recorded in VSL Mixer with Jammer linked?\n", dir);
    return 1;
  }
  snprintf(midi, sizeof midi, "%s", midi_arg ? midi_arg : "");
  if (!midi_arg) snprintf(midi, sizeof midi, "%s/Jammer MIDI.mid", dir);

  double rate = 0, rate2 = 0;
  SInt64 frames = 0, frames2 = 0;
  ExtAudioFileRef in1 = render_open_wav(jack1, &rate, &frames);
  ExtAudioFileRef in2 = render_open_wav(jack2, &rate2, &frames2);
  if (!in1 || !in2 || rate != rate2 || rate <= 0) {
    printf("can't read the jack recordings as mono audio at one rate\n");
    return 1;
  }

  printf("reading %s\n", midi);
  if (!render_read_midi(midi)) return 1;
  render_classify_tracks();
  render_mark_history();
  qsort(render_events, (size_t)render_n_events, sizeof *render_events,
        render_event_cmp);

  char out1[1024], out2[1024];
  snprintf(out1, sizeof out1, "%s/Jammer L (render).wav", out_dir);
  snprintf(out2, sizeof out2, "%s/Jammer R (render).wav", out_dir);
  ExtAudioFileRef w1 = render_create_wav(out1, rate);
  ExtAudioFileRef w2 = render_create_wav(out2, rate);
  if (!w1 || !w2) {
    printf("can't write to %s\n", out_dir);
    return 1;
  }

  // The live rig, minus devices.
  synth_sample_rate = rate;
  create_synth(soundfont);
  install_audio_hooks();
  if (!whistle_input_start_offline(rate)) return 1;
  srand(1);   // the same render every time, for comparing edits
  clock_simulated = true;
  // Well away from zero, which some of the timekeeping takes for "never".
  const uint64_t start_ns = 1000ULL * 1000000000ULL;
  clock_simulated_ns = start_ns - (RENDER_PREROLL_S + 1) * 1000000000ULL;
  render_next_tick_ns = clock_simulated_ns;
  LOCK();
  jml_setup();
  UNLOCK();

  // What came before, in the second before the preroll: a millisecond apart,
  // or closer if there's more than a second's worth.
  int e = 0, history = 0;
  while (history < render_n_events && render_events[history].history) history++;
  uint64_t step = history > 1000 ? 1000000000ULL / (uint64_t)history : 1000000;
  for (; e < history; e++) {
    render_advance(clock_simulated_ns + step);
    render_dispatch(&render_events[e]);
  }
  printf("replayed %d logged actions from before the recording\n", history);

  float* l = calloc(RENDER_CHUNK, sizeof(float));
  float* r = calloc(RENDER_CHUNK, sizeof(float));
  float* a = calloc(RENDER_CHUNK, sizeof(float));
  float* b = calloc(RENDER_CHUNK, sizeof(float));
  float zeros[RENDER_BLOCK] = {0};
  if (!l || !r || !a || !b) die("out of memory");

  // Two seconds with nothing coming in, so whatever was already sounding
  // is when the recording starts; not written.
  for (long f = -(long)(RENDER_PREROLL_S * rate); f < 0; f += RENDER_BLOCK) {
    int n = (int)(-f < RENDER_BLOCK ? -f : RENDER_BLOCK);
    render_advance(start_ns + (uint64_t)0 - (uint64_t)((-f) * 1e9 / rate));
    float* out[2] = {l, r};
    whistle_push_input(zeros, zeros, n);
    jammer_audio_render(fl_synth, n, 0, NULL, 2, out);
  }

  int dispatched = 0, last_pct = -1;
  for (SInt64 done = 0; done < frames; ) {
    UInt32 chunk = (UInt32)(frames - done < RENDER_CHUNK ? frames - done
                                                          : RENDER_CHUNK);
    render_read(in1, a, chunk);
    render_read(in2, b, chunk);
    for (UInt32 i = 0; i < chunk; i += RENDER_BLOCK) {
      int n = (int)(chunk - i < RENDER_BLOCK ? chunk - i : RENDER_BLOCK);
      double t = (double)(done + i) / rate;
      // Everything that had arrived by the start of this block.
      while (e < render_n_events && render_events[e].at <= t) {
        render_advance(start_ns + (uint64_t)(render_events[e].at * 1e9));
        render_dispatch(&render_events[e++]);
        dispatched++;
      }
      render_advance(start_ns + (uint64_t)(t * 1e9));
      float* out[2] = {l + i, r + i};
      whistle_push_input(a + i, b + i, n);
      jammer_audio_render(fl_synth, n, 0, NULL, 2, out);
    }
    render_write(w1, l, chunk);
    render_write(w2, r, chunk);
    done += chunk;
    int pct = (int)(100 * done / frames);
    if (pct / 10 != last_pct / 10) {
      printf("  %3d%%\n", pct);
      fflush(stdout);
      last_pct = pct;
    }
  }
  ExtAudioFileDispose(w1);
  ExtAudioFileDispose(w2);
  ExtAudioFileDispose(in1);
  ExtAudioFileDispose(in2);
  printf("played %d messages over %.1fs\n", dispatched, frames / rate);
  if (render_unknown_keys) {
    printf("%d strikes were of keys this jammer doesn't have\n",
           render_unknown_keys);
  }
  printf("wrote %s\n      %s\n", out1, out2);
  return 0;
}

#endif
