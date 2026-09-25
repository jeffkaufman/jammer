// pads.c -- listen to sustained sounds as the drone bass (Db) and drone
// chord (Dc) would play them, and write down the ones worth keeping.
//
// Both drones are Rock Organ out of the box.  This walks a list of pads,
// strings, choirs and organs, and plays each one the way the jammer's drones
// do -- same notes, same velocities, same channel volumes -- over a slow
// I-IV-V-I in the jammer's key, so you hear it hold under a groove and
// re-strike when the bass moves, which is when a slow attack or a long release
// shows up.
//
// What the drones play (see update_bass, send_chord, psend_midi):
//
//   Db   the bass root, MIDI 24-35, at DRONE_BASS_VELOCITY
//   Dc   root and fifth two octaves up, 48-59 plus the fifth, at
//        DRONE_CHORD_VELOCITY
//
// and an organ (16 or 18) goes up another octave on top of that.  Each can
// be moved by octaves, as the jammer's octave keys would.
//
// The dB shown for each is what that voicing measures at the jammer's own
// channel volume for the program, next to how far that is from where the
// drones' pads are levelled to: Rock Organ as the drones used to play it,
// and a little more on the chord.  The ones on the
// drones' keys (DRONE_VOICES) should all be close to +0; anything else is
// at whatever voices.h gives it.
//
// Build: make pads
// Run:   ./pads [--all] [--list] [--devices] [--device NAME]
//        ./pads --levels   the volumes DRONE_VOICES should have
//        ./pads --check    fails if a pad on the drones has drifted off level
//
// --all walks every melodic program rather than the hand-picked list.

#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <CoreFoundation/CoreFoundation.h>

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "loudness.h"

#define PAD_BANK 0
#define MAX_CANDIDATES 128

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Things that sustain and might sit under the band.  General MIDI numbering,
// from 0, as everywhere else in the jammer.
static const int PAD_PROGRAMS[] = {
  16, 17, 18, 19, 20,      // organs: drawbar, percussive, rock, church, reed
  48, 49, 50, 51,          // string ensembles 1 and 2, synth strings 1 and 2
  52, 53, 54,              // choir aahs, voice oohs, synth voice
  61, 62, 63,              // brass section, synth brass 1 and 2
  88, 89, 90, 91, 92, 93, 94, 95,  // the pads proper
  96, 99, 100, 101, 102,   // FX: rain, atmosphere, brightness, goblins, echoes
};

typedef struct {
  int program;
  char name[32];
  double db_dba;   // Db voicing, as the jammer would play it
  double dc_dba;   // Dc voicing
  bool kept;
  int kept_part;
  int kept_db_octave;
  int kept_dc_octave;
} Candidate;

static Candidate candidates[MAX_CANDIDATES];
static int n_candidates = 0;

static bool wanted_program(int program, bool all) {
  if (all) return true;
  for (int i = 0; i < COUNT(PAD_PROGRAMS); i++) {
    if (PAD_PROGRAMS[i] == program) return true;
  }
  return false;
}

static void collect_candidates(bool all) {
  fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(fl_synth, fl_sfont_id);
  if (!sfont) die("couldn't read the soundfont back");

  fluid_sfont_iteration_start(sfont);
  fluid_preset_t* preset;
  while ((preset = fluid_sfont_iteration_next(sfont)) != NULL) {
    if (fluid_preset_get_banknum(preset) != PAD_BANK) continue;
    int program = fluid_preset_get_num(preset);
    if (!wanted_program(program, all)) continue;
    if (n_candidates >= MAX_CANDIDATES) break;
    Candidate* candidate = &candidates[n_candidates++];
    memset(candidate, 0, sizeof(*candidate));
    candidate->program = program;
    snprintf(candidate->name, sizeof(candidate->name), "%s",
             fluid_preset_get_name(preset));
  }

  for (int i = 1; i < n_candidates; i++) {  // insertion sort, by program
    Candidate held = candidates[i];
    int j = i - 1;
    while (j >= 0 && candidates[j].program > held.program) {
      candidates[j + 1] = candidates[j];
      j--;
    }
    candidates[j + 1] = held;
  }
}

// ---------------------------------------------------------------------------
// The drones' voicing
// ---------------------------------------------------------------------------

#define PART_BOTH 0
#define PART_DB 1
#define PART_DC 2
static const char* PART_NAMES[] = {"Db+Dc", "Db", "Dc"};


// The same arithmetic psend_midi does for a drone endpoint: organs up an
// octave, chords two more, then the endpoint's own octave shift.
static int drone_note(int program, int note, bool chord, int octave) {
  if (program == 16 || program == 18) note += 12;
  if (chord) note += 24;
  return note + octave * 12;
}

// What send_chord puts under a root: root and fifth, and the third only when
// asked for, the way CLIPPED adds it when the drum is choosing the notes.
static int chord_notes(int root, bool minor, bool third, int* out) {
  int n = 0;
  out[n++] = to_root(root);
  if (third) out[n++] = to_root(root + (minor ? 3 : 4));
  out[n++] = to_root(root + 7);
  return n;
}

// select_endpoint_voice, minus choose_voice's line of chatter, which would
// scroll the candidate off the screen every time you moved.
static void quiet_select_voice(int endpoint, int program) {
  fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  FILE* null = fopen("/dev/null", "w");
  if (null) dup2(fileno(null), STDOUT_FILENO);
  select_endpoint_voice(endpoint, program, PAD_BANK, 0, -1, false);
  fflush(stdout);
  dup2(saved, STDOUT_FILENO);
  close(saved);
  if (null) fclose(null);
}

// The channel volume voices.h gives this program on this endpoint.  Asked of
// voices.h rather than copied out of it, so it stays right when the table
// changes.
static int jammer_cc7(int endpoint, int program) {
  quiet_select_voice(endpoint, program);
  int value = 0;
  fluid_synth_get_cc(fl_synth, endpoint, CC_07, &value);
  return value;
}

// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------

#define RATE 44100
// A pad can take a couple of seconds to swell, so measure long enough to hear
// it get there.
#define MEASURE_FRAMES (RATE * 4)

static const char* measure_soundfont = NULL;

// The loudest perceived moment of these notes, on a synth made for the
// purpose and thrown away after.  A fresh one every time because a reused
// one doesn't give the same answer twice: a pad's release and its LFOs carry
// over through a reset, and moved a quiet note by as much as 20dB depending
// on what was measured before it.  It costs about 70ms.
static double measure(int program, int cc7, const int* notes, int n_notes,
                      int velocity) {
  static float left[MEASURE_FRAMES], right[MEASURE_FRAMES];
  static float mono[MEASURE_FRAMES];

  fluid_settings_t* settings = new_fluid_settings();
  fluid_settings_setnum(settings, "synth.sample-rate", RATE);
  fluid_settings_setnum(settings, "synth.gain", 1.0);
  fluid_settings_setint(settings, "synth.reverb.active", 0);
  fluid_settings_setint(settings, "synth.chorus.active", 0);
  fluid_synth_t* synth = new_fluid_synth(settings);
  if (!synth) die("couldn't create the offline synth");
  int sfont = fluid_synth_sfload(synth, measure_soundfont, 1);
  if (sfont == FLUID_FAILED) die("offline synth couldn't load the soundfont");

  fluid_synth_program_select(synth, 0, sfont, PAD_BANK, program);
  fluid_synth_cc(synth, 0, CC_07, cc7);
  fluid_synth_cc(synth, 0, CC_11, MIDI_MAX);
  for (int i = 0; i < n_notes; i++) {
    fluid_synth_noteon(synth, 0, notes[i], velocity);
  }
  fluid_synth_write_float(synth, MEASURE_FRAMES, left, 0, 1, right, 0, 1);
  for (int i = 0; i < MEASURE_FRAMES; i++) {
    mono[i] = 0.5f * (left[i] + right[i]);
  }

  delete_fluid_synth(synth);
  delete_fluid_settings(settings);
  return perceived_loudness(mono, MEASURE_FRAMES, RATE);
}

// A drone's voicing at the jammer's default root, D, and no octave shift,
// at channel volume cc7 and this velocity.
static double measure_drone_at(int program, bool chord, int cc7,
                               int velocity) {
  int root = to_root(26);
  if (!chord) {
    int bass = drone_note(program, root, false, 0);
    return measure(program, cc7, &bass, 1, velocity);
  }
  int notes[3];
  int n = chord_notes(root, false, false, notes);
  for (int i = 0; i < n; i++) notes[i] = drone_note(program, notes[i], true, 0);
  return measure(program, cc7, notes, n, velocity);
}

// As the jammer plays it now.
static double measure_drone(int program, bool chord, int cc7) {
  return measure_drone_at(program, chord, cc7,
                          chord ? DRONE_CHORD_VELOCITY : DRONE_BASS_VELOCITY);
}

// What the drones are levelled to: Rock Organ as the drones always played
// it, before they had volumes of their own -- channel volume 65, which was
// its entry in voices.h, and velocities of 70 for the bass and 30 for the
// chord -- on each drone's own voicing.  By ear the bass drone
// was about right there and the chord drone a little quiet, so the chord
// drones aim that much higher.
#define OLD_ROCK_ORGAN 18
#define OLD_ROCK_ORGAN_VOLUME 65
#define OLD_BASS_VELOCITY 70
#define OLD_CHORD_VELOCITY 30
#define CHORD_OVER_OLD_ORGAN_DB 2.0

// Generous enough not to trip on a fluidsynth that rounds differently, tight
// enough that a pad nobody levelled stands out.  The same as kitlevels.
#define LEVEL_TOLERANCE_DB 1.5

static double bass_target_dba, chord_target_dba;

static void measure_target(void) {
  bass_target_dba = measure_drone_at(OLD_ROCK_ORGAN, false,
                                     OLD_ROCK_ORGAN_VOLUME, OLD_BASS_VELOCITY);
  chord_target_dba = measure_drone_at(OLD_ROCK_ORGAN, true,
                                      OLD_ROCK_ORGAN_VOLUME,
                                      OLD_CHORD_VELOCITY) +
    CHORD_OVER_OLD_ORGAN_DB;
}

static double target_for(bool chord) {
  return chord ? chord_target_dba : bass_target_dba;
}

// The channel volume that brings this voicing closest to its target.
// Loudness rises with volume, so this is a binary search, and searching
// rather than assuming fluidsynth's volume curve means it can't be wrong
// about it.
static int volume_for(int program, bool chord) {
  double target = target_for(chord);
  int lo = 1, hi = 127;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (measure_drone(program, chord, mid) < target) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // lo is the first at or over; the one under it may be closer.
  if (lo > 1 &&
      fabs(measure_drone(program, chord, lo - 1) - target) <
      fabs(measure_drone(program, chord, lo) - target)) {
    lo--;
  }
  return lo;
}

static void measure_candidates(void) {
  for (int i = 0; i < n_candidates; i++) {
    int program = candidates[i].program;
    candidates[i].db_dba = measure_drone(
      program, false, jammer_cc7(ENDPOINT_DRONE_BASS, program));
    candidates[i].dc_dba = measure_drone(
      program, true, jammer_cc7(ENDPOINT_DRONE_CHORD, program));
  }
}

// --levels: the volumes DRONE_VOICES should have, ready to paste in.
static void print_levels(void) {
  printf("pads aim for %.1f dB on the bass drones and %.1f on the chord "
         "drones\n\n", bass_target_dba, chord_target_dba);
  for (int i = 0; i < N_DRONE_VOICES; i++) {
    int program = DRONE_VOICES[i].program;
    int bass = volume_for(program, false);
    int chord = volume_for(program, true);
    printf("  {'%c', %d, \"%s\", %d, %d},  // %+.1f / %+.1f dB\n",
           DRONE_VOICES[i].note, program, DRONE_VOICES[i].label, bass, chord,
           measure_drone(program, false, bass) - bass_target_dba,
           measure_drone(program, true, chord) - chord_target_dba);
  }
}

// --check: whether DRONE_VOICES still puts every pad where it should be.
static int check_levels(void) {
  int failures = 0;
  for (int i = 0; i < N_DRONE_VOICES; i++) {
    for (int chord = 0; chord < 2; chord++) {
      int program = DRONE_VOICES[i].program;
      int volume = chord ? DRONE_VOICES[i].chord_volume
                         : DRONE_VOICES[i].bass_volume;
      double off = measure_drone(program, chord, volume) - target_for(chord);
      if (fabs(off) > LEVEL_TOLERANCE_DB) {
        printf("FAIL: program %d on the %s drones is %+.1f dB from its "
               "target\n", program, chord ? "chord" : "bass", off);
        failures++;
      }
    }
  }
  if (failures) {
    printf("\n%d pad level(s) off; run ./pads --levels for the volumes to "
           "use\n", failures);
    return 1;
  }
  printf("pad level check passed: %d pads within %.1f dB of the drones' old "
         "Rock Organ\n", N_DRONE_VOICES, LEVEL_TOLERANCE_DB);
  return 0;
}

// ---------------------------------------------------------------------------
// Where the sound comes out -- the same as audition.c
// ---------------------------------------------------------------------------

#define JAMMER_APP_ID CFSTR("com.jefftk.jammer")

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

// I-IV-V-I, two bars each.  The drones re-strike on every change, as they do
// when the foot moves the bass.
static const struct { int offset; bool minor; } PROGRESSION[] = {
  {0, false}, {5, false}, {7, false}, {0, false},
};
#define BARS_PER_CHORD 2
#define BEATS_PER_BAR 4

static int current = 0;
static int part = PART_BOTH;
static int key = 26;            // D, the jammer's default root
static int db_octave = 0;
static int dc_octave = 0;
static bool moving = true;      // follow the progression, or hold the I
static bool third = false;
static bool context = true;
static int bpm = 100;
static bool running = true;

// What's sounding on each drone, so it can be released.
static int db_sounding[1], n_db_sounding = 0;
static int dc_sounding[3], n_dc_sounding = 0;

static int chord_index = 0;

static void drones_off(void) {
  for (int i = 0; i < n_db_sounding; i++) {
    send_midi(MIDI_OFF, db_sounding[i], 0, ENDPOINT_DRONE_BASS);
  }
  for (int i = 0; i < n_dc_sounding; i++) {
    send_midi(MIDI_OFF, dc_sounding[i], 0, ENDPOINT_DRONE_CHORD);
  }
  n_db_sounding = n_dc_sounding = 0;
}

static void drones_on(void) {
  drones_off();
  int program = candidates[current].program;
  int step = moving ? chord_index : 0;
  int root = to_root(key + PROGRESSION[step].offset);

  if (part != PART_DC) {
    db_sounding[0] = drone_note(program, root, false, db_octave);
    n_db_sounding = 1;
    send_midi(MIDI_ON, db_sounding[0], DRONE_BASS_VELOCITY,
              ENDPOINT_DRONE_BASS);
  }
  if (part != PART_DB) {
    int notes[3];
    n_dc_sounding = chord_notes(root, PROGRESSION[step].minor, third, notes);
    for (int i = 0; i < n_dc_sounding; i++) {
      dc_sounding[i] = drone_note(program, notes[i], true, dc_octave);
      send_midi(MIDI_ON, dc_sounding[i], DRONE_CHORD_VELOCITY,
                ENDPOINT_DRONE_CHORD);
    }
  }
}

static void load_candidate(void) {
  drones_off();
  int program = candidates[current].program;
  quiet_select_voice(ENDPOINT_DRONE_BASS, program);
  quiet_select_voice(ENDPOINT_DRONE_CHORD, program);
  send_midi(MIDI_CC, CC_11, MIDI_MAX, ENDPOINT_DRONE_BASS);
  send_midi(MIDI_CC, CC_11, MIDI_MAX, ENDPOINT_DRONE_CHORD);
  drones_on();
}

static const char* key_name(int note) {
  static const char* STEPS[] = {"C", "C#", "D", "Eb", "E", "F",
                                "F#", "G", "Ab", "A", "Bb", "B"};
  return STEPS[note % 12];
}

static void show(void) {
  Candidate* candidate = &candidates[current];
  printf("\npads %d/%d  %-20s (%d:%-3d)  Db %5.1f dB (%+.1f)  "
         "Dc %5.1f dB (%+.1f)%s\n",
         current + 1, n_candidates, candidate->name, PAD_BANK,
         candidate->program,
         candidate->db_dba, candidate->db_dba - bass_target_dba,
         candidate->dc_dba, candidate->dc_dba - chord_target_dba,
         candidate->kept ? "  [KEPT]" : "");
  printf("  [space] next  [b] back  [k] keep  [d] playing %s  "
         "[1/!] Db oct %+d  [2/@] Dc oct %+d\n",
         PART_NAMES[part], db_octave, dc_octave);
  printf("  [r] %s  [t] third %s  [[/]] key %s  [c] beat %s  "
         "[-/+] %d bpm  [q] done\n",
         moving ? "I-IV-V-I" : "holding I", third ? "on " : "off",
         key_name(key), context ? "on " : "off", bpm);
  fflush(stdout);
}

static void step(int delta) {
  current = (current + delta + n_candidates) % n_candidates;
  load_candidate();
  show();
}

static int bump_octave(int octave, int delta) {
  octave += delta;
  if (octave > 2) octave = 2;
  if (octave < -2) octave = -2;
  return octave;
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

static void handle_key(char pressed) {
  switch (pressed) {
  case ' ': case 'n': step(1); break;
  case 'b': case 'p': step(-1); break;
  case 'k': {
    Candidate* candidate = &candidates[current];
    candidate->kept = !candidate->kept;
    // Which drone it was for, and where, is half of what was chosen.
    candidate->kept_part = part;
    candidate->kept_db_octave = db_octave;
    candidate->kept_dc_octave = dc_octave;
    show();
    break;
  }
  case 'd': part = (part + 1) % 3; drones_on(); show(); break;
  case '1': db_octave = bump_octave(db_octave, 1); drones_on(); show(); break;
  case '!': db_octave = bump_octave(db_octave, -1); drones_on(); show(); break;
  case '2': dc_octave = bump_octave(dc_octave, 1); drones_on(); show(); break;
  case '@': dc_octave = bump_octave(dc_octave, -1); drones_on(); show(); break;
  case 'r': moving = !moving; drones_on(); show(); break;
  case 't': third = !third; drones_on(); show(); break;
  case ']': key = to_root(key + 1); drones_on(); show(); break;
  case '[': key = to_root(key - 1); drones_on(); show(); break;
  case 'c': context = !context; show(); break;
  case '-': if (bpm > 50) bpm -= 5; show(); break;
  case '+': case '=': if (bpm < 200) bpm += 5; show(); break;
  case 'q': running = false; break;
  }
}

static void summarize(void) {
  printf("\n\nkept:\n");
  int n_kept = 0;
  for (int i = 0; i < n_candidates; i++) {
    Candidate* candidate = &candidates[i];
    if (!candidate->kept) continue;
    n_kept++;
    printf("  %d:%-3d %-20s for %-5s", PAD_BANK, candidate->program,
           candidate->name, PART_NAMES[candidate->kept_part]);
    if (candidate->kept_part != PART_DC) {
      printf("  Db oct %+d %5.1f dB (%+.1f)", candidate->kept_db_octave,
             candidate->db_dba, candidate->db_dba - bass_target_dba);
    }
    if (candidate->kept_part != PART_DB) {
      printf("  Dc oct %+d %5.1f dB (%+.1f)", candidate->kept_dc_octave,
             candidate->dc_dba, candidate->dc_dba - chord_target_dba);
    }
    printf("\n");
  }
  if (n_kept == 0) printf("  (nothing)\n");
  printf("\n(dB at octave 0 and the jammer's levels, relative to where the "
         "drones' pads are levelled to)\n");
}

int main(int argc, char** argv) {
  const char* wanted_device = NULL;
  bool all = false;
  bool list_only = false;
  bool levels_only = false;
  bool check_only = false;
  bool list_devices = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--all") == 0) {
      all = true;
    } else if (strcmp(argv[i], "--list") == 0) {
      list_only = true;
    } else if (strcmp(argv[i], "--levels") == 0) {
      levels_only = true;
    } else if (strcmp(argv[i], "--check") == 0) {
      check_only = true;
    } else if (strcmp(argv[i], "--devices") == 0) {
      list_devices = true;
    } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      wanted_device = argv[++i];
    } else {
      printf("usage: pads [--all] [--list] [--levels] [--check] [--devices] "
             "[--device NAME]\n");
      return 1;
    }
  }

  if (list_devices) {
    char names[32][256];
    int n = list_audio_devices(names, 32);
    for (int i = 0; i < n; i++) printf("%s\n", names[i]);
    return 0;
  }

  char soundfont[1024];
  if (!find_soundfont(".", soundfont, sizeof(soundfont))) {
    die("couldn't find FluidR3_GM.sf2");
  }

  // None of these want audio hardware, so don't ask for any.  They still
  // want a synth, since the jammer's channel volumes are read back off one.
  if (list_only || levels_only || check_only) {
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

  collect_candidates(all);
  if (n_candidates == 0) die("no programs to try");
  measure_soundfont = soundfont;
  measure_target();
  if (levels_only) {
    print_levels();
    return 0;
  }
  if (check_only) return check_levels();

  measure_candidates();
  printf("%d programs; pads aim for Db %.1f dB, Dc %.1f dB\n",
         n_candidates, bass_target_dba, chord_target_dba);

  if (list_only) {
    for (int i = 0; i < n_candidates; i++) {
      printf("  %d:%-3d %-20s  Db %5.1f dB (%+5.1f)  Dc %5.1f dB (%+5.1f)\n",
             PAD_BANK, candidates[i].program, candidates[i].name,
             candidates[i].db_dba, candidates[i].db_dba - bass_target_dba,
             candidates[i].dc_dba, candidates[i].dc_dba - chord_target_dba);
    }
    return 0;
  }

  // The beat underneath, out of the Standard set as the jammer's drum plays.
  choose_voice(CHANNEL_DRUM, PERCUSSION_BANK, 0);
  send_midi(MIDI_CC, CC_07, 100, ENDPOINT_DRUM);

  raw_terminal();
  load_candidate();
  show();

  int beat = 0;
  bool downbeat = true;
  uint64_t next_event = now();
  while (running) {
    uint64_t t = now();
    if (t >= next_event) {
      if (downbeat) {
        int bar_beat = beat % (BEATS_PER_BAR * BARS_PER_CHORD);
        if (bar_beat == 0 && beat > 0 && moving) {
          chord_index = (chord_index + 1) % COUNT(PROGRESSION);
          drones_on();
        }
        if (context) {
          send_midi(MIDI_ON, MIDI_DRUM_OUT_KICK_2, 80, ENDPOINT_DRUM);
        }
        beat++;
      } else if (context) {
        send_midi(MIDI_ON, MIDI_DRUM_OUT_CLOSED_HIHAT, 60, ENDPOINT_DRUM);
      }
      downbeat = !downbeat;
      uint64_t half = (60L * NS_PER_SEC / bpm) / 2;
      next_event += half;
      if (next_event < t) next_event = t + half;  // tempo jumped
      continue;
    }

    uint64_t wait_ns = next_event - t;
    struct timeval timeout = {wait_ns / NS_PER_SEC,
                              (wait_ns % NS_PER_SEC) / 1000};
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(STDIN_FILENO, &readers);
    if (select(STDIN_FILENO + 1, &readers, NULL, NULL, &timeout) > 0) {
      char pressed;
      if (read(STDIN_FILENO, &pressed, 1) == 1) handle_key(pressed);
    }
  }

  drones_off();
  restore_terminal();
  summarize();
  stop_synth();
  return 0;
}
