// Mac version of jammer.
//
// The Pi runs three processes: fluidsynth, jammer (ALSA MIDI routing), and
// kbd.py (evdev -> MIDI keypad).  Here it's one app:
//
//   * fluidsynth is linked in-process (macapi.h), so send_midi() goes straight
//     to the synth.
//   * MIDI inputs (piano, breath controller, foot pedals) come in over
//     CoreMIDI.
//   * The keypad is the Mac's own keyboard, drawn on screen so you can see
//     what every key does and what's currently switched on.
//
// jammermidilib.h -- all the actual musical logic -- is shared unchanged.

// CoreMIDI's packet-list API is deprecated in favour of the UMP one, and the
// IOHIDSystem parameter calls in fkeys.h are deprecated with no replacement
// that does the same job.  Both still work.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <CoreMIDI/CoreMIDI.h>
#include <pthread.h>

#include "macapi.h"
#include "jammermidilib.h"
#include "voices.h"
#include "keylayout.h"
#include "keypad.h"
#include "fkeys.h"

// ---------------------------------------------------------------------------
// Shared state
//
// jml_tick() runs on its own thread, CoreMIDI delivers on its own thread, and
// the GUI runs on the main thread.  Everything that touches jammermidilib
// state holds this lock.
// ---------------------------------------------------------------------------

static pthread_mutex_t jammer_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&jammer_lock)
#define UNLOCK() pthread_mutex_unlock(&jammer_lock)

// Which device a CoreMIDI source is, passed through as the connection refCon.
typedef enum {
  SRC_PIANO = 1,
  SRC_BREATH,
  SRC_FEET,
} SourceKind;

// ---------------------------------------------------------------------------
// MIDI activity
//
// So you can see at a glance whether each device is actually sending -- a
// dead cable or a device that enumerated under an unexpected name looks
// exactly like "jammer is broken" otherwise.
// ---------------------------------------------------------------------------

typedef struct {
  bool connected;
  uint64_t count;
  uint64_t last_ns;
  int status, data1, data2;
} MidiActivity;

#define N_SOURCE_KINDS 4  // unused 0, then SRC_PIANO, SRC_BREATH, SRC_FEET
static MidiActivity midi_activity[N_SOURCE_KINDS];

static const char* source_kind_name(int kind) {
  switch (kind) {
  case SRC_PIANO:  return "piano";
  case SRC_BREATH: return "breath";
  case SRC_FEET:   return "pedals";
  }
  return "?";
}

// A consistent read of everything the GUI draws, so we can let go of the lock
// before doing any actual drawing.
typedef struct {
  bool lit[N_KEYS];
  int selected_endpoint;
  int selected_voice;
  int root_note;
  int musical_mode;
  int octave_delta;
  int volume_delta;
  int air;
  int bpm;
  int armed_note;
  int n_digits_read;
  char digits_read[4];
  bool on[N_ENDPOINTS];
  MidiActivity midi[N_SOURCE_KINDS];
  uint64_t now_ns;
  char audio_device[256];
} Snapshot;

static void take_snapshot(Snapshot* s) {
  LOCK();
  for (int i = 0; i < N_KEYS; i++) {
    s->lit[i] = KEYS[i].label ? key_is_lit(&KEYS[i]) : false;
  }
  int sel = c->selected_endpoint;
  s->selected_endpoint = sel;
  s->selected_voice = c->voices[sel];
  s->root_note = root_note;
  s->musical_mode = musical_mode;
  s->octave_delta = c->octave_deltas[sel];
  s->volume_delta = c->volume_deltas[sel];
  s->air = (int)air;
  s->bpm = current_beat_ns > 0 ? (int)(60 * NS_PER_SEC / current_beat_ns) : 0;
  s->armed_note = armed_note;
  s->n_digits_read = n_digits_read;
  memcpy(s->digits_read, digits_read, sizeof(s->digits_read));
  for (int i = 0; i < N_ENDPOINTS; i++) {
    s->on[i] = c->on[i];
  }
  memcpy(s->midi, midi_activity, sizeof(s->midi));
  s->now_ns = now();
  snprintf(s->audio_device, sizeof(s->audio_device), "%s", audio_device);
  UNLOCK();
}

static const char* mode_name(int mode) {
  switch (mode) {
  case MODE_MAJOR: return "major";
  case MODE_MIXO: return "mixolydian";
  case MODE_MINOR: return "minor";
  case MODE_BETH_COHENS: return "beth cohen's";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// The keyboard view
// ---------------------------------------------------------------------------

#define STATUS_HEIGHT 74.0
#define KEY_GAP 4.0
#define VIEW_PAD 12.0

@interface JammerView : NSView {
  Snapshot snapshot;
  // Momentary keys have no lasting state, so flash them briefly when struck.
  NSTimeInterval flash_time[N_KEYS];
}
- (void)strikeKeyAtIndex:(int)index;
- (int)indexForVirtualKeyCode:(int)vk;
@end

static NSColor* group_color(KeyGroup group) {
  switch (group) {
  case GROUP_SELECT:   return [NSColor colorWithSRGBRed:0.31 green:0.60
                                                   blue:1.00 alpha:1];
  case GROUP_TOGGLE:   return [NSColor colorWithSRGBRed:0.24 green:0.85
                                                   blue:0.47 alpha:1];
  case GROUP_VOICE:    return [NSColor colorWithSRGBRed:1.00 green:0.63
                                                   blue:0.20 alpha:1];
  case GROUP_MODIFIER: return [NSColor colorWithSRGBRed:0.74 green:0.50
                                                   blue:1.00 alpha:1];
  case GROUP_GLOBAL:   return [NSColor colorWithSRGBRed:0.25 green:0.82
                                                   blue:0.85 alpha:1];
  case GROUP_NONE:     break;
  }
  return [NSColor colorWithSRGBRed:0.35 green:0.36 blue:0.40 alpha:1];
}

@implementation JammerView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (int)indexForVirtualKeyCode:(int)vk {
  // Mac laptops label backspace "delete"; accept the full-keyboard forward
  // delete for it too.
  if (vk == kVK_ForwardDelete) vk = kVK_Delete;
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].vk == vk && KEYS[i].label) return i;
  }
  return -1;
}

- (void)strikeKeyAtIndex:(int)index {
  if (index < 0 || index >= N_KEYS) return;
  if (!KEYS[index].label) return;

  LOCK();
  keypad_key(KEYS[index].note);
  UNLOCK();

  flash_time[index] = [NSDate timeIntervalSinceReferenceDate];
  [self setNeedsDisplay:YES];
}

// Geometry: the layout is N_LAYOUT_COLS x N_LAYOUT_ROWS key units, scaled to
// fill whatever size the window is.
- (NSRect)rectForKey:(const Key*)key {
  NSRect b = self.bounds;
  double top = STATUS_HEIGHT;
  double avail_w = b.size.width - 2 * VIEW_PAD;
  double avail_h = b.size.height - top - VIEW_PAD;
  double unit_w = avail_w / N_LAYOUT_COLS;
  double unit_h = avail_h / N_LAYOUT_ROWS;
  double h = key->h > 0 ? key->h : 1.0;
  return NSMakeRect(VIEW_PAD + key->x * unit_w,
                    top + key->row * unit_h,
                    key->w * unit_w - KEY_GAP,
                    h * unit_h - KEY_GAP);
}

- (void)drawString:(NSString*)s
            inRect:(NSRect)rect
              font:(NSFont*)font
             color:(NSColor*)color
           centered:(BOOL)centered {
  NSMutableParagraphStyle* style =
    [[NSMutableParagraphStyle defaultParagraphStyle] mutableCopy];
  style.alignment = centered ? NSTextAlignmentCenter : NSTextAlignmentLeft;
  style.lineBreakMode = NSLineBreakByClipping;
  [s drawInRect:rect withAttributes:@{
    NSFontAttributeName: font,
    NSForegroundColorAttributeName: color,
    NSParagraphStyleAttributeName: style,
  }];
}

- (void)drawKey:(const Key*)key index:(int)i {
  NSRect r = [self rectForKey:key];
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:r
                                                       xRadius:5 yRadius:5];

  NSColor* color = group_color(key->group);
  bool lit = snapshot.lit[i];
  bool unbound = (key->label == NULL);

  NSTimeInterval since_flash =
    [NSDate timeIntervalSinceReferenceDate] - flash_time[i];
  bool flashing = since_flash < 0.18;

  if (lit) {
    [[color colorWithAlphaComponent:0.90] setFill];
  } else if (flashing) {
    [[color colorWithAlphaComponent:0.55] setFill];
  } else if (unbound) {
    [[NSColor colorWithSRGBRed:0.11 green:0.11 blue:0.13 alpha:1] setFill];
  } else {
    [[NSColor colorWithSRGBRed:0.16 green:0.17 blue:0.20 alpha:1] setFill];
  }
  [path fill];

  // The endpoint the modifier keys currently act on gets a bright outline.
  bool is_selected_endpoint =
    (key->lit == LIT_SEL_EP && key->arg == snapshot.selected_endpoint);
  [[color colorWithAlphaComponent:unbound ? 0.25 : (lit ? 1.0 : 0.5)] setStroke];
  path.lineWidth = is_selected_endpoint ? 3.0 : 1.0;
  [path stroke];

  // Key cap letter, small, top left.
  NSColor* cap_color = lit
    ? [NSColor colorWithWhite:0.08 alpha:0.75]
    : [NSColor colorWithWhite:unbound ? 0.35 : 0.62 alpha:1];
  [self drawString:@(key->cap)
            inRect:NSMakeRect(r.origin.x + 5,
                              r.size.height < 34 ? r.origin.y + 4
                                                 : r.origin.y + 2,
                              r.size.width - 8, 14)
              font:[NSFont monospacedSystemFontOfSize:10
                                               weight:NSFontWeightBold]
             color:cap_color
          centered:NO];

  if (unbound) return;

  // What it does, centred in the rest of the key.
  const char* label = key->label;
  if (snapshot.selected_endpoint == ENDPOINT_DRUM && key->drum_label) {
    label = key->drum_label;
  }
  NSString* text = @(label);  // embedded \n in the table splits lines

  // Keys that carry a running value show it instead of a static label.
  if (key->lit == LIT_OCTAVE && snapshot.octave_delta != 0) {
    text = [NSString stringWithFormat:@"OCT\n%+d", snapshot.octave_delta];
  } else if (key->lit == LIT_VOLUME && snapshot.volume_delta != 0) {
    text = [NSString stringWithFormat:@"VOL\n%+d", snapshot.volume_delta];
  }

  CGFloat size = r.size.height > 52 ? 12 : 10.5;
  if (r.size.height < 34) size = 9.5;
  NSFont* font = [NSFont systemFontOfSize:size weight:NSFontWeightSemibold];
  NSInteger lines = [[text componentsSeparatedByString:@"\n"] count];
  CGFloat text_h = lines * (size + 2);
  CGFloat cap_room = r.size.height < 34 ? 0 : 14;
  NSRect text_rect = NSMakeRect(r.origin.x + 2,
                                r.origin.y + cap_room +
                                  (r.size.height - cap_room - text_h) / 2,
                                r.size.width - 4, text_h + 2);
  [self drawString:text
            inRect:text_rect
              font:font
             color:(lit ? [NSColor colorWithWhite:0.06 alpha:1]
                        : [NSColor colorWithWhite:0.88 alpha:1])
          centered:YES];
}

- (void)drawStatus {
  NSRect b = self.bounds;
  NSString* endpoint = @(ENDPOINT_NAMES[snapshot.selected_endpoint]);

  NSMutableString* line = [NSMutableString string];
  [line appendFormat:@"%@", endpoint];
  if (snapshot.selected_endpoint != ENDPOINT_DRUM) {
    [line appendFormat:@"  voice %d", snapshot.selected_voice];
  }
  [line appendFormat:@"   %s %s",
        note_str(snapshot.root_note), mode_name(snapshot.musical_mode)];
  if (snapshot.bpm > 0) {
    [line appendFormat:@"   %d bpm", snapshot.bpm];
  }
  [line appendFormat:@"   air %d", snapshot.air];
  if (snapshot.armed_note) {
    [line appendFormat:@"   %s: %.*s%.*s",
          snapshot.armed_note == F8 ? "root note" : "volume",
          snapshot.n_digits_read, snapshot.digits_read,
          3 - snapshot.n_digits_read, "___"];
  }

  [self drawString:line
            inRect:NSMakeRect(VIEW_PAD, 10, b.size.width - 2 * VIEW_PAD, 24)
              font:[NSFont monospacedSystemFontOfSize:15
                                               weight:NSFontWeightMedium]
             color:[NSColor colorWithWhite:0.95 alpha:1]
          centered:NO];

  // Which endpoints are making sound right now.
  NSMutableString* playing = [NSMutableString stringWithString:@"on: "];
  bool any = false;
  for (int i = 0; i < N_ENDPOINTS; i++) {
    if (snapshot.on[i]) {
      [playing appendFormat:@"%s%s", any ? "  " : "", ENDPOINT_NAMES[i]];
      any = true;
    }
  }
  if (!any) [playing appendString:@"—"];

  [self drawString:playing
            inRect:NSMakeRect(VIEW_PAD, 32, b.size.width - 2 * VIEW_PAD, 18)
              font:[NSFont monospacedSystemFontOfSize:11
                                               weight:NSFontWeightRegular]
             color:[NSColor colorWithSRGBRed:0.24 green:0.85
                                        blue:0.47 alpha:1]
          centered:NO];

  [self drawDeviceRow];
}

// One entry per MIDI source, with a dot that lights when something arrives and
// the last message alongside it, then the audio device we're playing through.
- (void)drawDeviceRow {
  NSFont* font = [NSFont monospacedSystemFontOfSize:11
                                             weight:NSFontWeightRegular];
  CGFloat x = VIEW_PAD;
  CGFloat y = 50;

  int kinds[] = {SRC_FEET, SRC_BREATH, SRC_PIANO};
  for (int i = 0; i < 3; i++) {
    MidiActivity* a = &snapshot.midi[kinds[i]];

    // Fade the dot out over a third of a second after the last message.
    double age = a->last_ns
      ? (double)(snapshot.now_ns - a->last_ns) / NS_PER_SEC : 999;
    double heat = age < 0.33 ? 1.0 - (age / 0.33) : 0.0;

    NSColor* dot;
    if (!a->connected) {
      dot = [NSColor colorWithSRGBRed:0.55 green:0.20 blue:0.20 alpha:1];
    } else if (a->count == 0) {
      dot = [NSColor colorWithWhite:0.32 alpha:1];  // present but silent
    } else {
      dot = [NSColor colorWithSRGBRed:0.24 green:0.55 + 0.35 * heat
                                 blue:0.47 alpha:0.55 + 0.45 * heat];
    }
    NSRect r = NSMakeRect(x, y + 4, 8, 8);
    [dot setFill];
    [[NSBezierPath bezierPathWithOvalInRect:r] fill];
    x += 13;

    NSMutableString* text =
      [NSMutableString stringWithUTF8String:source_kind_name(kinds[i])];
    if (!a->connected) {
      [text appendString:@" --"];
    } else if (a->count == 0) {
      [text appendString:@" (silent)"];
    } else if ((a->status & 0xf0) == MIDI_CC) {
      [text appendFormat:@" cc%d=%d", a->data1, a->data2];
    } else {
      [text appendFormat:@" %s%d v%d",
            note_str(a->data1), a->data1 / 12 - 1, a->data2];
    }
    [text appendFormat:@" (%llu)", (unsigned long long)a->count];

    CGFloat w = [text sizeWithAttributes:@{NSFontAttributeName: font}].width;
    [self drawString:text
              inRect:NSMakeRect(x, y, w + 4, 16)
                font:font
               color:[NSColor colorWithWhite:a->connected ? 0.72 : 0.5 alpha:1]
            centered:NO];
    x += w + 22;
  }

  NSString* audio = [NSString stringWithFormat:@"\u266a %s",
                     snapshot.audio_device];
  CGFloat w = [audio sizeWithAttributes:@{NSFontAttributeName: font}].width;
  [self drawString:audio
            inRect:NSMakeRect(self.bounds.size.width - VIEW_PAD - w - 4, y,
                              w + 8, 16)
              font:font
             color:[NSColor colorWithSRGBRed:1.00 green:0.63
                                        blue:0.20 alpha:1]
          centered:NO];
}

- (void)drawRect:(NSRect)dirty {
  take_snapshot(&snapshot);

  [[NSColor colorWithSRGBRed:0.055 green:0.06 blue:0.075 alpha:1] setFill];
  NSRectFill(self.bounds);

  [self drawStatus];
  for (int i = 0; i < N_KEYS; i++) {
    [self drawKey:&KEYS[i] index:i];
  }
}

- (void)mouseDown:(NSEvent*)event {
  NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].label && NSPointInRect(p, [self rectForKey:&KEYS[i]])) {
      [self strikeKeyAtIndex:i];
      return;
    }
  }
}

@end

// ---------------------------------------------------------------------------
// CoreMIDI input
// ---------------------------------------------------------------------------

static MIDIClientRef midi_client;
static MIDIPortRef midi_in_port;
static MIDIEndpointRef connected_sources[64];
static int n_connected_sources = 0;

static void handle_midi_message(SourceKind kind, const Byte* data, int len) {
  if (len < 2) return;

  unsigned int status = data[0] & 0xf0;
  unsigned int note_in = data[1];
  unsigned int val = len > 2 ? data[2] : 0;

  LOCK();
  if (kind > 0 && kind < N_SOURCE_KINDS) {
    MidiActivity* activity = &midi_activity[kind];
    activity->count++;
    activity->last_ns = now();
    activity->status = status;
    activity->data1 = note_in;
    activity->data2 = val;
  }
  if (status == MIDI_CC) {
    handle_cc(note_in, val);
  } else if (status == MIDI_ON || status == MIDI_OFF) {
    unsigned int action = status;
    if (action == MIDI_ON && val == 0) {
      action = MIDI_OFF;
    }
    switch (kind) {
    case SRC_PIANO:  handle_piano(action, note_in, val); break;
    case SRC_FEET:   handle_feet(action, note_in, val); break;
    case SRC_BREATH: break;  // breath controller only sends CC
    }
  }
  UNLOCK();
}

static void read_midi(const MIDIPacketList* pktlist,
                      void* readProcRefCon,
                      void* srcConnRefCon) {
  SourceKind kind = (SourceKind)(intptr_t)srcConnRefCon;
  const MIDIPacket* packet = pktlist->packet;
  for (unsigned int i = 0; i < pktlist->numPackets; i++) {
    // A packet can hold several messages back to back.  Walk them by status
    // byte rather than assuming one message per packet.
    unsigned int at = 0;
    while (at < packet->length) {
      Byte status = packet->data[at];
      if (status < 0x80) break;  // running status; not worth supporting
      int len = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 2 : 3;
      if (status >= 0xf8) len = 1;  // clock/sensing, ignore
      if (at + len > packet->length) break;
      if (len == 3 || len == 2) {
        handle_midi_message(kind, &packet->data[at], len);
      }
      at += len;
    }
    packet = MIDIPacketNext(packet);
  }
}

static bool name_contains(CFStringRef name, const char* needle) {
  CFStringRef n = CFStringCreateWithCString(NULL, needle, kCFStringEncodingUTF8);
  bool found = CFStringFind(name, n, kCFCompareCaseInsensitive).location
    != kCFNotFound;
  CFRelease(n);
  return found;
}

// Mirrors the device matching in jammer.c: the mio is the foot pedals, the
// TE-Control is the breath controller, and the piano is either a known name or
// whatever other MIDI device happens to be plugged in.
static void connect_midi_sources() {
  for (int i = 0; i < n_connected_sources; i++) {
    MIDIPortDisconnectSource(midi_in_port, connected_sources[i]);
  }
  n_connected_sources = 0;

  LOCK();
  for (int i = 0; i < N_SOURCE_KINDS; i++) {
    midi_activity[i].connected = false;
  }
  UNLOCK();

  ItemCount n_sources = MIDIGetNumberOfSources();
  MIDIEndpointRef piano_candidate = 0;
  bool found_piano = false;

  printf("MIDI sources:\n");
  for (ItemCount i = 0; i < n_sources; i++) {
    MIDIEndpointRef src = MIDIGetSource(i);
    if (!src) continue;

    CFStringRef name = NULL;
    MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &name);
    if (!name) continue;

    char cname[256];
    CFStringGetCString(name, cname, sizeof(cname), kCFStringEncodingUTF8);
    printf("    %s\n", cname);

    SourceKind kind = (SourceKind)0;
    if (name_contains(name, "mio") || name_contains(name, "DTX")) {
      kind = SRC_FEET;
    } else if (name_contains(name, "Breath Controller")) {
      kind = SRC_BREATH;
    } else if (name_contains(name, "USB MIDI Interface") ||
               name_contains(name, "Piano") ||
               name_contains(name, "Roland")) {
      kind = SRC_PIANO;
      found_piano = true;
    } else if (!name_contains(name, "IAC") &&
               !name_contains(name, "jammer")) {
      // Prefer a device we recognise, but fall back to any unknown one.
      piano_candidate = src;
    }

    if (kind) {
      MIDIPortConnectSource(midi_in_port, src, (void*)(intptr_t)kind);
      connected_sources[n_connected_sources++] = src;
      LOCK();
      midi_activity[kind].connected = true;
      UNLOCK();
      printf("        -> %s\n",
             kind == SRC_FEET ? "feet"
             : kind == SRC_BREATH ? "breath controller" : "piano");
    }
    CFRelease(name);
  }

  if (!found_piano && piano_candidate) {
    MIDIPortConnectSource(midi_in_port, piano_candidate,
                          (void*)(intptr_t)SRC_PIANO);
    connected_sources[n_connected_sources++] = piano_candidate;
    LOCK();
    midi_activity[SRC_PIANO].connected = true;
    UNLOCK();
    printf("    (unrecognized device assumed to be the piano)\n");
  }
  printf("\n");
}

static void midi_notify(const MIDINotification* message, void* refCon) {
  if (message->messageID == kMIDIMsgSetupChanged) {
    // Something was plugged in or unplugged; re-scan.
    dispatch_async(dispatch_get_main_queue(), ^{
      connect_midi_sources();
    });
  }
}

static void setup_midi_input() {
  attempt(MIDIClientCreate(CFSTR("jammer"), midi_notify, NULL, &midi_client)
          == noErr ? 0 : -1, "creating MIDI client");
  attempt(MIDIInputPortCreate(midi_client, CFSTR("jammer input"),
                              read_midi, NULL, &midi_in_port)
          == noErr ? 0 : -1, "creating MIDI input port");
  connect_midi_sources();
}

// ---------------------------------------------------------------------------
// Tick thread
// ---------------------------------------------------------------------------

static void* tick_thread(void* unused) {
  uint64_t next = now();
  while (true) {
    LOCK();
    jml_tick();
    UNLOCK();

    next += TICK_MS * 1000000LL;
    uint64_t t = now();
    if (next > t) {
      struct timespec ts = {0, (long)(next - t)};
      nanosleep(&ts, NULL);
    } else {
      next = t;  // we fell behind; don't try to catch up in a burst
    }
  }
  return NULL;
}

static void start_tick_thread() {
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  // Timing matters more here than anywhere else in the app.
  struct sched_param param;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO);
  if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0) {
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  }

  if (pthread_create(&thread, &attr, tick_thread, NULL) != 0) {
    // Realtime priority needs privileges we may not have; plain thread is fine.
    pthread_attr_destroy(&attr);
    attempt(pthread_create(&thread, NULL, tick_thread, NULL) == 0 ? 0 : -1,
            "starting tick thread");
    return;
  }
  pthread_attr_destroy(&attr);
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

@class JammerAppDelegate;
static JammerAppDelegate* app_delegate;  // NSApp.delegate is weak; this owns it

@interface JammerAppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@property(strong) JammerView* view;
@property(strong) NSMenu* audioMenu;
- (void)rebuildAudioMenu;
@end

@implementation JammerAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)note {
  NSRect frame = NSMakeRect(0, 0, 1180, 480);
  self.window =
    [[NSWindow alloc] initWithContentRect:frame
                                styleMask:(NSWindowStyleMaskTitled |
                                           NSWindowStyleMaskClosable |
                                           NSWindowStyleMaskMiniaturizable |
                                           NSWindowStyleMaskResizable)
                                  backing:NSBackingStoreBuffered
                                    defer:NO];
  self.window.title = @"jammer";
  self.window.minSize = NSMakeSize(900, 380);
  [self.window center];

  self.view = [[JammerView alloc] initWithFrame:frame];
  self.window.contentView = self.view;
  [self.window makeFirstResponder:self.view];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  // A local monitor sees keys before the window's own handling, so tab and
  // escape reach us instead of moving focus or closing the window.
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                        handler:^NSEvent*(NSEvent* event) {
    if (event.modifierFlags & (NSEventModifierFlagCommand |
                               NSEventModifierFlagControl)) {
      return event;  // leave cmd-Q and friends alone
    }
    int index = [self.view indexForVirtualKeyCode:event.keyCode];
    if (index < 0) return event;
    [self.view strikeKeyAtIndex:index];
    return nil;
  }];

  [self rebuildAudioMenu];

  [NSTimer scheduledTimerWithTimeInterval:1.0 / 60
                                  repeats:YES
                                    block:^(NSTimer* t) {
    [self.view setNeedsDisplay:YES];
  }];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  return YES;
}

// Jammer only reads the keyboard while it's frontmost, so that's exactly how
// long it should hold onto the F-keys.  Switch away and they go back to
// controlling brightness and volume.
// Remembers the chosen output across launches, so a rig that's set up once
// stays set up.
- (void)chooseAudioDevice:(NSMenuItem*)item {
  const char* wanted = item.representedObject
    ? [item.representedObject UTF8String] : "default";
  LOCK();
  set_audio_device(wanted);
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setObject:item.representedObject
                                          forKey:@"audioDevice"];
  [self rebuildAudioMenu];
}

- (void)rebuildAudioMenu {
  NSMenu* menu = self.audioMenu;
  [menu removeAllItems];

  char names[32][256];
  int n = list_audio_devices(names, 32);
  for (int i = 0; i < n; i++) {
    NSString* name = @(names[i]);
    NSMenuItem* item = [menu addItemWithTitle:name
                                       action:@selector(chooseAudioDevice:)
                                keyEquivalent:@""];
    item.target = self;
    item.representedObject = name;
    item.state = (strcmp(names[i], audio_device) == 0)
      ? NSControlStateValueOn : NSControlStateValueOff;
  }
}

- (void)applicationDidBecomeActive:(NSNotification*)note {
  fkeys_grab();
}

- (void)applicationDidResignActive:(NSNotification*)note {
  fkeys_release();
}

- (void)applicationWillTerminate:(NSNotification*)note {
  fkeys_release();
  LOCK();
  all_notes_off();
  UNLOCK();
  stop_synth();
}

@end

static void setup_menu(JammerAppDelegate* delegate) {
  NSMenu* menubar = [NSMenu new];
  NSApp.mainMenu = menubar;

  NSMenuItem* app_item = [NSMenuItem new];
  [menubar addItem:app_item];
  NSMenu* app_menu = [NSMenu new];
  [app_menu addItemWithTitle:@"Quit jammer"
                      action:@selector(terminate:)
               keyEquivalent:@"q"];
  app_item.submenu = app_menu;

  NSMenuItem* audio_item = [NSMenuItem new];
  [menubar addItem:audio_item];
  delegate.audioMenu = [[NSMenu alloc] initWithTitle:@"Audio Output"];
  audio_item.submenu = delegate.audioMenu;
}

int main(int argc, const char** argv) {
  @autoreleasepool {
    // In an .app bundle the soundfont sits in Contents/Resources; running
    // straight out of the source directory it's alongside the binary.
    char bundle_dir[1024];
    NSString* resources = NSBundle.mainBundle.resourcePath;
    snprintf(bundle_dir, sizeof(bundle_dir), "%s",
             resources ? resources.UTF8String : ".");

    char soundfont[1024];
    if (!find_soundfont(bundle_dir, soundfont, sizeof(soundfont))) {
      printf("Couldn't find FluidR3_GM.sf2.\n"
             "Run `make soundfont`, or set $JAMMER_SOUNDFONT.\n");
      return 1;
    }
    // An explicit choice beats the system default, which on a laptop is the
    // built-in speakers -- rarely what you want on stage.
    const char* device = getenv("JAMMER_AUDIO_DEVICE");
    if (!device) {
      NSString* saved =
        [NSUserDefaults.standardUserDefaults stringForKey:@"audioDevice"];
      if (saved) device = saved.UTF8String;
    }
    start_synth(soundfont, device ? device : "default");

    setup_midi_input();

    LOCK();
    jml_setup();
    UNLOCK();

    start_tick_thread();
    fkeys_install_restore_handlers();

    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    // NSApp.delegate is a weak reference, so the delegate needs an owner that
    // outlives this scope or ARC will free it and the callbacks stop firing.
    app_delegate = [JammerAppDelegate new];
    setup_menu(app_delegate);
    NSApp.delegate = app_delegate;
    [NSApp run];
  }
  return 0;
}
