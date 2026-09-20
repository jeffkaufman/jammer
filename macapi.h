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

static int jammer_audio_render(void* data, int len, int nfx, float** fx,
                               int nout, float** out) {
  audio_frames_rendered += len;
  return fluid_synth_process((fluid_synth_t*)data, len, nfx, fx, nout, out);
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
  fluid_settings_setnum(fl_settings, "synth.gain", 1.0);
  fluid_settings_setint(fl_settings, "synth.audio-channels", 1);
  fluid_settings_setint(fl_settings, "synth.midi-channels", 16);
  fluid_settings_setint(fl_settings, "synth.reverb.active", 0);
  fluid_settings_setint(fl_settings, "synth.chorus.active", 0);
  // Channel 9 is percussion, as on the Pi (ENDPOINT_DRUM == CHANNEL_DRUM == 9).
  fluid_settings_setstr(fl_settings, "synth.midi-bank-select", "gm");

  fl_synth = new_fluid_synth(fl_settings);
  if (!fl_synth) die("couldn't create fluidsynth synth");

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
  if (bank > 127) bank = 127;
  if (voice < 0) voice = 0;
  if (voice > 127) voice = 127;

  if (!fl_synth) return;

  if (fluid_synth_program_select(fl_synth, channel, fl_sfont_id,
                                 bank, voice) == FLUID_FAILED) {
    // Not every bank/preset combination exists in the soundfont; fall back to
    // bank 0 rather than leaving the channel on whatever it had.
    fluid_synth_program_select(fl_synth, channel, fl_sfont_id, 0, voice);
  }
  printf("set endpoint #%d to voice %d-%d\n", channel, bank, voice);
}

#endif
