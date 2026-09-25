#ifndef JML_WHISTLE_INPUT_H
#define JML_WHISTLE_INPUT_H

// The microphone, and only the microphone.
//
// whistle-synth's Mac app owns both ends of its audio; here fluidsynth already
// owns the output, so all that's missing is an input stream feeding the ring
// in whistle.h.  One AUHAL unit in input-only mode, its own callback thread,
// straight into whistle_push_input().
//
// Written against the public AudioUnit/HAL API, the same as mac/core/
// whistle_audio.c, which this is modelled on.
//
// Include after whistle.h.

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#define WHISTLE_DEVICE_NAME_MAX 128
#define WHISTLE_DEVICE_UID_MAX 256
#define WHISTLE_MAX_INPUT_DEVICES 32

typedef struct {
  AudioDeviceID id;
  char uid[WHISTLE_DEVICE_UID_MAX];    // stable across reboots and reconnects
  char name[WHISTLE_DEVICE_NAME_MAX];
  int channels;
  double sample_rate;
  bool is_default;
} WhistleInputDevice;

// Why the whistle isn't running, in a sentence meant to be read in the status
// bar.  Empty when it is.
static char whistle_input_error[256];
static char whistle_input_name[WHISTLE_DEVICE_NAME_MAX];

// What the whistle adds on top of whatever fluidsynth's own output is doing:
// the microphone's own block, plus what the ring holds.  Shown in the status
// row, because a latency you can't see is one you can only argue about.
static double whistle_latency_ms;

static void whistle_input_fail(const char* what, OSStatus status) {
  if (status == noErr) {
    snprintf(whistle_input_error, sizeof(whistle_input_error), "%s", what);
  } else {
    snprintf(whistle_input_error, sizeof(whistle_input_error),
             "%s (CoreAudio error %d)", what, (int)status);
  }
  printf("whistle: %s\n", whistle_input_error);
}

/* ----------------------------------------------------------- properties --- */

static OSStatus whistle_get_property(AudioObjectID object,
                                     AudioObjectPropertySelector selector,
                                     AudioObjectPropertyScope scope,
                                     void* out, UInt32 size) {
  AudioObjectPropertyAddress address = {
    selector, scope, kAudioObjectPropertyElementMain };
  UInt32 io_size = size;
  return AudioObjectGetPropertyData(object, &address, 0, NULL, &io_size, out);
}

static bool whistle_copy_cfstring(AudioObjectID object,
                                  AudioObjectPropertySelector selector,
                                  char* out, size_t out_size) {
  out[0] = '\0';
  CFStringRef string = NULL;
  if (whistle_get_property(object, selector, kAudioObjectPropertyScopeGlobal,
                           &string, sizeof(string)) != noErr || !string) {
    return false;
  }
  bool ok = CFStringGetCString(string, out, (CFIndex)out_size,
                               kCFStringEncodingUTF8);
  CFRelease(string);
  return ok;
}

// Channels a device offers for input.  None means it isn't an input device,
// which is how the menu tells microphones from speakers.
static int whistle_input_channels(AudioDeviceID device) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size)
        != noErr || size == 0) {
    return 0;
  }
  AudioBufferList* list = (AudioBufferList*)malloc(size);
  if (!list) return 0;
  int channels = 0;
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, list)
        == noErr) {
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
      channels += (int)list->mBuffers[i].mNumberChannels;
    }
  }
  free(list);
  return channels;
}

static int whistle_output_channels(AudioDeviceID device) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size)
        != noErr || size == 0) {
    return 0;
  }
  AudioBufferList* list = (AudioBufferList*)malloc(size);
  if (!list) return 0;
  int channels = 0;
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, list)
        == noErr) {
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
      channels += (int)list->mBuffers[i].mNumberChannels;
    }
  }
  free(list);
  return channels;
}

static AudioDeviceID whistle_default_output(void) {
  AudioDeviceID device = kAudioObjectUnknown;
  whistle_get_property(kAudioObjectSystemObject,
                       kAudioHardwarePropertyDefaultOutputDevice,
                       kAudioObjectPropertyScopeGlobal, &device,
                       sizeof(device));
  return device;
}

static AudioDeviceID whistle_default_input(void) {
  AudioDeviceID device = kAudioObjectUnknown;
  whistle_get_property(kAudioObjectSystemObject,
                       kAudioHardwarePropertyDefaultInputDevice,
                       kAudioObjectPropertyScopeGlobal, &device,
                       sizeof(device));
  return device;
}

static double whistle_device_rate(AudioDeviceID device) {
  Float64 rate = 0;
  whistle_get_property(device, kAudioDevicePropertyNominalSampleRate,
                       kAudioObjectPropertyScopeGlobal, &rate, sizeof(rate));
  return (double)rate;
}

static int whistle_device_buffer_frames(AudioDeviceID device) {
  UInt32 frames = 0;
  whistle_get_property(device, kAudioDevicePropertyBufferFrameSize,
                       kAudioObjectPropertyScopeGlobal, &frames,
                       sizeof(frames));
  return (int)frames;
}

/* -------------------------------------------------------------- devices --- */

// Input devices only, so the menu doesn't offer you a pair of speakers to
// whistle into.
static int whistle_list_input_devices(WhistleInputDevice* out, int max) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     NULL, &size) != noErr) {
    return 0;
  }
  int count = (int)(size / sizeof(AudioDeviceID));
  if (count <= 0) return 0;

  AudioDeviceID* ids = (AudioDeviceID*)malloc(size);
  if (!ids) return 0;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                 &size, ids) != noErr) {
    free(ids);
    return 0;
  }

  AudioDeviceID default_in = whistle_default_input();
  int found = 0;
  for (int i = 0; i < count && found < max; i++) {
    int channels = whistle_input_channels(ids[i]);
    if (channels == 0) continue;

    WhistleInputDevice* device = &out[found];
    memset(device, 0, sizeof(*device));
    device->id = ids[i];
    device->channels = channels;
    device->is_default = (ids[i] == default_in);
    device->sample_rate = whistle_device_rate(ids[i]);
    whistle_copy_cfstring(ids[i], kAudioObjectPropertyName, device->name,
                          sizeof(device->name));
    whistle_copy_cfstring(ids[i], kAudioDevicePropertyDeviceUID, device->uid,
                          sizeof(device->uid));
    found++;
  }
  free(ids);
  return found;
}

// What to listen to when nothing has been chosen.  The system default is the
// wrong answer on a live rig for the same reason it is on the way out -- on a
// laptop it is the built-in microphone, which is a foot from the speakers and
// pointed at them -- so the interface wins if it is plugged in.  Same
// arrangement as resolve_audio_device on the output side, matched the same
// way: an exact name or any substring of one, so "Scarlett" finds "Scarlett
// 2i2 USB".
#define WHISTLE_PREFERRED_INPUT "Scarlett"

// kAudioObjectUnknown if nothing matches, which is not a failure -- it just
// means falling back to the system default.
static AudioDeviceID whistle_preferred_input(char* name_out,
                                             size_t name_out_len) {
  const char* wanted = getenv("JAMMER_WHISTLE_INPUT");
  if (!wanted || !*wanted) wanted = WHISTLE_PREFERRED_INPUT;

  WhistleInputDevice devices[WHISTLE_MAX_INPUT_DEVICES];
  int count = whistle_list_input_devices(devices, WHISTLE_MAX_INPUT_DEVICES);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < count; i++) {
      bool hit = pass == 0 ? strcmp(devices[i].name, wanted) == 0
                           : strstr(devices[i].name, wanted) != NULL;
      if (hit) {
        if (name_out) snprintf(name_out, name_out_len, "%s", devices[i].name);
        return devices[i].id;
      }
    }
  }
  return kAudioObjectUnknown;
}

// A UID that is no longer there falls back the same way an unset one does:
// unplugging an interface shouldn't leave the whistle unable to make a sound.
static AudioDeviceID whistle_device_for_uid(const char* uid) {
  if (uid && uid[0]) {
    WhistleInputDevice devices[WHISTLE_MAX_INPUT_DEVICES];
    int count = whistle_list_input_devices(devices, WHISTLE_MAX_INPUT_DEVICES);
    for (int i = 0; i < count; i++) {
      if (strcmp(devices[i].uid, uid) == 0) return devices[i].id;
    }
  }
  AudioDeviceID preferred = whistle_preferred_input(NULL, 0);
  return preferred != kAudioObjectUnknown ? preferred
                                          : whistle_default_input();
}

/* --------------------------------------------------------- the sample rate */

// The nominal sample rate belongs to the device, not to us: a device runs at
// one rate for everything using it.  We need the microphone at the rate the
// engine is running at, because the detector reads a stream of samples and
// works out how fast it is wiggling -- hand it 48kHz audio while it believes
// it is at 44.1kHz and every note comes out a semitone and a half sharp.
// Resampling is not an option worth taking either: it would be latency on the
// one path where latency is the whole point.
//
// So: ask.  If the device says no we don't run, and the status bar says which
// rate it wanted -- which is a thing you can fix in Audio MIDI Setup once.
// What we change we put back, on the way out.
static AudioDeviceID whistle_rate_changed_device;
static Float64 whistle_saved_rate;

static bool whistle_request_rate(AudioDeviceID device, double rate) {
  Float64 current = whistle_device_rate(device);
  if (current == (Float64)rate) return true;

  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  Float64 wanted = (Float64)rate;
  if (AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(wanted),
                                 &wanted) != noErr) {
    return false;
  }
  // The device takes a moment to settle, and reports the old rate until it
  // has.  Bounded, so a device that never gets there doesn't hang startup.
  for (int wait = 0; wait < 100; wait++) {
    if (whistle_device_rate(device) == (Float64)rate) break;
    usleep(10000);
  }
  if (whistle_device_rate(device) != (Float64)rate) return false;

  // Only the first time: a restart must not overwrite the original with our
  // own value from the previous run.
  if (whistle_rate_changed_device != device && current > 0) {
    whistle_rate_changed_device = device;
    whistle_saved_rate = current;
  }
  return true;
}

// The latency knob on the input side.  Without asking, the device runs at
// whatever it defaults to -- 512 frames on this Mac's built-in microphone,
// which is 10.7ms of delay before the detector has even seen the sound.
//
// This does *not* change the block size other apps on the device get: the HAL
// keeps a buffer size per client and adapts between them.  That was measured
// rather than assumed -- see "The device is borrowed, not taken" in
// whistle-synth's mac/README.md.  It is restored anyway, on the principle
// that what we changed we put back.
// One slot per direction: at most an input and an output are held at once.
// A slot is free again once it's been put back (frames 0), since switching
// devices from the menus lets go of one and takes another.
static struct {
  AudioDeviceID device;
  UInt32 frames;
} whistle_saved_frames[2];
static int whistle_saved_frames_count;

static void whistle_remember_frames(AudioDeviceID device, UInt32 frames) {
  int free_slot = -1;
  for (int i = 0; i < whistle_saved_frames_count; i++) {
    if (!whistle_saved_frames[i].frames) {
      if (free_slot < 0) free_slot = i;
    } else if (whistle_saved_frames[i].device == device) {
      return;  // only the first
    }
  }
  if (free_slot < 0) {
    if (whistle_saved_frames_count >= 2) return;
    free_slot = whistle_saved_frames_count++;
  }
  whistle_saved_frames[free_slot].device = device;
  whistle_saved_frames[free_slot].frames = frames;
}

static void whistle_request_buffer_frames(AudioDeviceID device, int frames) {
  if (frames <= 0) return;

  AudioValueRange range = {0, 0};
  if (whistle_get_property(device, kAudioDevicePropertyBufferFrameSizeRange,
                           kAudioObjectPropertyScopeGlobal, &range,
                           sizeof(range)) == noErr) {
    if (frames < (int)range.mMinimum) frames = (int)range.mMinimum;
    if (range.mMaximum > 0 && frames > (int)range.mMaximum) {
      frames = (int)range.mMaximum;
    }
  }

  UInt32 current = 0;
  bool have_current =
    whistle_get_property(device, kAudioDevicePropertyBufferFrameSize,
                         kAudioObjectPropertyScopeGlobal, &current,
                         sizeof(current)) == noErr && current > 0;
  if (have_current && current == (UInt32)frames) return;

  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 wanted = (UInt32)frames;
  if (AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(wanted),
                                 &wanted) != noErr) {
    return;
  }
  if (have_current) whistle_remember_frames(device, current);
}

// Put back whatever we changed.  `device` restores just that one, or
// kAudioObjectUnknown for all of them.
static void whistle_restore_buffer_frames(AudioDeviceID device) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  for (int i = 0; i < whistle_saved_frames_count; i++) {
    if (device != kAudioObjectUnknown &&
        whistle_saved_frames[i].device != device) {
      continue;
    }
    if (!whistle_saved_frames[i].frames) continue;
    AudioObjectSetPropertyData(whistle_saved_frames[i].device, &address, 0,
                               NULL, sizeof(whistle_saved_frames[i].frames),
                               &whistle_saved_frames[i].frames);
    whistle_saved_frames[i].frames = 0;
  }
}

static void whistle_restore_rate(void) {
  if (!whistle_rate_changed_device || whistle_saved_rate <= 0) return;
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  AudioObjectSetPropertyData(whistle_rate_changed_device, &address, 0, NULL,
                             sizeof(whistle_saved_rate), &whistle_saved_rate);
  whistle_rate_changed_device = 0;
  whistle_saved_rate = 0;
}

/* ------------------------------------------------- the output device too --- */

// fluidsynth's CoreAudio driver never asks for a block size, so its client
// inherits whatever the device is set to -- which is how a 512-frame default
// ends up setting the pace for the whole rig, the whistle's ring included.
// Setting audio.period-size instead makes the driver open and then never pull
// a sample (see macapi.h, and it still does -- measured), so this is the way
// in.
//
// Done before start_synth, by the name fluidsynth uses for the device.  Best
// effort: a device that won't take it keeps its own, and everything still
// works, just at the old latency.
static AudioDeviceID whistle_output_device;

static void whistle_prepare_output_device(const char* name, int frames) {
  if (!name || !*name || frames <= 0) return;

  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     NULL, &size) != noErr) {
    return;
  }
  int count = (int)(size / sizeof(AudioDeviceID));
  AudioDeviceID* ids = (AudioDeviceID*)malloc(size);
  if (!ids) return;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                 &size, ids) == noErr) {
    for (int i = 0; i < count; i++) {
      char device_name[WHISTLE_DEVICE_NAME_MAX];
      whistle_copy_cfstring(ids[i], kAudioObjectPropertyName, device_name,
                            sizeof(device_name));
      // fluidsynth reports "default" for the system default, which is the
      // one case where there is no name to match.  Otherwise matched the way
      // fluidsynth matches, ignoring case, so it's the device it will open.
      bool matches = strcmp(name, "default") == 0
        ? ids[i] == whistle_default_output()
        : strcasecmp(device_name, name) == 0;
      if (matches && whistle_output_channels(ids[i]) > 0) {
        whistle_output_device = ids[i];
        whistle_request_buffer_frames(ids[i], frames);
        printf("whistle: asked %s for %d-frame output blocks, got %d\n",
               device_name, frames, whistle_device_buffer_frames(ids[i]));
        break;
      }
    }
  }
  free(ids);
}

/* --------------------------------------------------------------- stream --- */

static AudioComponentInstance whistle_unit;
static AudioDeviceID whistle_input_device;   // so teardown knows what to undo
static AudioBufferList* whistle_capture_list;
static float* whistle_capture_scratch;
static UInt32 whistle_capture_frames;
static int whistle_capture_channels;

// The biggest block we will ever answer, which sets the one buffer the output
// thread reads.  Allocated once and never freed: keeping it alive across a
// device change is what lets the mix carry on reading it while the input side
// is being rebuilt underneath.
#define WHISTLE_MAX_BLOCK 8192

static void whistle_free_capture(void) {
  free(whistle_capture_list);
  whistle_capture_list = NULL;
  free(whistle_capture_scratch);
  whistle_capture_scratch = NULL;
  whistle_capture_frames = 0;
}

static bool whistle_allocate_capture(UInt32 frames, int channels) {
  whistle_free_capture();
  if (frames > WHISTLE_MAX_BLOCK) frames = WHISTLE_MAX_BLOCK;
  size_t list_size = sizeof(AudioBufferList) +
    sizeof(AudioBuffer) * (size_t)(channels > 0 ? channels - 1 : 0);
  whistle_capture_list = (AudioBufferList*)calloc(1, list_size);
  whistle_capture_scratch =
    (float*)calloc(frames * (size_t)(channels > 0 ? channels : 1),
                   sizeof(float));
  if (!whistle_input_block) {
    whistle_input_block = (float*)calloc(WHISTLE_MAX_BLOCK, sizeof(float));
    whistle_input_block_2 = (float*)calloc(WHISTLE_MAX_BLOCK, sizeof(float));
    whistle_input_block_frames = WHISTLE_MAX_BLOCK;
  }
  if (!whistle_capture_list || !whistle_capture_scratch ||
      !whistle_input_block || !whistle_input_block_2) {
    whistle_free_capture();
    whistle_input_fail("out of memory", noErr);
    return false;
  }
  whistle_capture_frames = frames;
  whistle_capture_channels = channels;
  atomic_store_explicit(&whistle_input_count, channels, memory_order_relaxed);
  return true;
}

// AUHAL wants the sizes right on every call, and may hand back fewer frames
// than it asked for.
static void whistle_prepare_list(UInt32 frames) {
  whistle_capture_list->mNumberBuffers = (UInt32)whistle_capture_channels;
  for (int channel = 0; channel < whistle_capture_channels; channel++) {
    whistle_capture_list->mBuffers[channel].mNumberChannels = 1;
    whistle_capture_list->mBuffers[channel].mDataByteSize =
      frames * sizeof(float);
    whistle_capture_list->mBuffers[channel].mData =
      whistle_capture_scratch + (size_t)channel * whistle_capture_frames;
  }
}

// Only channel 0 carries the whistle.  Everything else the device offers is
// kept out of it rather than mixed in, so an interface with a guitar in
// input 2 doesn't confuse the detector -- but channel 1 goes along beside
// it, for the vocal effects to take if they're asked to (whistle_fx_mic).
static OSStatus whistle_capture(void* context,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* timestamp,
                                UInt32 bus, UInt32 frames,
                                AudioBufferList* io) {
  (void)context;
  (void)io;
  if (frames > whistle_capture_frames) return noErr;

  whistle_prepare_list(frames);
  if (AudioUnitRender(whistle_unit, flags, timestamp, bus, frames,
                      whistle_capture_list) != noErr) {
    return noErr;
  }
  whistle_push_input(
    (const float*)whistle_capture_list->mBuffers[0].mData,
    whistle_capture_channels > 1
      ? (const float*)whistle_capture_list->mBuffers[1].mData : NULL,
    (int)frames);
  return noErr;
}

static void whistle_input_stop(void) {
  whistle_engine_silence();
  if (whistle_unit) {
    AudioOutputUnitStop(whistle_unit);
    AudioUnitUninitialize(whistle_unit);
    AudioComponentInstanceDispose(whistle_unit);
    whistle_unit = NULL;
  }
  whistle_free_capture();
  // After the unit is gone, so the device will accept the changes.  Only the
  // microphone: the output device belongs to fluidsynth for as long as it is
  // running, and stopping listening is not a reason to disturb it.
  if (whistle_input_device != kAudioObjectUnknown) {
    whistle_restore_buffer_frames(whistle_input_device);
    whistle_input_device = kAudioObjectUnknown;
  }
  whistle_restore_rate();
  whistle_available = false;
  whistle_input_name[0] = '\0';
}

// Open `uid` (empty for the system default) and start feeding the ring.  The
// engine runs at `rate`, which is fluidsynth's own rate -- see
// whistle_request_rate for why they have to agree.
//
// Returns false with whistle_input_error set.  Not audio-thread safe and not
// re-entrant: call it from the main thread.
static bool whistle_input_start(const char* uid, double rate) {
  whistle_input_stop();
  whistle_input_error[0] = '\0';

  AudioDeviceID device = whistle_device_for_uid(uid);
  if (device == kAudioObjectUnknown) {
    whistle_input_fail("no microphone is available", noErr);
    return false;
  }
  int channels = whistle_input_channels(device);
  if (channels <= 0) {
    whistle_input_fail("that device has no input channels", noErr);
    return false;
  }

  char name[WHISTLE_DEVICE_NAME_MAX];
  whistle_copy_cfstring(device, kAudioObjectPropertyName, name, sizeof(name));

  if (!whistle_request_rate(device, rate)) {
    char message[256];
    snprintf(message, sizeof(message),
             "%s runs at %.0f Hz and jammer is at %.0f Hz; set it to %.0f in "
             "Audio MIDI Setup", name[0] ? name : "that microphone",
             whistle_device_rate(device), rate, rate);
    whistle_input_fail(message, noErr);
    return false;
  }

  // Small, because this is latency on the one path where latency is the whole
  // point.  $JAMMER_WHISTLE_BUFFER is here for experimenting; the device
  // clamps whatever it is asked for to what it supports.
  const char* buffer_env = getenv("JAMMER_WHISTLE_BUFFER");
  whistle_request_buffer_frames(device, buffer_env ? atoi(buffer_env) : 64);

  whistle_input_device = device;
  int buffer_frames = whistle_device_buffer_frames(device);
  if (buffer_frames <= 0) buffer_frames = 512;
  UInt32 max_frames = (UInt32)(buffer_frames * 4);
  if (max_frames < 2048) max_frames = 2048;
  if (max_frames > WHISTLE_MAX_BLOCK) max_frames = WHISTLE_MAX_BLOCK;
  if (!whistle_allocate_capture(max_frames, channels)) return false;

  // How much to keep buffered between the microphone's thread and
  // fluidsynth's.  One of each block is the least that can work: the consumer
  // takes a whole output block at once, so that much has to be there when it
  // runs, and the producer only tops it up an input block at a time.
  //
  // It used to be two input blocks, which had nothing to do with what the
  // consumer asks for and was 21ms on a device defaulting to 512 frames.
  int output_block = audio_block_frames > 0 ? audio_block_frames : 512;
  unsigned target = (unsigned)(buffer_frames + output_block);
  atomic_store_explicit(&whistle_ring_target, target, memory_order_relaxed);

  whistle_latency_ms = 1000.0 * (buffer_frames + (int)target) / rate;

  AudioComponentDescription description = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
  };
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component) {
    whistle_input_fail("no CoreAudio input unit available", noErr);
    return false;
  }
  OSStatus status = AudioComponentInstanceNew(component, &whistle_unit);
  if (status != noErr) {
    whistle_input_fail("could not create the audio unit", status);
    whistle_input_stop();
    return false;
  }

  UInt32 on = 1, off = 0;
  status = AudioUnitSetProperty(whistle_unit,
                                kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Input, 1, &on, sizeof(on));
  if (status == noErr) {
    status = AudioUnitSetProperty(whistle_unit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output, 0, &off,
                                  sizeof(off));
  }
  if (status != noErr) {
    whistle_input_fail("could not open that device for input", status);
    whistle_input_stop();
    return false;
  }

  status = AudioUnitSetProperty(whistle_unit,
                                kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &device,
                                sizeof(device));
  if (status != noErr) {
    whistle_input_fail("could not select that microphone", status);
    whistle_input_stop();
    return false;
  }

  status = AudioUnitSetProperty(whistle_unit,
                                kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &max_frames,
                                sizeof(max_frames));
  if (status != noErr) {
    whistle_input_fail("could not set the input block size", status);
    whistle_input_stop();
    return false;
  }

  // Non-interleaved float32, which is what AUHAL speaks natively, so nothing
  // has to be de-interleaved on the audio thread.
  AudioStreamBasicDescription format = {
    .mSampleRate = rate,
    .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                    kAudioFormatFlagIsNonInterleaved,
    .mBitsPerChannel = 32,
    .mChannelsPerFrame = (UInt32)channels,
    .mFramesPerPacket = 1,
    .mBytesPerFrame = sizeof(float),
    .mBytesPerPacket = sizeof(float),
  };
  status = AudioUnitSetProperty(whistle_unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Output, 1, &format,
                                sizeof(format));
  if (status != noErr) {
    whistle_input_fail("that device would not accept the input format",
                       status);
    whistle_input_stop();
    return false;
  }

  AURenderCallbackStruct callback = { whistle_capture, NULL };
  status = AudioUnitSetProperty(whistle_unit,
                                kAudioOutputUnitProperty_SetInputCallback,
                                kAudioUnitScope_Global, 0, &callback,
                                sizeof(callback));
  if (status != noErr) {
    whistle_input_fail("could not install the input callback", status);
    whistle_input_stop();
    return false;
  }

  // Everything the audio thread will touch has to exist before it runs.
  whistle_engine_prepare(rate);
  whistle_publish();

  status = AudioUnitInitialize(whistle_unit);
  if (status != noErr) {
    whistle_input_fail("could not open the microphone", status);
    whistle_input_stop();
    return false;
  }
  status = AudioOutputUnitStart(whistle_unit);
  if (status != noErr) {
    whistle_input_fail("could not start the microphone", status);
    whistle_input_stop();
    return false;
  }

  // Let the microphone get a block or two ahead before fluidsynth's thread
  // starts asking, so the first few blocks don't count as dropouts.  Bounded,
  // so a device that never delivers doesn't hang startup.
  for (int wait = 0; wait < 200 && whistle_ring_fill() < target; wait++) {
    usleep(1000);
  }

  snprintf(whistle_input_name, sizeof(whistle_input_name), "%s", name);
  whistle_available = true;
  atomic_store_explicit(&whistle_engine_live, 1, memory_order_release);
  printf("whistle: listening on %s at %.0f Hz, %d-frame blocks, "
         "%u-frame ring (%.1fms in + %.1fms detect)\n",
         whistle_input_name, rate, buffer_frames, target, whistle_latency_ms,
         500.0 * whistle_engine.detector.window_len / rate);
  return true;
}

#endif
