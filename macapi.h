#ifndef JML_MAC_API_H
#define JML_MAC_API_H

// Mac equivalent of linuxapi.h.
//
// On the Pi we send MIDI out over ALSA sequencer ports to a separate
// fluidsynth process.  Here we link libfluidsynth directly and call it
// in-process, so there's one app to launch and no MIDI routing to get wrong.
// Same soundfont, so the same voice/volume tuning applies.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <fluidsynth.h>
#include <stdatomic.h>

// Its own channel here, rather than the drum channel's: see Kick Duck below.
// Mirrors the drum channel's program and controllers, so it sounds the same.
#define CHANNEL_KICK 14

#include "common.h"

int attempt(int result, char* errmsg) {
  if (result < 0) {
    perror("");
    die(errmsg);
  }
  return result;
}

uint64_t now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

// Which CoreAudio device to play through.  "default" follows the system
// output, which is usually the wrong thing for a live rig -- on the Pi
// run-fluidsynth.sh picks the USB interface explicitly, and this is the
// equivalent.
#define AUDIO_DEVICE_SETTING "audio.coreaudio.device"
char audio_device[256] = "default";

// The Pi passes fluidsynth -c 2 -z 64: two ALSA periods of 64 frames.  Neither
// translates to CoreAudio.  Setting audio.periods at all makes the driver open
// happily and then never ask us for a single sample -- no error, no sound --
// and it's device-dependent, so it can look fine on one output and be silent
// on the next.  Leave the buffering to fluidsynth unless told otherwise, and
// use audio_is_flowing() to catch it if an override misbehaves.

// Bumped by the render callback, so we can tell "device open" from "device
// actually playing".
static volatile uint64_t audio_frames_rendered = 0;

// Anything else that wants to be heard through jammer's output device, summed
// into fluidsynth's buffers after it has filled them.  This is how the whistle
// bass gets out without a second audio device -- see whistle.h.  NULL in the
// tools that include this header for the synth alone.
//
// Called on the audio thread, so whatever is behind it must not lock or
// allocate.
static void (*audio_mix_hook)(float** out, int nout, int len,
                              double sample_rate) = NULL;

// What the synth is running at, which is also what any mixed-in engine has to
// run at.  Set before start_synth to ask for something other than fluidsynth's
// own default.
double synth_sample_rate = 44100;

// How big a block fluidsynth's driver asks for.  Read by the whistle, which
// has to keep at least one of these buffered to hand it -- see
// whistle_input_start.  Set here because this is the only place that knows.
static volatile int audio_block_frames = 0;

// ---------------------------------------------------------------------------
// Kick Duck
//
// Everything fluidsynth plays but the kick, turned down on each kick and
// brought back up over the beat: the pump of a sidechained mix, set off by
// each kick as it's played rather than by a beat worked out ahead of time, so
// it follows the feet and a kick left out is a pump left out.  The kick pedal
// counts whether or not the rig's kick is on (jammermidilib.h's
// kick_duck_kick), since the pedals may be playing another synth's drums.  Done on the
// audio rather than with CC11, which the fades, the breath and Pulse already
// share, so it's smooth and lands on the sample.
//
// The rhythm parts that play with the kick aren't ducked either -- the foot
// basses and the arp -- so they hit with it rather than being pushed down by
// it.
//
// For that the synth renders each MIDI channel to a stereo pair of its own
// (start_synth), and the mix happens here: those channels as they are, and
// the rest ducked.  The whistle is summed in after, by audio_mix_hook, so
// it isn't ducked; it isn't fluidsynth's.
// ---------------------------------------------------------------------------

#define SYNTH_CHANNELS 16
#define KICK_DUCK_DEPTH 0.788f   // taken off at the bottom: about -13.5dB
#define KICK_DUCK_ATTACK_MS 5    // down this fast, so it doesn't click
#define KICK_DUCK_RELEASE 0.6    // back up over this much of a beat
#define KICK_DUCK_FRAMES 1024    // rendered this many at a time

// Set on each kick while Kick Duck is on, with the length of the beat it's
// in, which is how long the duck takes to come back up.
static _Atomic unsigned kick_duck_hits;
static _Atomic uint64_t kick_duck_beat_ns;

// Called by jammermidilib.h's kick_hook, under jammer's lock.
static void kick_duck_hit(uint64_t beat_ns) {
  atomic_store_explicit(&kick_duck_beat_ns, beat_ns, memory_order_relaxed);
  atomic_fetch_add_explicit(&kick_duck_hits, 1, memory_order_release);
}

// The audio thread's own.
static float synth_channel_bufs[2 * SYNTH_CHANNELS][KICK_DUCK_FRAMES];
static unsigned kick_duck_seen;
static double kick_duck_pos = -1;  // frames into the duck, or -1 if none
static double kick_duck_from;      // the gain its attack started from
static double kick_duck_attack, kick_duck_release;  // in frames
static double kick_duck_gain = 1;

// Endpoints are channels (common.h), so these are the foot basses and arp.
static bool kick_duck_exempt(int channel) {
  switch (channel) {
  case CHANNEL_KICK:
  case CHANNEL_PITCHED_KICK:
  case ENDPOINT_FOOTBASS:
  case ENDPOINT_FOOTBASS_2:
  case ENDPOINT_FOOTBASS_3:
  case ENDPOINT_ARP:
    return true;
  }
  return false;
}

// The duck's gain for the next frame.
static float kick_duck_step(void) {
  if (kick_duck_pos < 0) return 1;
  double bottom = 1 - KICK_DUCK_DEPTH;
  if (kick_duck_pos < kick_duck_attack) {
    kick_duck_gain = kick_duck_from +
      (bottom - kick_duck_from) * kick_duck_pos / kick_duck_attack;
  } else if (kick_duck_pos < kick_duck_attack + kick_duck_release) {
    // A raised cosine back up, so it leaves the bottom and arrives at the
    // top gently.
    double x = (kick_duck_pos - kick_duck_attack) / kick_duck_release;
    kick_duck_gain = 1 - KICK_DUCK_DEPTH * 0.5 * (1 + cos(M_PI * x));
  } else {
    kick_duck_gain = 1;
    kick_duck_pos = -1;
    return 1;
  }
  kick_duck_pos++;
  return (float)kick_duck_gain;
}

// fluid_synth_process, with every channel but the exempt ones ducked.  Up to
// KICK_DUCK_FRAMES at a time, since that's what the channel buffers hold.
static int render_kick_ducked(fluid_synth_t* synth, int len, int nfx,
                              float** fx, int nout, float** out,
                              double sample_rate) {
  unsigned hits = atomic_load_explicit(&kick_duck_hits, memory_order_acquire);
  if (hits != kick_duck_seen) {
    kick_duck_seen = hits;
    double beat_s = (double)atomic_load_explicit(
      &kick_duck_beat_ns, memory_order_relaxed) / 1e9;
    kick_duck_from = kick_duck_gain;
    kick_duck_attack = sample_rate * KICK_DUCK_ATTACK_MS / 1000;
    kick_duck_release = sample_rate * beat_s * KICK_DUCK_RELEASE;
    kick_duck_pos = 0;
  }

  float* bufs[2 * SYNTH_CHANNELS];
  float* chunk_fx[nfx > 0 ? nfx : 1];
  int result = FLUID_OK;
  for (int done = 0; done < len; done += KICK_DUCK_FRAMES) {
    int n = len - done < KICK_DUCK_FRAMES ? len - done : KICK_DUCK_FRAMES;
    for (int i = 0; i < 2 * SYNTH_CHANNELS; i++) {
      bufs[i] = synth_channel_bufs[i];
      memset(bufs[i], 0, (size_t)n * sizeof(float));
    }
    for (int i = 0; i < nfx; i++) chunk_fx[i] = fx[i] + done;
    result = fluid_synth_process(synth, n, nfx, chunk_fx,
                                 2 * SYNTH_CHANNELS, bufs);
    if (result != FLUID_OK || nout < 2) continue;

    float* left = out[0] + done;
    float* right = out[1] + done;
    for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
      if (kick_duck_exempt(ch)) continue;
      for (int i = 0; i < n; i++) {
        left[i] += bufs[2 * ch][i];
        right[i] += bufs[2 * ch + 1][i];
      }
    }
    if (kick_duck_pos >= 0) {
      for (int i = 0; i < n; i++) {
        float gain = kick_duck_step();
        left[i] *= gain;
        right[i] *= gain;
      }
    }
    for (int ch = 0; ch < SYNTH_CHANNELS; ch++) {
      if (!kick_duck_exempt(ch)) continue;
      for (int i = 0; i < n; i++) {
        left[i] += bufs[2 * ch][i];
        right[i] += bufs[2 * ch + 1][i];
      }
    }
  }
  return result;
}

static int jammer_audio_render(void* data, int len, int nfx, float** fx,
                               int nout, float** out) {
  audio_frames_rendered += len;
  if (len > audio_block_frames) audio_block_frames = len;
  // Cleared before the synth writes into them rather than trusted to arrive
  // clean: fluidsynth's docs don't promise either way, and a mix hook that
  // added into a buffer still holding its own last block would run away.
  for (int i = 0; i < nout; i++) {
    memset(out[i], 0, (size_t)len * sizeof(float));
  }
  for (int i = 0; i < nfx; i++) {
    memset(fx[i], 0, (size_t)len * sizeof(float));
  }
  int result = render_kick_ducked((fluid_synth_t*)data, len, nfx, fx, nout,
                                  out, synth_sample_rate);
  if (audio_mix_hook) {
    audio_mix_hook(out, nout, len, synth_sample_rate);
  }
  return result;
}

// True if the driver has pulled audio from us recently.  Opening a device
// tells you nothing; this tells you it's running.
bool audio_is_flowing(void) {
  uint64_t before = audio_frames_rendered;
  usleep(300000);
  return audio_frames_rendered > before;
}

// Global output volume, on top of the per-voice levels in voices.h.  1.0 is
// what the Pi's run-fluidsynth.sh uses; the Audio Output menu's slider moves
// it when a room or a PA wants more or less.
#define MAX_SYNTH_GAIN 2.0
double synth_gain = 1.0;

fluid_settings_t* fl_settings = NULL;
fluid_synth_t* fl_synth = NULL;
fluid_audio_driver_t* fl_driver = NULL;
int fl_sfont_id = -1;

// Where to look for FluidR3_GM.sf2, in priority order.  $JAMMER_SOUNDFONT
// wins, then anything bundled next to the binary, then the usual homebrew and
// Linux locations.
const char* SOUNDFONT_PATHS[] = {
  "FluidR3_GM.sf2",
  "soundfonts/FluidR3_GM.sf2",
  "/opt/homebrew/share/soundfonts/FluidR3_GM.sf2",
  "/usr/local/share/soundfonts/FluidR3_GM.sf2",
  "/usr/share/sounds/sf2/FluidR3_GM.sf2",
  NULL
};

// Fills buf with the first soundfont we can read, or returns false.  bundle_dir
// may be NULL; when set it's checked (with each relative path above) first.
bool find_soundfont(const char* bundle_dir, char* buf, size_t buf_len) {
  const char* from_env = getenv("JAMMER_SOUNDFONT");
  if (from_env && access(from_env, R_OK) == 0) {
    snprintf(buf, buf_len, "%s", from_env);
    return true;
  }

  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0 && !bundle_dir) continue;
    for (int i = 0; SOUNDFONT_PATHS[i]; i++) {
      if (pass == 0) {
        snprintf(buf, buf_len, "%s/%s", bundle_dir, SOUNDFONT_PATHS[i]);
      } else if (SOUNDFONT_PATHS[i][0] != '/') {
        continue;  // relative paths only make sense against bundle_dir
      } else {
        snprintf(buf, buf_len, "%s", SOUNDFONT_PATHS[i]);
      }
      if (access(buf, R_OK) == 0) {
        return true;
      }
    }
  }
  return false;
}

// Names of the CoreAudio output devices fluidsynth can see, plus "default".
// Returns how many were written.
struct DeviceCollect { char (*names)[256]; int n; int max; };

static void collect_device_name(void* data, const char* setting,
                                const char* option) {
  struct DeviceCollect* collect = (struct DeviceCollect*)data;
  if (collect->n < collect->max) {
    snprintf(collect->names[collect->n++], 256, "%s", option);
  }
}

int list_audio_devices(char names[][256], int max_names) {
  struct DeviceCollect collect = {names, 0, max_names};

  fluid_settings_t* settings = fl_settings;
  bool temporary = false;
  if (!settings) {
    settings = new_fluid_settings();
    fluid_settings_setstr(settings, "audio.driver", "coreaudio");
    temporary = true;
  }
  fluid_settings_foreach_option(settings, AUDIO_DEVICE_SETTING,
                               &collect, collect_device_name);
  if (temporary) delete_fluid_settings(settings);
  return collect.n;
}

// Accepts an exact device name or any substring of one, so "Scarlett" finds
// "Scarlett 2i2 USB".  Falls back to "default" if nothing matches.
void resolve_audio_device(const char* wanted, char* out, size_t out_len) {
  snprintf(out, out_len, "default");
  if (!wanted || !*wanted) return;

  char names[32][256];
  int n = list_audio_devices(names, 32);
  for (int i = 0; i < n; i++) {
    if (strcmp(names[i], wanted) == 0) {
      snprintf(out, out_len, "%s", names[i]);
      return;
    }
  }
  for (int i = 0; i < n; i++) {
    if (strstr(names[i], wanted) != NULL) {
      snprintf(out, out_len, "%s", names[i]);
      return;
    }
  }
  printf("no audio device matching \"%s\"; using the system default\n", wanted);
}

// Swap the output device without disturbing the synth, so notes that are
// sounding keep their state.  Returns false and falls back to the system
// default if the device won't open.
bool set_audio_device(const char* wanted) {
  char resolved[256];
  resolve_audio_device(wanted, resolved, sizeof(resolved));

  if (fl_driver) {
    delete_fluid_audio_driver(fl_driver);
    fl_driver = NULL;
  }
  fluid_settings_setstr(fl_settings, AUDIO_DEVICE_SETTING, resolved);
  fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                      fl_synth);

  // An open device that never pulls is the failure mode we care about, so
  // check before believing it, and back off to safe buffering if need be.
  if (fl_driver && !audio_is_flowing()) {
    printf("audio device \"%s\" opened but isn't pulling samples; "
           "retrying with fluidsynth's own buffering\n", resolved);
    delete_fluid_audio_driver(fl_driver);

    // Put the buffering back to fluidsynth's defaults -- actually changing
    // them, rather than setting the same values again.
    int default_periods = 0, default_period_size = 0;
    fluid_settings_getint_default(fl_settings, "audio.periods",
                                  &default_periods);
    fluid_settings_getint_default(fl_settings, "audio.period-size",
                                  &default_period_size);
    fluid_settings_setint(fl_settings, "audio.periods", default_periods);
    fluid_settings_setint(fl_settings, "audio.period-size",
                          default_period_size);

    fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                        fl_synth);
    if (fl_driver && !audio_is_flowing()) {
      printf("still no audio from \"%s\" -- try another output device\n",
             resolved);
    }
  }

  if (!fl_driver) {
    printf("couldn't open audio device \"%s\"; falling back to default\n",
           resolved);
    snprintf(resolved, sizeof(resolved), "default");
    fluid_settings_setstr(fl_settings, AUDIO_DEVICE_SETTING, resolved);
    fl_driver = new_fluid_audio_driver2(fl_settings, jammer_audio_render,
                                        fl_synth);
    if (!fl_driver) die("couldn't open any audio output");
    snprintf(audio_device, sizeof(audio_device), "%s", resolved);
    return false;
  }

  snprintf(audio_device, sizeof(audio_device), "%s", resolved);
  printf("audio output: %s (%llu frames rendered so far)\n", audio_device,
         (unsigned long long)audio_frames_rendered);
  return true;
}

void set_synth_gain(double gain) {
  if (gain < 0) gain = 0;
  if (gain > MAX_SYNTH_GAIN) gain = MAX_SYNTH_GAIN;
  synth_gain = gain;
  if (fl_synth) fluid_synth_set_gain(fl_synth, (float)gain);
}

// Mirrors run-fluidsynth.sh: -c 2 -z 64 -g 1.0, stereo, reverb/chorus off.
void start_synth(const char* soundfont_path, const char* device) {
  fl_settings = new_fluid_settings();
  if (!fl_settings) die("couldn't create fluidsynth settings");

  fluid_settings_setstr(fl_settings, "audio.driver", "coreaudio");
  // Only override the buffering if asked; see the note above.
  const char* periods_env = getenv("JAMMER_PERIODS");
  if (periods_env) {
    fluid_settings_setint(fl_settings, "audio.periods", atoi(periods_env));
  }
  const char* period_env = getenv("JAMMER_PERIOD_SIZE");
  if (period_env) {
    fluid_settings_setint(fl_settings, "audio.period-size", atoi(period_env));
  }
  fluid_settings_setnum(fl_settings, "synth.sample-rate", synth_sample_rate);
  fluid_settings_setnum(fl_settings, "synth.gain", 1.0);
  // A stereo pair for each MIDI channel, so Kick Duck can mix them itself.
  // Put back to one before the driver is made, below: the CoreAudio driver
  // reads the same setting as how many channels to open the device with.
  fluid_settings_setint(fl_settings, "synth.audio-channels", SYNTH_CHANNELS);
  fluid_settings_setint(fl_settings, "synth.audio-groups", SYNTH_CHANNELS);
  fluid_settings_setint(fl_settings, "synth.midi-channels", SYNTH_CHANNELS);
  fluid_settings_setint(fl_settings, "synth.reverb.active", 0);
  fluid_settings_setint(fl_settings, "synth.chorus.active", 0);
  // Channel 9 is percussion, as on the Pi (ENDPOINT_DRUM == CHANNEL_DRUM == 9).
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");

  fl_synth = new_fluid_synth(fl_settings);
  if (!fl_synth) die("couldn't create fluidsynth synth");
  fluid_settings_setint(fl_settings, "synth.audio-channels", 1);
  fluid_synth_set_channel_type(fl_synth, CHANNEL_KICK, CHANNEL_TYPE_DRUM);

  fl_sfont_id = fluid_synth_sfload(fl_synth, soundfont_path, 1);
  if (fl_sfont_id == FLUID_FAILED) {
    printf("failed to load soundfont %s\n", soundfont_path);
    die("couldn't load soundfont");
  }
  printf("loaded soundfont %s\n", soundfont_path);

  set_synth_gain(synth_gain);
  set_audio_device(device);
}

void stop_synth() {
  if (fl_driver) delete_fluid_audio_driver(fl_driver);
  if (fl_synth) delete_fluid_synth(fl_synth);
  if (fl_settings) delete_fluid_settings(fl_settings);
  fl_driver = NULL;
  fl_synth = NULL;
  fl_settings = NULL;
}

void send_midi(int action, int note, int velocity, int endpoint) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;

  if (!fl_synth) return;  // no synth yet, or we're shutting down

  int channel = endpoint;

  if (action == MIDI_CC) {
    fluid_synth_cc(fl_synth, channel, note, velocity);
    // The kick's channel is the drum's, split off: same volume, pan, fade.
    if (channel == CHANNEL_DRUM) {
      fluid_synth_cc(fl_synth, CHANNEL_KICK, note, velocity);
    }
  } else if (action == MIDI_ON) {
    fluid_synth_noteon(fl_synth, channel, note, velocity);
  } else if (action == MIDI_OFF) {
    // fluidsynth returns FLUID_FAILED for note-offs on notes that aren't
    // sounding, which happens constantly and normally.  Ignore it.
    fluid_synth_noteoff(fl_synth, channel, note);
  } else {
    printf("unknown action %d\n", action);
  }
}

void choose_voice(int channel, int bank, int voice) {
  if (bank < 0) bank = 0;
  // PERCUSSION_BANK, not 127: clamping to 127 turned every request for a
  // drum kit into a bank that doesn't exist, and the fallback below then put
  // a grand piano on the drum channel.
  if (bank > PERCUSSION_BANK) bank = PERCUSSION_BANK;
  if (voice < 0) voice = 0;
  if (voice > 127) voice = 127;

  if (!fl_synth) return;

  if (fluid_synth_program_select(fl_synth, channel, fl_sfont_id,
                                 bank, voice) == FLUID_FAILED) {
    // Not every bank/preset combination exists in the soundfont.  Fall back
    // within the same family -- the first percussion set for a drum kit,
    // bank 0 for a melodic voice -- rather than leaving the channel on
    // whatever it had, or swapping a kit for a piano.
    int fallback_bank = bank == PERCUSSION_BANK ? PERCUSSION_BANK : 0;
    int fallback_voice = bank == PERCUSSION_BANK ? 0 : voice;
    printf("no voice %d-%d in the soundfont; falling back to %d-%d\n",
           bank, voice, fallback_bank, fallback_voice);
    fluid_synth_program_select(fl_synth, channel, fl_sfont_id,
                               fallback_bank, fallback_voice);
  }
  printf("set endpoint #%d to voice %d-%d\n", channel, bank, voice);
  if (channel == CHANNEL_DRUM) choose_voice(CHANNEL_KICK, bank, voice);
}

#endif
