#ifndef JML_TALKBOX_H
#define JML_TALKBOX_H

// The talkbox: its own output device, and what comes back from it.
//
//   jammer_audio_render -> talkbox_out_hook -> ring -> talkbox_play -> device
//
// macapi.h mixes what has TB on into the talkbox's buses, apart from the
// main output, and hands the two together to talkbox_push once a block.
// This plays them out of a device of their own -- by default the USB Audio
// Device, a C-Media card -- the same on both its sides, since a talkbox's
// driver is one speaker.  The device runs on its own clock, so between the
// two is a ring, kept near TALKBOX_RING_TARGET by dropping or waiting, the
// way the whistle's is (whistle.h).
//
// The talkbox's tube goes into the mouth, in front of the microphone the
// whistle listens to, and what the microphone hears comes back onto the
// main output: the right, if the mandolin is on the talkbox, since that's
// the mandolin's side, and otherwise the left.  Only while something has TB
// on, so the microphone isn't on the main output otherwise; faded in and
// out over TALKBOX_RETURN_FADE_MS.  After the right's level, like the
// mandolin: it's what that level is matched against.
//
// Include after whistleinput.h.

#define TALKBOX_DEFAULT_DEVICE "USB Audio Device"
#define TALKBOX_RING_FRAMES 16384u  // power of two, for the mask
#define TALKBOX_BLOCK 64            // asked of the device: latency, again
#define TALKBOX_RETURN_FADE_MS 10
#define MAX_TALKBOX_RETURN 2.0

// What the menu asked for, a name or part of one, and what that opened.
static char talkbox_wanted[256] = TALKBOX_DEFAULT_DEVICE;
static char talkbox_device_name[WHISTLE_DEVICE_NAME_MAX];
static char talkbox_error[256];
static AudioDeviceID talkbox_device;
static AudioComponentInstance talkbox_unit;
static int talkbox_channels;

// Bumped by the device's thread, for the watchdog to tell playing from
// stalled.
static _Atomic uint64_t talkbox_frames_played;

static float talkbox_ring[TALKBOX_RING_FRAMES];
static _Atomic unsigned talkbox_ring_write;
static _Atomic unsigned talkbox_ring_read;
static _Atomic unsigned talkbox_ring_target;
static _Atomic bool talkbox_running;
static bool talkbox_priming;  // the device's thread's own: waiting to refill

// The return's level, from the menu, and where its fade has got to.
static _Atomic float talkbox_return_gain = 1.0f;
static float talkbox_return_now;  // the audio thread's own

void set_talkbox_return_gain(double gain) {
  if (gain < 0) gain = 0;
  if (gain > MAX_TALKBOX_RETURN) gain = MAX_TALKBOX_RETURN;
  atomic_store_explicit(&talkbox_return_gain, (float)gain,
                        memory_order_relaxed);
}

// From the main render, through talkbox_out_hook.  Dropped if the device's
// thread has fallen that far behind.
static void talkbox_push(const float* mono, int len) {
  if (!atomic_load_explicit(&talkbox_running, memory_order_acquire)) return;
  unsigned write = atomic_load_explicit(&talkbox_ring_write,
                                        memory_order_relaxed);
  unsigned read = atomic_load_explicit(&talkbox_ring_read,
                                       memory_order_acquire);
  if ((unsigned)len > TALKBOX_RING_FRAMES - (write - read) - 1) return;
  for (int i = 0; i < len; i++) {
    talkbox_ring[(write + (unsigned)i) & (TALKBOX_RING_FRAMES - 1)] = mono[i];
  }
  atomic_store_explicit(&talkbox_ring_write, write + (unsigned)len,
                        memory_order_release);
}

// The device's thread: the ring, onto each of its channels.  Two clocks
// drift, so a ring that's grown past three times its target is brought back
// to it, and one that's run dry waits until it's back at it, rather than
// stuttering a block at a time.
static OSStatus talkbox_play(void* context, AudioUnitRenderActionFlags* flags,
                             const AudioTimeStamp* timestamp, UInt32 bus,
                             UInt32 frames, AudioBufferList* io) {
  (void)context;
  (void)flags;
  (void)timestamp;
  (void)bus;
  unsigned read = atomic_load_explicit(&talkbox_ring_read,
                                       memory_order_relaxed);
  unsigned write = atomic_load_explicit(&talkbox_ring_write,
                                        memory_order_acquire);
  unsigned available = write - read;
  unsigned target = atomic_load_explicit(&talkbox_ring_target,
                                         memory_order_relaxed);
  if (available > target * 3) {
    read += available - target;
    available = target;
  }
  if (talkbox_priming && available >= target) talkbox_priming = false;
  unsigned take = talkbox_priming ? 0
    : available < frames ? available : frames;
  if (take < frames) talkbox_priming = true;
  float* first = (float*)io->mBuffers[0].mData;
  for (UInt32 i = 0; i < frames; i++) {
    first[i] = i < take
      ? talkbox_ring[(read + i) & (TALKBOX_RING_FRAMES - 1)] : 0;
  }
  for (UInt32 b = 1; b < io->mNumberBuffers; b++) {
    memcpy(io->mBuffers[b].mData, first, frames * sizeof(float));
  }
  atomic_store_explicit(&talkbox_ring_read, read + take,
                        memory_order_release);
  atomic_fetch_add_explicit(&talkbox_frames_played, frames,
                            memory_order_relaxed);
  return noErr;
}

static void talkbox_fail(const char* what, OSStatus status) {
  if (status == noErr) {
    snprintf(talkbox_error, sizeof(talkbox_error), "%s", what);
  } else {
    snprintf(talkbox_error, sizeof(talkbox_error),
             "%s (CoreAudio error %d)", what, (int)status);
  }
  printf("talkbox: %s\n", talkbox_error);
}

// The output device named `wanted`, exactly or failing that in part,
// ignoring case, or kAudioObjectUnknown.
static AudioDeviceID talkbox_find(const char* wanted, char* name_out,
                                  size_t name_size) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (!wanted || !*wanted ||
      AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     NULL, &size) != noErr) {
    return kAudioObjectUnknown;
  }
  int count = (int)(size / sizeof(AudioDeviceID));
  AudioDeviceID* ids = (AudioDeviceID*)malloc(size);
  if (!ids) return kAudioObjectUnknown;
  AudioDeviceID found = kAudioObjectUnknown;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                 &size, ids) == noErr) {
    for (int pass = 0; pass < 2 && found == kAudioObjectUnknown; pass++) {
      for (int i = 0; i < count; i++) {
        char name[WHISTLE_DEVICE_NAME_MAX];
        whistle_copy_cfstring(ids[i], kAudioObjectPropertyName, name,
                              sizeof(name));
        if (whistle_output_channels(ids[i]) <= 0) continue;
        if (pass == 0 ? strcasecmp(name, wanted) == 0
                      : strcasestr(name, wanted) != NULL) {
          found = ids[i];
          snprintf(name_out, name_size, "%s", name);
          break;
        }
      }
    }
  }
  free(ids);
  return found;
}

static void talkbox_stop(void) {
  atomic_store_explicit(&talkbox_running, false, memory_order_release);
  if (talkbox_unit) {
    AudioOutputUnitStop(talkbox_unit);
    AudioUnitUninitialize(talkbox_unit);
    AudioComponentInstanceDispose(talkbox_unit);
    talkbox_unit = NULL;
  }
  // Its block size back, unless the rig's other audio is on it too.
  if (talkbox_device != kAudioObjectUnknown &&
      talkbox_device != whistle_output_device &&
      talkbox_device != whistle_input_device) {
    whistle_restore_buffer_frames(talkbox_device);
  }
  talkbox_device = kAudioObjectUnknown;
  talkbox_device_name[0] = '\0';
}

// Open `wanted` for the talkbox, at the rig's `rate`, which the device's
// unit converts from if it runs at another.  Main thread only.  False, with
// talkbox_error set, if it isn't there or won't open, in which case what's
// sent to the talkbox goes nowhere.
static bool talkbox_start(const char* wanted, double rate) {
  talkbox_stop();
  talkbox_error[0] = '\0';
  if (wanted != talkbox_wanted) {
    snprintf(talkbox_wanted, sizeof(talkbox_wanted), "%s", wanted);
  }

  char name[WHISTLE_DEVICE_NAME_MAX];
  AudioDeviceID device = talkbox_find(talkbox_wanted, name, sizeof(name));
  if (device == kAudioObjectUnknown) {
    char message[256];
    snprintf(message, sizeof(message), "no %s", talkbox_wanted);
    talkbox_fail(message, noErr);
    return false;
  }
  int channels = whistle_output_channels(device);
  if (channels > 2) channels = 2;

  if (device != whistle_output_device && device != whistle_input_device) {
    whistle_request_buffer_frames(device, TALKBOX_BLOCK);
  }
  talkbox_device = device;
  int block = whistle_device_buffer_frames(device);
  if (block <= 0) block = 512;
  // The device's block and the rig's, one of each, as for the whistle.
  int output_block = audio_block_frames > 0 ? audio_block_frames : 512;
  atomic_store_explicit(&talkbox_ring_target, (unsigned)(block + output_block),
                        memory_order_relaxed);
  atomic_store_explicit(&talkbox_ring_read,
                        atomic_load_explicit(&talkbox_ring_write,
                                             memory_order_relaxed),
                        memory_order_relaxed);
  talkbox_priming = true;

  AudioComponentDescription description = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
  };
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component) {
    talkbox_fail("no CoreAudio output unit available", noErr);
    talkbox_stop();
    return false;
  }
  OSStatus status = AudioComponentInstanceNew(component, &talkbox_unit);
  if (status != noErr) {
    talkbox_unit = NULL;
    talkbox_fail("could not create the audio unit", status);
    talkbox_stop();
    return false;
  }
  status = AudioUnitSetProperty(talkbox_unit,
                                kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &device,
                                sizeof(device));
  if (status != noErr) {
    talkbox_fail("could not select that device", status);
    talkbox_stop();
    return false;
  }
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
  status = AudioUnitSetProperty(talkbox_unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format,
                                sizeof(format));
  if (status != noErr) {
    talkbox_fail("that device would not accept the format", status);
    talkbox_stop();
    return false;
  }
  AURenderCallbackStruct callback = { talkbox_play, NULL };
  status = AudioUnitSetProperty(talkbox_unit,
                                kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback,
                                sizeof(callback));
  if (status != noErr) {
    talkbox_fail("could not install the output callback", status);
    talkbox_stop();
    return false;
  }
  status = AudioUnitInitialize(talkbox_unit);
  if (status == noErr) {
    atomic_store_explicit(&talkbox_running, true, memory_order_release);
    status = AudioOutputUnitStart(talkbox_unit);
  }
  if (status != noErr) {
    talkbox_fail("could not start that device", status);
    talkbox_stop();
    return false;
  }
  talkbox_channels = channels;
  snprintf(talkbox_device_name, sizeof(talkbox_device_name), "%s", name);
  printf("talkbox: playing on %s, %d-frame blocks, %u-frame ring\n", name,
         block, atomic_load(&talkbox_ring_target));
  return true;
}

// Whether anything has TB on.  On the audio thread.
static bool talkbox_wanted_now(void) {
  return atomic_load_explicit(&audio_talkbox, memory_order_relaxed) ||
    atomic_load_explicit(&mando_pub_talkbox, memory_order_relaxed);
}

// What the microphone heard this block, back onto the main output, while
// anything has TB on.  After the right's level: audio_after_alt_hook.
static void talkbox_return_add(float** out, int nout, int len) {
  int have = whistle_input_block_len;
  whistle_input_block_len = 0;
  float target = talkbox_wanted_now()
    ? atomic_load_explicit(&talkbox_return_gain, memory_order_relaxed) : 0;
  if (have != len || nout < 1) {
    talkbox_return_now = 0;  // nothing heard: start again from silence
    return;
  }
  if (target == 0 && talkbox_return_now == 0) return;
  bool mando = atomic_load_explicit(&mando_pub_talkbox, memory_order_relaxed);
  float* to = out[mando && nout >= 2 ? 1 : 0];
  float step = (float)(1000.0 / (synth_sample_rate * TALKBOX_RETURN_FADE_MS));
  float g = talkbox_return_now;
  for (int i = 0; i < len; i++) {
    g = target > g ? fminf(target, g + step) : fmaxf(target, g - step);
    to[i] += g * whistle_input_block[i];
  }
  talkbox_return_now = g;
}

// The audio thread's after-the-right's-level: the talkbox's return, and the
// mandolin.
static void talkbox_after_alt(float** out, int nout, int len) {
  talkbox_return_add(out, nout, len);
  mando_add(out, nout, len);
}

#endif
