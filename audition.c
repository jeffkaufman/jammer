// audition.c -- listen to drum sounds one at a time, in a beat, and write
// down the ones worth keeping.
//
// The jammer plays percussion on channel 9 and never sends that channel a
// program change, so every kit so far comes out of the soundfont's "Standard"
// percussion set.  The soundfont has thirty-one of these sets, and the
// techno-leaning sounds -- the 808 kick, the electronic snares -- live in the
// ones we've never touched.
//
// This walks every (set, note) pair for one family of sounds, plays it on a
// loop so you hear it in a beat rather than in isolation, and prints the ones
// you keep as a table.  Levels are A-weighted (dBA, see aweight.h), so two
// sounds showing the same number sound about equally loud -- unlike peak
// level, which rates a kick and a hat that sound nothing alike as equals.  Sets whose sample for a note is byte-for-byte the
// same are collapsed into one entry, so the list is only the sounds that
// actually differ -- for kicks that's 62 pairs down to a handful.
//
// One caveat the tool makes audible: a MIDI channel has one program, so the
// kick and the hihat of a kit have to come from the same set.  Press 'c' and
// the context sounds you hear alongside the candidate are the ones you'd
// really get.
//
// Build: make audition
// Run:   ./audition [kicks|snares|hihats|synthkicks] [--list]
//
// Plays through whatever output the Mac app is set to; --devices lists
// what's available and --device NAME picks another.

#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <CoreFoundation/CoreFoundation.h>

#include "macapi.h"
#include "aweight.h"

// jammermidilib.h has this too, but it's the jammer's whole brain and we
// only want the synth.
#define NS_PER_SEC 1000000000LL

#define DRUM_BANK 128
#define MAX_PRESETS 64
#define MAX_CANDIDATES 512
#define MAX_ALIASES 8

// ---------------------------------------------------------------------------
// What's in the soundfont
// ---------------------------------------------------------------------------

static const char* perc_name(int note) {
  static const char* NAMES[] = {
    "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare",
    "Hand Clap", "Electric Snare", "Low Floor Tom", "Closed Hi-Hat",
    "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat",
    "Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1", "High Tom",
    "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell", "Tambourine",
    "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap",
    "Ride Cymbal 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga",
    "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale",
    "High Agogo", "Low Agogo", "Cabasa", "Maracas",
    "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro",
    "Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica",
    "Open Cuica", "Mute Triangle", "Open Triangle",
  };
  if (note < 35 || note > 81) return "?";
  return NAMES[note - 35];
}

// C0 is MIDI 12, the convention where middle C is 60.
static const char* note_name(int note) {
  static const char* STEPS[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};
  static char buf[8];
  snprintf(buf, sizeof(buf), "%s%d", STEPS[note % 12], note / 12 - 1);
  return buf;
}

typedef struct {
  int program;
  char name[32];
} Preset;

static Preset presets[MAX_PRESETS];
static int n_presets = 0;

// The presets the loaded soundfont has in the family's bank, in program
// order, skipping any the family doesn't ask for.
static void collect_presets(int bank, const int* wanted, int n_wanted) {
  fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(fl_synth, fl_sfont_id);
  if (!sfont) die("couldn't read the soundfont back");

  fluid_sfont_iteration_start(sfont);
  fluid_preset_t* preset;
  while ((preset = fluid_sfont_iteration_next(sfont)) != NULL) {
    if (fluid_preset_get_banknum(preset) != bank) continue;
    if (wanted) {
      bool asked_for = false;
      for (int i = 0; i < n_wanted; i++) {
        if (wanted[i] == fluid_preset_get_num(preset)) asked_for = true;
      }
      if (!asked_for) continue;
    }
    if (n_presets >= MAX_PRESETS) break;
    presets[n_presets].program = fluid_preset_get_num(preset);
    snprintf(presets[n_presets].name, sizeof(presets[n_presets].name), "%s",
             fluid_preset_get_name(preset));
    n_presets++;
  }

  for (int i = 1; i < n_presets; i++) {  // insertion sort, by program
    Preset held = presets[i];
    int j = i - 1;
    while (j >= 0 && presets[j].program > held.program) {
      presets[j + 1] = presets[j];
      j--;
    }
    presets[j + 1] = held;
  }
}

// ---------------------------------------------------------------------------
// Families of sounds to walk through
// ---------------------------------------------------------------------------

// 35 and 36 are the two kicks every set has; 41, 43 and 45 are the low toms,
// which in the 808 set are kicks in their own right.
static const int KICK_NOTES[]  = {35, 36, 41, 43, 45};
static const int SNARE_NOTES[] = {37, 38, 39, 40};
static const int HIHAT_NOTES[] = {42, 44, 46, 51, 53, 54, 56, 69, 70, 75, 76,
                                  80, 81};

// A percussion set's kick is one fixed sample, so it can't follow the tune.
// A melodic program played low can, and fluidsynth is happy to put one on the
// drum channel.  These are the melodic programs that behave like a kick --
// low, loud, and over quickly -- and the pitches worth hearing them at.
static const int SYNTH_KICK_NOTES[] = {12, 18, 24, 28, 31, 36};
static const int SYNTH_KICK_PROGRAMS[] = {
  38,   // Synth Bass 1 -- 15-65 Hz depending on the note, decays in ~0.1s
  39,   // Synth Bass 2 -- louder, even shorter
  62,   // Synth Brass 1
  80,   // Square Lead
  87,   // Bass & Lead -- the deepest of them, 20 Hz down at note 24
  116,  // Taiko Drum -- tracks pitch nicely, but a long 0.5s boom
  117,  // Melodic Tom
  118,  // Synth Drum
};

typedef struct {
  const char* name;
  int bank;              // 128 for the percussion sets, 0 for melodic
  const int* notes;
  int n_notes;
  const int* programs;   // NULL for every preset in the bank
  int n_programs;
  // A melodic program sustains, so its note has to be released; a percussion
  // sample doesn't care.  It also means the note is a pitch rather than a
  // choice of sound, and that the context sounds have to come from somewhere
  // else -- note 42 on Synth Bass 1 is not a hihat.
  bool pitched;
  // Where in the beat the candidate lands, and what plays alongside it with
  // context switched on.  The jammer puts the kick and its snare on the
  // downbeat and the hihat halfway through.
  bool on_downbeat;
  int context_downbeat;  // 0 for none
  int context_upbeat;
} Family;

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Family FAMILIES[] = {
  {"kicks", 128, KICK_NOTES, COUNT(KICK_NOTES), NULL, 0,
   false, true, 0, 42},
  {"snares", 128, SNARE_NOTES, COUNT(SNARE_NOTES), NULL, 0,
   false, true, 36, 42},
  {"hihats", 128, HIHAT_NOTES, COUNT(HIHAT_NOTES), NULL, 0,
   false, false, 36, 0},
  {"synthkicks", 0, SYNTH_KICK_NOTES, COUNT(SYNTH_KICK_NOTES),
   SYNTH_KICK_PROGRAMS, COUNT(SYNTH_KICK_PROGRAMS),
   true, true, 0, 42},
};
#define N_FAMILIES COUNT(FAMILIES)

// Pitched candidates can't take their hihat from their own program, so the
// context for those plays on a second channel out of the 808 set.  That the
// synth has to do this at all is the point: a kit that mixes a pitched kick
// with a sampled hat needs two channels, not one.
#define CONTEXT_CHANNEL 10
#define CONTEXT_PROGRAM 25  // TR-808

// ---------------------------------------------------------------------------
// Candidates, deduplicated by what they actually sound like
// ---------------------------------------------------------------------------

typedef struct {
  int program;
  int note;
} Sound;

typedef struct {
  int program;
  int note;
  float peak;             // 0..1; what clips, rather than what sounds loud
  double loudness;        // dBA -- how loud it actually sounds
  Sound aliases[MAX_ALIASES];  // other pairs that render identically
  int n_aliases;
  bool kept;
  int kept_gate;          // the gate it was kept at, for a pitched family
} Candidate;

static Candidate candidates[MAX_CANDIDATES];
static int n_candidates = 0;

// A second synth with no audio driver, used to render a hit offline so we can
// tell which (program, note) pairs are the same sample.
static fluid_settings_t* off_settings = NULL;
static fluid_synth_t* off_synth = NULL;

static void start_offline_synth(const char* soundfont_path) {
  off_settings = new_fluid_settings();
  fluid_settings_setnum(off_settings, "synth.gain", 1.0);
  fluid_settings_setint(off_settings, "synth.reverb.active", 0);
  fluid_settings_setint(off_settings, "synth.chorus.active", 0);
  off_synth = new_fluid_synth(off_settings);
  if (!off_synth) die("couldn't create the offline synth");
  if (fluid_synth_sfload(off_synth, soundfont_path, 1) == FLUID_FAILED) {
    die("the offline synth couldn't load the soundfont");
  }
}

static void stop_offline_synth(void) {
  if (off_synth) delete_fluid_synth(off_synth);
  if (off_settings) delete_fluid_settings(off_settings);
}

// About a second at the default rate, but a whole number of fluidsynth's
// 64-frame blocks.  That matters: a note-on takes effect at the next block
// boundary, so if a render left the synth mid-block the next hit would come
// out shifted by a few samples and two copies of one sound would hash
// differently.
#define RENDER_FRAMES (689 * 64)

// Renders one hit and returns a hash of the samples, with the peak and the
// A-weighted loudness out of band.  Both, because they answer different
// questions: peak is what clips, loudness is what you hear, and for drums
// they disagree by tens of dB between a kick and a hat.  Silence hashes to 0.
//
// Float rather than the int16 render, because write_s16() dithers: two
// renders of one sound come out a bit apart in the bottom bit and hash
// differently.  Quantizing to int16 scale on the way into the hash drops
// that, along with the handful of samples at the very start of the attack
// where fluidsynth isn't bit-identical between renders.
static uint64_t render_hash(int bank, int program, int note,
                            float* peak_out, double* loudness_out) {
  static float left[RENDER_FRAMES], right[RENDER_FRAMES];

  fluid_synth_system_reset(off_synth);
  if (fluid_synth_program_select(off_synth, CHANNEL_DRUM, fl_sfont_id,
                                 bank, program) == FLUID_FAILED) {
    *peak_out = 0;
    *loudness_out = -INFINITY;
    return 0;
  }
  fluid_synth_noteon(off_synth, CHANNEL_DRUM, note, 100);
  fluid_synth_write_float(off_synth, RENDER_FRAMES, left, 0, 1, right, 0, 1);

  uint64_t hash = 1469598103934665603ULL;  // FNV-1a
  float peak = 0;
  for (int i = 0; i < RENDER_FRAMES; i++) {
    int quantized = (int)lrintf(left[i] * 32768.0f);
    hash = (hash ^ (unsigned)(quantized & 0xffff)) * 1099511628211ULL;
    float magnitude = left[i] < 0 ? -left[i] : left[i];
    if (magnitude > peak) peak = magnitude;
  }
  *peak_out = peak;
  *loudness_out = aweight_loudness(left, RENDER_FRAMES, 44100);
  return peak == 0 ? 0 : hash;
}

// Every (program, note) pair in the family that makes a distinct sound.  A
// pair whose samples match one we already have is filed as an alias of it, so
// walking the list is walking real choices and nothing else: FluidR3's eight
// Standard variants share most of their drums, several sets put the same
// sample on both kick notes, and silent pairs drop out entirely.
//
// Matching runs across the whole family rather than note by note, so a set
// whose two kicks are one sample shows up once.
static void collect_candidates(const Family* family) {
  uint64_t hashes[MAX_CANDIDATES];
  int hash_owner[MAX_CANDIDATES];  // index into candidates[]
  int n_hashes = 0;

  for (int n = 0; n < family->n_notes; n++) {
    int note = family->notes[n];

    for (int p = 0; p < n_presets; p++) {
      float peak;
      double loudness;
      uint64_t hash = render_hash(family->bank, presets[p].program, note,
                                  &peak, &loudness);
      if (hash == 0) continue;  // this set doesn't put anything on this note

      int seen = -1;
      for (int h = 0; h < n_hashes && !family->pitched; h++) {
        if (hashes[h] == hash) { seen = h; break; }
      }

      if (seen >= 0) {
        Candidate* owner = &candidates[hash_owner[seen]];
        if (owner->n_aliases < MAX_ALIASES) {
          owner->aliases[owner->n_aliases].program = presets[p].program;
          owner->aliases[owner->n_aliases].note = note;
          owner->n_aliases++;
        }
        continue;
      }

      if (n_candidates >= MAX_CANDIDATES) return;
      Candidate* candidate = &candidates[n_candidates];
      candidate->program = presets[p].program;
      candidate->note = note;
      candidate->peak = peak;
      candidate->loudness = loudness;
      candidate->n_aliases = 0;
      candidate->kept = false;
      hashes[n_hashes] = hash;
      hash_owner[n_hashes] = n_candidates;
      n_hashes++;
      n_candidates++;
    }
  }
}

static const char* program_name(int program) {
  for (int i = 0; i < n_presets; i++) {
    if (presets[i].program == program) return presets[i].name;
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Where the sound comes out
// ---------------------------------------------------------------------------

// The Mac app remembers its output in its own preferences, so read that same
// setting rather than having a second place to configure: auditioning comes
// out of whatever the jammer is already plugged into.  --device and
// $JAMMER_AUDIO_DEVICE override it, and both take any substring of a device
// name.
#define JAMMER_APP_ID CFSTR("net.jefftk.jammer")

static bool app_audio_device(char* buf, size_t len) {
  CFPropertyListRef value =
    CFPreferencesCopyAppValue(CFSTR("audioDevice"), JAMMER_APP_ID);
  if (!value) return false;

  bool ok = CFGetTypeID(value) == CFStringGetTypeID() &&
    CFStringGetCString((CFStringRef)value, buf, len, kCFStringEncodingUTF8);
  CFRelease(value);
  return ok;
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

static const Family* family;
static int current = 0;
static int bpm = 120;
static int velocity = 100;
static bool context = true;
static bool running = true;

// How long a pitched candidate is held before its note-off.  For a kick made
// out of a bass patch this is the main thing you're choosing: it's the
// difference between a thud and a note.
static const int GATES_MS[] = {60, 120, 200, 400};
static int gate = 1;

// The pitched note waiting to be released, if any.
static int sounding_note = -1;
static uint64_t note_off_at = 0;

static void release_sounding_note(void) {
  if (sounding_note < 0) return;
  send_midi(MIDI_OFF, sounding_note, 0, ENDPOINT_DRUM);
  sounding_note = -1;
}

// The jammer's beat: the kick lands on the downbeat and the hihat halfway
// through, so a cycle here is one of those halves.
static uint64_t half_beat_ns(void) {
  return (60L * NS_PER_SEC / bpm) / 2;
}

static void play_half(bool downbeat) {
  Candidate* candidate = &candidates[current];
  fluid_synth_program_select(fl_synth, CHANNEL_DRUM, fl_sfont_id,
                             family->bank, candidate->program);

  if (downbeat == family->on_downbeat) {
    release_sounding_note();  // in case the gate outlasts the beat
    send_midi(MIDI_ON, candidate->note, velocity, ENDPOINT_DRUM);
    if (family->pitched) {
      sounding_note = candidate->note;
      note_off_at = now() + GATES_MS[gate] * 1000000LL;
    }
  }
  if (!context) return;

  int other = downbeat ? family->context_downbeat : family->context_upbeat;
  if (!other) return;

  if (family->pitched) {
    fluid_synth_noteon(fl_synth, CONTEXT_CHANNEL, other, velocity * 0.8);
  } else {
    send_midi(MIDI_ON, other, velocity * 0.8, ENDPOINT_DRUM);
  }
}

static void show(void) {
  Candidate* candidate = &candidates[current];
  printf("\n%s %d/%d  %-20s (%d:%-3d)  note %d %-18s  %6.1f dBA%s\n",
         family->name, current + 1, n_candidates,
         program_name(candidate->program), family->bank, candidate->program,
         candidate->note,
         family->pitched ? note_name(candidate->note)
                         : perc_name(candidate->note),
         candidate->loudness,
         candidate->kept ? "  [KEPT]" : "");
  if (candidate->kept && family->pitched &&
      candidate->kept_gate != GATES_MS[gate]) {
    printf("  kept at a %dms gate; [k] again to keep it at %dms\n",
           candidate->kept_gate, GATES_MS[gate]);
  }
  if (candidate->n_aliases > 0) {
    printf("  same sample as:");
    for (int i = 0; i < candidate->n_aliases; i++) {
      printf(" %s/%d", program_name(candidate->aliases[i].program),
             candidate->aliases[i].note);
    }
    printf("\n");
  }
  printf("  [space] next  [b] back  [[/]] note group  [k] keep  [c] context %s"
         "  [-/+] %d bpm  [,/.] vel %d",
         context ? "on " : "off", bpm, velocity);
  if (family->pitched) printf("  [g] gate %dms", GATES_MS[gate]);
  printf("  [q] done\n");
  fflush(stdout);
}

static void step(int delta) {
  release_sounding_note();
  current = (current + delta + n_candidates) % n_candidates;
  show();
}

// Jumps to the first candidate of the next or previous note.
static void step_note(int delta) {
  int note = candidates[current].note;
  int target = current;
  for (int i = 1; i <= n_candidates; i++) {
    int at = ((current + i * delta) % n_candidates + n_candidates) %
             n_candidates;
    if (candidates[at].note != note) { target = at; break; }
  }
  if (delta < 0) {  // land on the first of that note, not the last
    int note_back = candidates[target].note;
    while (target > 0 && candidates[target - 1].note == note_back) target--;
  }
  release_sounding_note();
  current = target;
  show();
}

// ---------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------

static struct termios saved_termios;
static bool termios_saved = false;

static void restore_terminal(void) {
  if (termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

static void on_signal(int sig) {
  (void)sig;
  restore_terminal();
  _exit(1);
}

static void raw_terminal(void) {
  if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
    termios_saved = true;
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  }
}

static void handle_key(char key) {
  switch (key) {
  case ' ': case 'n': step(1); break;
  case 'b': case 'p': step(-1); break;
  case ']': step_note(1); break;
  case '[': step_note(-1); break;
  case 'k':
    candidates[current].kept = !candidates[current].kept;
    // For a pitched sound the gate is half the choice, so remember the one
    // it was kept at rather than whatever the gate happens to be at the end.
    candidates[current].kept_gate = GATES_MS[gate];
    show();
    break;
  case 'c': context = !context; show(); break;
  case 'g':
    gate = (gate + 1) % COUNT(GATES_MS);
    show();
    break;
  case '-': if (bpm > 50) bpm -= 5; show(); break;
  case '+': case '=': if (bpm < 200) bpm += 5; show(); break;
  case ',': if (velocity > 10) velocity -= 10; show(); break;
  case '.': if (velocity < 127) velocity += 10; show(); break;
  case 'q': running = false; break;
  }
}

static void summarize(void) {
  printf("\n\nkept:\n");
  int n_kept = 0;
  for (int i = 0; i < n_candidates; i++) {
    if (!candidates[i].kept) continue;
    n_kept++;
    printf("  %d:%-3d %-20s note %2d %-18s %6.1f dBA",
           family->bank, candidates[i].program,
           program_name(candidates[i].program), candidates[i].note,
           family->pitched ? note_name(candidates[i].note)
                           : perc_name(candidates[i].note),
           candidates[i].loudness);
    if (family->pitched) printf("  gate %dms", candidates[i].kept_gate);
    printf("\n");
  }
  if (n_kept == 0) printf("  (nothing)\n");
}

int main(int argc, char** argv) {
  const char* wanted = "kicks";
  const char* wanted_device = NULL;
  bool list_only = false;
  bool list_devices = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--list") == 0) {
      list_only = true;
    } else if (strcmp(argv[i], "--devices") == 0) {
      list_devices = true;
    } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      wanted_device = argv[++i];
    } else {
      wanted = argv[i];
    }
  }

  if (list_devices) {
    char names[32][256];
    int n = list_audio_devices(names, 32);
    for (int i = 0; i < n; i++) printf("%s\n", names[i]);
    return 0;
  }

  family = NULL;
  for (int i = 0; i < N_FAMILIES; i++) {
    if (strcmp(FAMILIES[i].name, wanted) == 0) family = &FAMILIES[i];
  }
  if (!family) {
    printf("usage: audition [kicks|snares|hihats|synthkicks] [--list] "
           "[--devices] [--device NAME]\n");
    return 1;
  }

  // "." so the copy in the source directory counts, since that's where this
  // gets run from.
  char soundfont[1024];
  if (!find_soundfont(".", soundfont, sizeof(soundfont))) {
    die("couldn't find FluidR3_GM.sf2");
  }

  // --list wants no audio hardware, so don't ask for any.
  if (list_only) {
    fl_settings = new_fluid_settings();
    fl_synth = new_fluid_synth(fl_settings);
    fl_sfont_id = fluid_synth_sfload(fl_synth, soundfont, 1);
    if (fl_sfont_id == FLUID_FAILED) die("couldn't load soundfont");
  } else {
    char from_app[256];
    const char* device = wanted_device;
    if (!device) device = getenv("JAMMER_AUDIO_DEVICE");
    if (!device && app_audio_device(from_app, sizeof(from_app))) {
      device = from_app;
      printf("using the jammer's own output device\n");
    }
    start_synth(soundfont, device ? device : "default");
  }

  collect_presets(family->bank, family->programs, family->n_programs);
  printf("%d %s in the soundfont\n", n_presets,
         family->bank == DRUM_BANK ? "percussion sets" : "programs to try");

  start_offline_synth(soundfont);

  // Collapsing identical sounds is for the percussion sets, where 31 of them
  // share most of their samples.  Melodic programs are a short hand-picked
  // list with nothing to collapse, and several have an LFO whose phase
  // follows the synth's running clock, so two renders of one note aren't
  // bit-identical and matching them would be meaningless anyway.
  //
  // Where it does run, it's only honest if rendering is reproducible.  Check
  // rather than assume, so an unstable fluidsynth shows up as a warning
  // instead of as a list that just happens to have no duplicates in it.
  if (!family->pitched) {
    float ignored;
    double ignored_loudness;
    if (render_hash(family->bank, presets[0].program, family->notes[0],
                    &ignored, &ignored_loudness) !=
        render_hash(family->bank, presets[0].program, family->notes[0],
                    &ignored, &ignored_loudness)) {
      printf("warning: rendering isn't reproducible here, so identical sounds "
             "won't be collapsed\n");
    }
  }

  collect_candidates(family);
  stop_offline_synth();
  printf("%s: %d distinct sounds\n", family->name, n_candidates);

  if (n_candidates == 0) return 1;

  if (list_only) {
    int note = -1;
    for (int i = 0; i < n_candidates; i++) {
      if (candidates[i].note != note) {
        note = candidates[i].note;
        printf("\nnote %d %s\n", note,
               family->pitched ? note_name(note) : perc_name(note));
      }
      printf("  %2d. %d:%-3d %-20s %6.1f dBA", i + 1, family->bank,
             candidates[i].program, program_name(candidates[i].program),
             candidates[i].loudness);
      if (candidates[i].n_aliases > 0) {
        printf("  (= ");
        for (int a = 0; a < candidates[i].n_aliases; a++) {
          printf("%s%s/%d", a ? ", " : "",
                 program_name(candidates[i].aliases[a].program),
                 candidates[i].aliases[a].note);
        }
        printf(")");
      }
      printf("\n");
    }
    return 0;
  }

  if (family->pitched) {
    fluid_synth_program_select(fl_synth, CONTEXT_CHANNEL, fl_sfont_id,
                               DRUM_BANK, CONTEXT_PROGRAM);
  }

  raw_terminal();
  show();

  bool downbeat = true;
  uint64_t next_event = now();
  while (running) {
    uint64_t t = now();
    if (sounding_note >= 0 && t >= note_off_at) release_sounding_note();
    if (t >= next_event) {
      play_half(downbeat);
      downbeat = !downbeat;
      next_event += half_beat_ns();
      if (next_event < t) next_event = t + half_beat_ns();  // tempo jumped
      continue;
    }

    // Wake for whichever comes first, the next hit or the note-off that ends
    // the one that's sounding.
    uint64_t wake_at = next_event;
    if (sounding_note >= 0 && note_off_at < wake_at) wake_at = note_off_at;
    uint64_t wait_ns = wake_at - t;
    struct timeval timeout = {wait_ns / NS_PER_SEC,
                              (wait_ns % NS_PER_SEC) / 1000};
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(STDIN_FILENO, &readers);
    if (select(STDIN_FILENO + 1, &readers, NULL, NULL, &timeout) > 0) {
      char key;
      if (read(STDIN_FILENO, &key, 1) == 1) handle_key(key);
    }
  }

  release_sounding_note();
  restore_terminal();
  summarize();
  stop_synth();
  return 0;
}
