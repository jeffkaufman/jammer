#ifndef JML_FKEYS_H
#define JML_FKEYS_H

// By default macOS turns F1-F12 into brightness/volume/etc, and an app never
// sees them unless you hold fn.  Jammer wants them as plain function keys.
//
// HIDFKeyMode is the live system parameter behind the "Use F1, F2, etc. keys
// as standard function keys" checkbox.  Setting it takes effect immediately
// and needs neither root nor Accessibility permission, so we flip it while
// jammer has the keyboard and put it back the moment it doesn't.  Anywhere
// else -- another app, or jammer not running -- the F-keys do their usual
// macOS thing.
//
// It's a system-wide setting with no per-device form, so while jammer is
// frontmost this applies to the built-in keyboard too.

#include <dispatch/dispatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/IOHIDShared.h>

#define FKEY_MODE_MEDIA 0     // kIOHIDFKeyModeApple: media keys, fn for F1-F12
#define FKEY_MODE_STANDARD 1  // plain F1-F12, fn for media

// The live HIDFKeyMode is volatile state; the durable answer to "what does the
// user actually want" is the preference behind the System Settings checkbox.
// We restore to that rather than to a value we remembered at startup, so a
// hard kill that leaves the live setting flipped heals itself the next time
// jammer runs, instead of being mistaken for a deliberate choice.
#define FKEY_PREF CFSTR("com.apple.keyboard.fnState")

// True once we've flipped the live setting, so we don't do needless work.
static bool fkeys_held = false;

static io_connect_t fkeys_open() {
  io_service_t service = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching("IOHIDSystem"));
  if (!service) return 0;

  io_connect_t handle = 0;
  kern_return_t result = IOServiceOpen(service, mach_task_self(),
                                       kIOHIDParamConnectType, &handle);
  IOObjectRelease(service);
  return result == KERN_SUCCESS ? handle : 0;
}

static bool fkeys_set(int mode) {
  io_connect_t handle = fkeys_open();
  if (!handle) return false;

  uint32_t value = mode;
  kern_return_t result = IOHIDSetParameter(
      handle, CFSTR(kIOHIDFKeyModeKey), &value, sizeof(value));
  IOServiceClose(handle);
  return result == KERN_SUCCESS;
}

// What the System Settings checkbox says.  Unset means the macOS default,
// which is media keys.
static int fkeys_preferred_mode(void) {
  CFPreferencesAppSynchronize(kCFPreferencesAnyApplication);
  CFPropertyListRef value =
      CFPreferencesCopyAppValue(FKEY_PREF, kCFPreferencesAnyApplication);
  if (!value) return FKEY_MODE_MEDIA;

  int mode = FKEY_MODE_MEDIA;
  if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
    mode = CFBooleanGetValue(value) ? FKEY_MODE_STANDARD : FKEY_MODE_MEDIA;
  } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
    int n = 0;
    CFNumberGetValue(value, kCFNumberIntType, &n);
    mode = n ? FKEY_MODE_STANDARD : FKEY_MODE_MEDIA;
  }
  CFRelease(value);
  return mode;
}

// Hand the F-keys back.  Safe to call repeatedly and from either thread: it
// only clears the held flag once the restore has actually gone through, so a
// call that races with a dying process doesn't make a later one skip the work.
//
// The IOKit calls aren't async-signal-safe, so calling this from a crash
// handler is best-effort -- but leaving the setting flipped is worse than the
// risk.
static void fkeys_release(void) {
  if (!fkeys_held) return;
  if (fkeys_set(fkeys_preferred_mode())) {
    fkeys_held = false;
  }
}

static void fkeys_signal_handler(int sig) {
  fkeys_release();
  signal(sig, SIG_DFL);
  raise(sig);
}

// Take the F-keys, whatever state a previous run may have left them in.
static void fkeys_grab(void) {
  if (fkeys_held) return;
  if (fkeys_preferred_mode() == FKEY_MODE_STANDARD) {
    return;  // already how they want it; nothing to take or give back
  }
  if (!fkeys_set(FKEY_MODE_STANDARD)) {
    printf("couldn't take over the function keys; hold fn for F1-F10\n");
    return;
  }
  fkeys_held = true;
}

// If we die without running our normal teardown the setting would be left
// flipped, so cover the ways we might go.
static void fkeys_install_restore_handlers(void) {
  atexit(fkeys_release);

  // Graceful signals go through dispatch sources, which run the handler on a
  // normal queue where the IOKit calls are legal.
  static int graceful[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
  int n_graceful = (int)(sizeof(graceful) / sizeof(graceful[0]));
  // These have to outlive this function: under ARC a dispatch source released
  // at the end of scope is cancelled, and with the signal already set to
  // SIG_IGN that would leave the app ignoring it outright.
  static dispatch_source_t fkeys_signal_sources[4];
  for (int i = 0; i < n_graceful; i++) {
    int sig = graceful[i];
    dispatch_source_t source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_SIGNAL, sig, 0, dispatch_get_main_queue());
    if (!source) continue;  // keep the default disposition rather than ignore
    dispatch_source_set_event_handler(source, ^{
      fkeys_release();
      exit(128 + sig);
    });
    dispatch_resume(source);
    fkeys_signal_sources[i] = source;
    signal(sig, SIG_IGN);  // only now that the source is live
  }

  // Crashes can't wait for a run loop, so these stay best-effort.
  int crashes[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};
  for (int i = 0; i < (int)(sizeof(crashes) / sizeof(crashes[0])); i++) {
    signal(crashes[i], fkeys_signal_handler);
  }
}

#endif
