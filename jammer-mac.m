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
#include "whistle.h"
#include "speechwords.h"
#include "whistleinput.h"
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

// Wants the lock, so here rather than with the other includes.
#include "speech.h"
#include "numtrain.h"
#include "numheard.h"

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
  bool selected[N_KEYS];
  int selected_endpoint;
  int root_note;
  int octave_delta;
  int volume_delta;
  int air;
  int bpm;
  bool on[N_ENDPOINTS];
  bool pans[N_ENDPOINTS];  // channel swapped (F2)
  MidiActivity midi[N_SOURCE_KINDS];
  uint64_t now_ns;
  char audio_device[256];
  double gain;

  // The whistle bass, which is an instrument here but not an endpoint.
  bool whistle_available;
  bool whistle_on;
  bool whistle_selected;
  bool whistle_dead[N_KEYS];
  bool drone_dead[N_KEYS];
  int drone_voice[N_KEYS];  // DRONE_VOICES index each key picks, or -1
  int whistle_voice;
  int whistle_octave;
  int whistle_volume;
  double whistle_gain;
  float whistle_level;
  float whistle_freq;
  bool whistle_voiced;
  int whistle_dropouts;
  double whistle_latency_ms;
  char whistle_device[WHISTLE_DEVICE_NAME_MAX];
  char whistle_error[256];

  // Speech recognition (F8).
  char speech_state[160];
  char speech_heard[200];
  char speech_action[64];
  float speech_level;  // peak, held and decaying here
  double speech_gate_db;
  bool speech_gate_open;
} Snapshot;

static void take_snapshot(Snapshot* s) {
  LOCK();
  for (int i = 0; i < N_KEYS; i++) {
    s->lit[i] = KEYS[i].label ? key_is_lit(&KEYS[i]) : false;
    s->selected[i] = key_is_selected_endpoint(&KEYS[i]);
    s->whistle_dead[i] = whistle_key_is_dead(&KEYS[i]);
    s->drone_dead[i] = drone_key_is_dead(&KEYS[i]);
    s->drone_voice[i] = drone_voice_on_key(&KEYS[i]);
  }
  int sel = c->selected_endpoint;
  s->selected_endpoint = sel;
  s->root_note = root_note;
  s->octave_delta = c->octave_deltas[sel];
  s->volume_delta = c->volume_deltas[sel];
  s->air = (int)air;
  s->bpm = current_beat_ns > 0 ? (int)(60 * NS_PER_SEC / current_beat_ns) : 0;
  for (int i = 0; i < N_ENDPOINTS; i++) {
    s->on[i] = c->on[i];
    s->pans[i] = c->pans[i];
  }
  memcpy(s->midi, midi_activity, sizeof(s->midi));
  s->now_ns = now();
  s->gain = synth_gain;
  snprintf(s->audio_device, sizeof(s->audio_device), "%s", audio_device);

  s->whistle_available = whistle_available;
  s->whistle_on = whistle_on;
  s->whistle_selected = whistle_selected;
  s->whistle_voice = whistle_voice;
  s->whistle_octave = whistle_octave;
  s->whistle_volume = whistle_volume;
  s->whistle_gain = whistle_gain;
  // Peak-hold for a second and a half.  The audio thread reports the loudest
  // it heard since the last read, which at 60Hz is a 16ms window: read raw it
  // flickers far too fast to set the full-blow knob against, and it drops to
  // zero between notes.  Holding the highest recent value is what makes it a
  // number you can whistle at and then go and type in.
  float level =
    atomic_exchange_explicit(&whistle_meter_level, 0, memory_order_relaxed)
      / 10000.0f;
  static float peak_hold;
  static uint64_t peak_hold_ns;
  uint64_t now_ns = now();
  if (level >= peak_hold || now_ns - peak_hold_ns > 1500000000ULL) {
    peak_hold = level;
    peak_hold_ns = now_ns;
  }
  s->whistle_level = peak_hold;
  s->whistle_freq =
    atomic_load_explicit(&whistle_meter_freq, memory_order_relaxed) / 100.0f;
  s->whistle_voiced =
    atomic_load_explicit(&whistle_meter_voiced, memory_order_relaxed) != 0;
  s->whistle_dropouts =
    atomic_load_explicit(&whistle_dropouts, memory_order_relaxed);
  s->whistle_latency_ms = whistle_latency_ms;
  snprintf(s->whistle_device, sizeof(s->whistle_device), "%s",
           whistle_input_name);
  snprintf(s->whistle_error, sizeof(s->whistle_error), "%s",
           whistle_input_error);

  snprintf(s->speech_state, sizeof(s->speech_state), "%s", speech_state);
  snprintf(s->speech_heard, sizeof(s->speech_heard), "%s", speech_heard_text);
  snprintf(s->speech_action, sizeof(s->speech_action), "%s",
           speech_last_action);
  // Held and let fall, since it's refilled 20 times a second and drawn 60.
  s->speech_level = fmaxf(s->speech_level * 0.93f, speech_level);
  speech_level = 0;
  s->speech_gate_db = speech_gate_db;
  s->speech_gate_open = speech_gate_open;
  UNLOCK();
}

// ---------------------------------------------------------------------------
// The keyboard view
// ---------------------------------------------------------------------------

// Sized so the whole status block stays readable from a few feet back with
// the window maximized on a laptop screen.
#define STATUS_HEIGHT 242.0
#define KEY_GAP 4.0
#define VIEW_PAD 14.0

// The endpoint the modifier keys act on.  Deliberately not one of the group
// colours, since it has to read on top of any of them.
#define SELECTED_COLOR [NSColor colorWithSRGBRed:1.00 green:0.93 blue:0.30 \
                                            alpha:1]

@interface JammerView : NSView {
  Snapshot snapshot;
  // Momentary keys have no lasting state, so flash them briefly when struck.
  NSTimeInterval flash_time[N_KEYS];
  // Where the key signature is drawn, so a click there can open the picker.
  NSRect root_note_rect;
}
- (void)strikeKeyAtIndex:(int)index selecting:(BOOL)selecting;
- (void)flashKeyAtIndex:(int)index;
- (int)indexForVirtualKeyCode:(int)vk;
@end

static NSColor* group_color(KeyGroup group) {
  switch (group) {
  case GROUP_TOGGLE:   return [NSColor colorWithSRGBRed:0.24 green:0.85
                                                   blue:0.47 alpha:1];
  case GROUP_VOICE:    return [NSColor colorWithSRGBRed:1.00 green:0.63
                                                   blue:0.20 alpha:1];
  case GROUP_MODIFIER: return [NSColor colorWithSRGBRed:0.74 green:0.50
                                                   blue:1.00 alpha:1];
  case GROUP_GLOBAL:   return [NSColor colorWithSRGBRed:0.25 green:0.82
                                                   blue:0.85 alpha:1];
  // Its own colour because it is its own synthesis engine: nothing it does
  // goes through fluidsynth, so it shouldn't read as one of the endpoints.
  case GROUP_WHISTLE:  return [NSColor colorWithSRGBRed:1.00 green:0.42
                                                   blue:0.66 alpha:1];
  case GROUP_NONE:     break;
  }
  return [NSColor colorWithSRGBRed:0.35 green:0.36 blue:0.40 alpha:1];
}

// Text on the keys scales with the keys, so maximizing the window actually
// makes things easier to read rather than just more spread out.
static CGFloat clamped(CGFloat value, CGFloat lo, CGFloat hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// "C " -> "C", since note_str() pads to two columns for fixed-width output.
static NSString* note_name(int note) {
  return [@(note_str(note)) stringByTrimmingCharactersInSet:
            NSCharacterSet.whitespaceCharacterSet];
}

// Fonts are looked up once per size and weight and kept.  macOS has, rarely,
// handed back nil for a system font mid-performance, and a nil font in an
// attributes dictionary throws, which AppKit treats as fatal inside drawRect.
// So fall back to plainer fonts, and cache whatever works so it can't
// disappear later.  May still return nil; the drawing code copes.
static NSFont* jammer_font(CGFloat size, NSFontWeight weight, bool mono) {
  static NSMutableDictionary<NSString*, NSFont*>* cache;
  if (!cache) cache = [NSMutableDictionary dictionary];
  NSString* cache_key =
    [NSString stringWithFormat:@"%d %.2f %.3f", mono, size, weight];
  NSFont* font = cache[cache_key];
  if (font) return font;

  if (mono) font = [NSFont monospacedSystemFontOfSize:size weight:weight];
  if (!font) font = [NSFont systemFontOfSize:size weight:weight];
  if (!font) font = [NSFont systemFontOfSize:size];
  if (!font) font = [NSFont userFontOfSize:size];
  if (font) cache[cache_key] = font;
  return font;
}

static NSFont* mono_font(CGFloat size, NSFontWeight weight) {
  return jammer_font(size, weight, true);
}

static NSFont* ui_font(CGFloat size, NSFontWeight weight) {
  return jammer_font(size, weight, false);
}

static CGFloat text_width(NSString* s, NSFont* font) {
  if (!s || !font) return 0;
  return [s sizeWithAttributes:@{NSFontAttributeName: font}].width;
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

// Shift over an endpoint's on/off key selects that endpoint for the modifier
// keys instead of toggling it, which is what the number row used to do.
- (void)strikeKeyAtIndex:(int)index selecting:(BOOL)selecting {
  if (index < 0 || index >= N_KEYS) return;
  const Key* key = &KEYS[index];
  if (!key->label) return;

  LOCK();
  strike_key_locked(key, selecting);
  UNLOCK();
  [self flashKeyAtIndex:index];
}

- (void)flashKeyAtIndex:(int)index {
  flash_time[index] = [NSDate timeIntervalSinceReferenceDate];
  [self setNeedsDisplay:YES];
}

// Geometry: the layout is N_LAYOUT_COLS x N_LAYOUT_ROWS key units, and a key
// unit is square.  Whichever of width and height runs out first sets the
// scale, and the keyboard is centred in what's left, so going full screen
// makes the keys bigger rather than stretching them out of shape.
- (NSRect)keyboardRect {
  NSRect b = self.bounds;
  double avail_w = b.size.width - 2 * VIEW_PAD;
  double avail_h = b.size.height - STATUS_HEIGHT - VIEW_PAD;
  double unit = MIN(avail_w / N_LAYOUT_COLS, avail_h / N_LAYOUT_ROWS);
  double w = unit * N_LAYOUT_COLS;
  double h = unit * N_LAYOUT_ROWS;
  return NSMakeRect(VIEW_PAD + (avail_w - w) / 2,
                    STATUS_HEIGHT + (avail_h - h) / 2, w, h);
}

- (NSRect)rectForKey:(const Key*)key {
  NSRect kb = [self keyboardRect];
  double unit = kb.size.width / N_LAYOUT_COLS;
  double h = key->h > 0 ? key->h : 1.0;
  return NSMakeRect(kb.origin.x + key->x * unit,
                    kb.origin.y + key->row * unit,
                    key->w * unit - KEY_GAP,
                    h * unit - KEY_GAP);
}

- (void)drawString:(NSString*)s
            inRect:(NSRect)rect
              font:(NSFont*)font
             color:(NSColor*)color
           centered:(BOOL)centered {
  if (!s || !font || !color) return;  // a blank label beats a crash on stage
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

// Centres possibly-multi-line text in both directions.
- (void)drawCentered:(NSString*)s
              inRect:(NSRect)rect
                font:(NSFont*)font
               color:(NSColor*)color {
  if (!s || !font) return;
  NSInteger lines = [[s componentsSeparatedByString:@"\n"] count];
  CGFloat text_h = lines * font.pointSize * 1.22;
  [self drawString:s
            inRect:NSMakeRect(rect.origin.x,
                              rect.origin.y + (rect.size.height - text_h) / 2,
                              rect.size.width, text_h + 2)
              font:font
             color:color
          centered:YES];
}

- (void)drawKey:(const Key*)key index:(int)i {
  NSRect r = [self rectForKey:key];
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:r
                                                       xRadius:6 yRadius:6];

  NSColor* color = group_color(key->group);
  bool lit = snapshot.lit[i];
  // A key with no label at all is filler; a voice key whose drum label is
  // empty does nothing while the drum is selected.  Both draw as dead keys.
  // Not while the whistle is selected, though: then the voice keys are its,
  // whichever endpoint was selected before it.
  bool blank_on_drum = snapshot.selected_endpoint == ENDPOINT_DRUM &&
                       !snapshot.whistle_selected &&
                       key->drum_label && key->drum_label[0] == '\0';
  bool unbound = (key->label == NULL) || blank_on_drum ||
                 snapshot.whistle_dead[i] || snapshot.drone_dead[i];
  bool selected = snapshot.selected[i];

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

  // The endpoint the modifier keys currently act on gets its own outline,
  // whether or not that endpoint is switched on.
  if (selected) {
    [SELECTED_COLOR setStroke];
    path.lineWidth = 4.0;
  } else {
    [[color colorWithAlphaComponent:unbound ? 0.25 : (lit ? 1.0 : 0.5)]
      setStroke];
    path.lineWidth = 1.0;
  }
  [path stroke];

  // Key cap letter, small, top left.
  CGFloat cap_size = clamped(r.size.height * 0.145, 9, 17);
  NSColor* cap_color = lit
    ? [NSColor colorWithWhite:0.08 alpha:0.75]
    : [NSColor colorWithWhite:unbound ? 0.35 : 0.62 alpha:1];
  [self drawString:@(key->cap)
            inRect:NSMakeRect(r.origin.x + 6, r.origin.y + 3,
                              r.size.width - 10, cap_size + 5)
              font:mono_font(cap_size, NSFontWeightBold)
             color:cap_color
          centered:NO];

  if (unbound) return;

  // An endpoint that's been channel swapped carries F2's tag in the corner,
  // since otherwise the only way to tell is to select it and look at F2.
  if (key->lit == LIT_EP_ON && snapshot.pans[key->arg]) {
    NSFont* tag_font = mono_font(cap_size * 0.85, NSFontWeightBold);
    NSString* tag = @"CH";
    CGFloat tag_w = text_width(tag, tag_font) + 8;
    CGFloat tag_h = cap_size + 2;
    NSRect tag_rect = NSMakeRect(NSMaxX(r) - tag_w - 5, r.origin.y + 4,
                                 tag_w, tag_h);
    [group_color(GROUP_MODIFIER) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:tag_rect xRadius:3 yRadius:3]
      fill];
    [self drawCentered:tag
                inRect:tag_rect
                  font:tag_font
                 color:[NSColor colorWithWhite:0.06 alpha:1]];
  }

  // What it does, under the cap letter.
  const char* label = key->label;
  const char* shortname = key->shortname;
  if (snapshot.selected_endpoint == ENDPOINT_DRUM && key->drum_label) {
    label = key->drum_label;
  }
  // So do the drones' pads, and like the whistle's they have no paper tab.
  if (snapshot.drone_voice[i] >= 0) {
    label = DRONE_VOICES[snapshot.drone_voice[i]].label;
    shortname = NULL;
  }
  // The whistle's ten voices take over the voice keys while it is selected,
  // the same way the drum kits do.  They have no paper tab on the real
  // keyboard, so the name gets the whole key.
  int whistle_voice_index = -1;
  if (snapshot.whistle_selected && key->group == GROUP_VOICE) {
    whistle_voice_index = whistle_voice_for_note(key->note);
    if (whistle_voice_index >= 0) {
      label = WHISTLE_VOICES[whistle_voice_index].label;
      shortname = NULL;
    }
  }
  NSString* text = @(label);  // embedded \n in the table splits lines

  // Keys that carry a running value show it instead of a static label -- the
  // whistle's own when it is what they are moving.
  int octave_delta = snapshot.whistle_selected ? snapshot.whistle_octave
                                               : snapshot.octave_delta;
  int volume_delta = snapshot.whistle_selected
    ? snapshot.whistle_volume - WHISTLE_VOLUME_DEFAULT
    : snapshot.volume_delta;
  if (key->lit == LIT_OCTAVE && octave_delta != 0) {
    text = [NSString stringWithFormat:@"OCT\n%+d", octave_delta];
  } else if (key->lit == LIT_VOLUME && volume_delta != 0) {
    text = [NSString stringWithFormat:@"VOL\n%+d", volume_delta];
  }

  NSColor* text_color = lit ? [NSColor colorWithWhite:0.06 alpha:1]
                            : [NSColor colorWithWhite:0.88 alpha:1];

  CGFloat cap_room = cap_size + 6;
  NSRect body = NSMakeRect(r.origin.x + 2, r.origin.y + cap_room,
                           r.size.width - 4, r.size.height - cap_room - 4);
  if (body.size.height < 10) return;

  if (!shortname) {
    [self drawCentered:text
                inRect:body
                  font:ui_font(clamped(r.size.height * 0.19, 9, 24),
                                NSFontWeightSemibold)
                 color:text_color];
    return;
  }

  // The abbreviation written on the paper tab stuck to the real keyboard goes
  // on top, big, so the screen and the keyboard read the same; the spelled-out
  // name fills what's left.
  CGFloat short_size = clamped(r.size.height * 0.27, 11, 32);
  CGFloat short_h = MIN(short_size * 1.25, body.size.height * 0.55);
  [self drawCentered:@(shortname)
              inRect:NSMakeRect(body.origin.x, body.origin.y,
                                body.size.width, short_h)
                font:ui_font(short_size, NSFontWeightHeavy)
               color:text_color];
  [self drawCentered:text
              inRect:NSMakeRect(body.origin.x, body.origin.y + short_h,
                                body.size.width, body.size.height - short_h)
                font:ui_font(clamped(r.size.height * 0.145, 8.5, 17),
                              NSFontWeightSemibold)
               color:[text_color colorWithAlphaComponent:0.82]];
}

// ---------------------------------------------------------------------------
// Status block
// ---------------------------------------------------------------------------

- (void)drawStatus {
  NSRect b = self.bounds;
  CGFloat width = b.size.width - 2 * VIEW_PAD;

  // Line 1: the key, which is a button -- click it to pick another -- then
  // the mode, tempo and air.
  NSFont* note_font = mono_font(30, NSFontWeightBold);
  NSString* note_text = [NSString stringWithFormat:@"%@ ▾",
                         note_name(snapshot.root_note)];
  // Sized for the widest name, so what follows doesn't move with the key.
  CGFloat note_w = text_width(@"C# ▾", note_font);

  root_note_rect = NSMakeRect(VIEW_PAD, 8, note_w + 26, 44);
  NSBezierPath* pill = [NSBezierPath bezierPathWithRoundedRect:root_note_rect
                                                      xRadius:8 yRadius:8];
  [[NSColor colorWithSRGBRed:0.17 green:0.18 blue:0.23 alpha:1] setFill];
  [pill fill];
  [[NSColor colorWithWhite:0.45 alpha:1] setStroke];
  pill.lineWidth = 1.5;
  [pill stroke];
  [self drawCentered:note_text
              inRect:root_note_rect
                font:note_font
               color:[NSColor colorWithWhite:0.97 alpha:1]];

  // Every field in the status rows keeps a fixed number of columns, blank
  // when there's nothing to show, so a value changing never shoves the rest
  // of its row sideways.
  NSMutableString* line = [NSMutableString string];
  if (snapshot.bpm > 0) {
    [line appendFormat:@"%3d bpm   ", snapshot.bpm];
  } else {
    [line appendString:@"          "];
  }
  [line appendFormat:@"air %3d", snapshot.air];
  CGFloat rest_x = NSMaxX(root_note_rect) + 18;
  [self drawString:line
            inRect:NSMakeRect(rest_x, 14, b.size.width - rest_x - VIEW_PAD, 36)
              font:mono_font(25, NSFontWeightMedium)
             color:[NSColor colorWithWhite:0.95 alpha:1]
          centered:NO];

  // Line 2: which endpoints are making sound right now, each in a place of
  // its own.
  NSMutableString* playing = [NSMutableString stringWithString:@"on: "];
  bool any = snapshot.whistle_on;
  for (int i = 0; i < N_ENDPOINTS; i++) any = any || snapshot.on[i];
  for (int i = 0; i <= N_ENDPOINTS; i++) {
    const char* name = i < N_ENDPOINTS ? ENDPOINT_NAMES[i] : "Whistle";
    bool on = i < N_ENDPOINTS ? snapshot.on[i] : snapshot.whistle_on;
    if (!any && i == 0) {
      [playing appendFormat:@"%-*s", (int)strlen(name), "-"];
    } else {
      [playing appendFormat:@"%-*s", (int)strlen(name), on ? name : ""];
    }
    if (i < N_ENDPOINTS) [playing appendString:@"  "];
  }

  [self drawString:playing
            inRect:NSMakeRect(VIEW_PAD, 58, width, 26)
              font:mono_font(19, NSFontWeightMedium)
             color:[NSColor colorWithSRGBRed:0.24 green:0.85
                                        blue:0.47 alpha:1]
          centered:NO];

  [self drawMidiRow];
  [self drawAudioRow];
  [self drawWhistleRow];
  [self drawSpeechRow];
}

// One entry per MIDI source, with a dot that lights when something arrives and
// the last message alongside it.
- (void)drawMidiRow {
  NSFont* font = mono_font(17, NSFontWeightRegular);
  CGFloat x = VIEW_PAD;
  CGFloat y = 90;

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
    [dot setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x, y + 6, 12, 12)] fill];
    x += 18;

    char value[32];
    if (!a->connected) {
      snprintf(value, sizeof(value), "--");
    } else if (a->count == 0) {
      snprintf(value, sizeof(value), "(silent)");
    } else if ((a->status & 0xf0) == MIDI_CC) {
      snprintf(value, sizeof(value), "cc%d=%d", a->data1, a->data2);
    } else {
      snprintf(value, sizeof(value), "%s%d v%d",
               note_str(a->data1), a->data1 / 12 - 1, a->data2);
    }
    // Wide enough for "cc127=127" and "C#-1 v127".
    NSString* text = [NSString stringWithFormat:@"%-7s %-9s",
                      source_kind_name(kinds[i]), value];

    CGFloat w = text_width(text, font);
    [self drawString:text
              inRect:NSMakeRect(x, y, w + 6, 24)
                font:font
               color:[NSColor colorWithWhite:a->connected ? 0.72 : 0.5 alpha:1]
            centered:NO];
    x += w + 30;
  }
}

// Where the sound is going, and how loud, on a row of its own -- both are set
// from the Audio Output menu and both are worth checking before a gig.
- (void)drawAudioRow {
  NSFont* font = mono_font(17, NSFontWeightRegular);
  CGFloat y = 120;

  NSString* audio = [NSString stringWithFormat:@"♪ %s   vol %3d%%",
                     snapshot.audio_device, (int)(snapshot.gain * 100 + 0.5)];
  [self drawString:audio
            inRect:NSMakeRect(VIEW_PAD, y,
                              self.bounds.size.width - 2 * VIEW_PAD, 24)
              font:font
             color:[NSColor colorWithSRGBRed:1.00 green:0.63
                                        blue:0.20 alpha:1]
          centered:NO];
}

// The whistle bass: whether it can run at all, what it's listening to, and
// what it's hearing.  A microphone that isn't working looks exactly like "the
// whistle is broken" otherwise, and a level meter is how you set the gate and
// the full-blow level from the menu without guessing.
- (void)drawWhistleRow {
  NSFont* font = mono_font(17, NSFontWeightRegular);
  CGFloat y = 150;
  NSColor* whistle_color = [NSColor colorWithSRGBRed:1.00 green:0.42
                                                blue:0.66 alpha:1];

  if (!snapshot.whistle_available) {
    NSString* text = snapshot.whistle_error[0]
      ? [NSString stringWithFormat:@"whistle: %s", snapshot.whistle_error]
      : @"whistle: no microphone";
    [self drawString:text
              inRect:NSMakeRect(VIEW_PAD, y,
                                self.bounds.size.width - 2 * VIEW_PAD, 24)
                font:font
               color:[NSColor colorWithSRGBRed:0.80 green:0.45
                                          blue:0.45 alpha:1]
            centered:NO];
    return;
  }

  CGFloat x = VIEW_PAD;

  // A dot that fills while a note is actually being detected, so you can see
  // the gate opening and closing without listening for it.
  NSColor* dot = snapshot.whistle_voiced
    ? whistle_color
    : [NSColor colorWithWhite:snapshot.whistle_on ? 0.32 : 0.22 alpha:1];
  [dot setFill];
  [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x, y + 6, 12, 12)] fill];
  x += 18;

  NSMutableString* text = [NSMutableString stringWithString:@"whistle "];
  [text appendFormat:@"%s", snapshot.whistle_on ? "on " : "off"];
  // Wide enough for "eight-oh-eight".
  [text appendFormat:@"  %-14s",
        WHISTLE_VOICES[snapshot.whistle_voice].preset];
  char octave[16] = "";
  if (snapshot.whistle_octave != 0) {
    snprintf(octave, sizeof(octave), "%+doct", snapshot.whistle_octave);
  }
  [text appendFormat:@" %-5s", octave];
  // The level the detector heard while a note was sounding, which is the
  // number the full-blow knob is set against.
  [text appendFormat:@"   lvl %5.3f", snapshot.whistle_level];
  char heard[16] = "";
  if (snapshot.whistle_voiced && snapshot.whistle_freq > 0) {
    int midi = whistle_hz_to_note(snapshot.whistle_freq);
    snprintf(heard, sizeof(heard), "%s%d", note_str(midi), midi / 12 - 1);
  }
  [text appendFormat:@"   %-4s", heard];
  [text appendFormat:@"   ♪ %s  vol %3d%%", snapshot.whistle_device,
        (int)(snapshot.whistle_gain * 100 + 0.5)];
  // What the whistle adds on top of fluidsynth's own output latency.
  [text appendFormat:@"  +%5.1fms", snapshot.whistle_latency_ms];
  if (snapshot.whistle_dropouts > 0) {
    [text appendFormat:@"   %d dropouts", snapshot.whistle_dropouts];
  }

  [self drawString:text
            inRect:NSMakeRect(x, y, self.bounds.size.width - x - VIEW_PAD, 24)
              font:font
             color:[whistle_color colorWithAlphaComponent:
                      snapshot.whistle_on ? 1.0 : 0.6]
          centered:NO];
}

// Speech recognition (F8): whether it's listening, how
// loud what it's listening to is, what words it's hearing, and what it last
// did about them.  Without this a recognizer that isn't working looks the
// same as one hearing nothing it knows.
- (void)drawSpeechRow {
  NSFont* font = mono_font(17, NSFontWeightRegular);
  CGFloat y = 180;
  NSColor* color = [NSColor colorWithSRGBRed:0.45 green:0.78
                                        blue:1.00 alpha:1];
  bool listening = strncmp(snapshot.speech_state, "listening", 9) == 0;
  CGFloat x = VIEW_PAD;

  NSString* state = [NSString stringWithFormat:@"speech: %s",
                     snapshot.speech_state];
  NSColor* state_color = listening ? color
    : strncmp(snapshot.speech_state, "off", 3) == 0
      ? [color colorWithAlphaComponent:0.5]
      : [NSColor colorWithSRGBRed:0.80 green:0.45 blue:0.45 alpha:1];
  [self drawString:state
            inRect:NSMakeRect(x, y, self.bounds.size.width - x - VIEW_PAD, 24)
              font:font
             color:state_color
          centered:NO];
  if (!listening) return;

  // What it's hearing gets a row of its own, so the state above -- whose
  // length changes as the dictionary loads and numbers are learned -- can't
  // push it around.
  y += 30;
  x = VIEW_PAD + 18;

  // The meter: -60dBFS to 0, which is where speech into a vocal mic lives,
  // with the gate's threshold marked on it.  Bright while the gate is open,
  // dim when what's coming in is being kept from the recognizer.
  double db = snapshot.speech_level > 0
    ? 20 * log10(snapshot.speech_level) : -99;
  double fill = fmin(1, fmax(0, (db + 60) / 60));
  CGFloat meter_w = 120;
  [[NSColor colorWithWhite:0.22 alpha:1] setFill];
  NSRectFill(NSMakeRect(x, y + 7, meter_w, 10));
  [[color colorWithAlphaComponent:snapshot.speech_gate_open ? 1.0 : 0.35]
    setFill];
  NSRectFill(NSMakeRect(x, y + 7, meter_w * fill, 10));
  double gate = fmin(1, fmax(0, (snapshot.speech_gate_db + 60) / 60));
  [[NSColor whiteColor] setFill];
  NSRectFill(NSMakeRect(x + meter_w * gate - 1, y + 3, 2, 18));
  x += meter_w + 8;
  NSString* level = db > -99 ? [NSString stringWithFormat:@"%4.0f dB", db]
                             : @"  silent";
  [self drawString:level
            inRect:NSMakeRect(x, y, 90, 24)
              font:font
             color:color
          centered:NO];
  x += 96;

  // The last action in a slot of its own ahead of the words, which run on
  // to the end of the row.
  NSString* action = @(snapshot.speech_action);
  if (action.length > 24) {
    action = [[action substringToIndex:23] stringByAppendingString:@"…"];
  }
  NSString* text = [NSString stringWithFormat:@"→ %@%*s   heard: \"%s\"",
                    action, (int)(24 - action.length), "",
                    snapshot.speech_heard];
  [self drawString:text
            inRect:NSMakeRect(x, y, self.bounds.size.width - x - VIEW_PAD, 24)
              font:font
             color:color
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

// ---------------------------------------------------------------------------
// Picking the key
// ---------------------------------------------------------------------------

- (void)chooseRootNote:(NSMenuItem*)item {
  LOCK();
  change_key((int)item.tag);
  UNLOCK();
  [self setNeedsDisplay:YES];
}

- (void)showRootNotePicker {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Key"];
  menu.font = mono_font(16, NSFontWeightMedium);
  for (int i = 0; i < 12; i++) {
    int note = to_root(i);
    NSMenuItem* item = [menu addItemWithTitle:note_name(note)
                                       action:@selector(chooseRootNote:)
                                keyEquivalent:@""];
    item.target = self;
    item.tag = note;
    item.state = (note == snapshot.root_note) ? NSControlStateValueOn
                                              : NSControlStateValueOff;
  }
  [menu popUpMenuPositioningItem:nil
                      atLocation:NSMakePoint(NSMinX(root_note_rect),
                                             NSMaxY(root_note_rect) + 4)
                          inView:self];
}

- (void)mouseDown:(NSEvent*)event {
  NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
  if (NSPointInRect(p, root_note_rect)) {
    [self showRootNotePicker];
    return;
  }
  BOOL selecting = (event.modifierFlags & NSEventModifierFlagShift) != 0;
  for (int i = 0; i < N_KEYS; i++) {
    if (KEYS[i].label && NSPointInRect(p, [self rectForKey:&KEYS[i]])) {
      [self strikeKeyAtIndex:i selecting:selecting];
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

// "press ..." said with speech recognition on (speech.h) strikes the key
// itself, on the beat; this is just the flash that shows it happened.
static __weak JammerView* spoken_view;
static void flash_from_speech(int key) {
  dispatch_async(dispatch_get_main_queue(), ^{
    [spoken_view flashKeyAtIndex:key];
  });
}

@interface JammerAppDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@property(strong) JammerView* view;
@property(strong) NSMenu* audioMenu;
@property(strong) NSMenuItem* volumeItem;
@property(strong) NSSlider* volumeSlider;
@property(strong) NSMenu* whistleMenu;
@property(strong) NSMenu* speechMenu;
@property(strong) NSTextField* speechGateCaption;
@property(strong) NtWindowController* numberSamples;
@property(strong) NhReviewController* numberReview;
@property(strong) NSMenuItem* whistleVolumeItem;
@property(strong) NSSlider* whistleVolumeSlider;
- (void)rebuildAudioMenu;
- (void)rebuildWhistleMenu;
@end

@implementation JammerAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)note {
  NSRect frame = NSMakeRect(0, 0, 1280, 660);
  self.window =
    [[NSWindow alloc] initWithContentRect:frame
                                styleMask:(NSWindowStyleMaskTitled |
                                           NSWindowStyleMaskClosable |
                                           NSWindowStyleMaskMiniaturizable |
                                           NSWindowStyleMaskResizable)
                                  backing:NSBackingStoreBuffered
                                    defer:NO];
  self.window.title = @"jammer";
  self.window.minSize = NSMakeSize(1000, 540);
  [self.window center];

  self.view = [[JammerView alloc] initWithFrame:frame];
  spoken_view = self.view;
  speech_flash = flash_from_speech;
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
    // The review window answers with number keys of its own.
    if (event.window && event.window == self.numberReview.window) {
      return event;
    }
    int index = [self.view indexForVirtualKeyCode:event.keyCode];
    if (index < 0) return event;
    BOOL selecting = (event.modifierFlags & NSEventModifierFlagShift) != 0;
    [self.view strikeKeyAtIndex:index selecting:selecting];
    return nil;
  }];

  [self rebuildAudioMenu];
  [self rebuildWhistleMenu];
  [self buildSpeechMenu];

  [NSTimer scheduledTimerWithTimeInterval:1.0 / 60
                                  repeats:YES
                                    block:^(NSTimer* t) {
    [self.view setNeedsDisplay:YES];
  }];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  return YES;
}

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

// One volume for the whole rig, on top of the per-voice levels -- the knob to
// reach for when the room or the PA wants more, without retuning anything.
- (void)volumeChanged:(NSSlider*)slider {
  LOCK();
  set_synth_gain(slider.doubleValue);
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setDouble:synth_gain
                                          forKey:@"synthGain"];
  [self.view setNeedsDisplay:YES];
}

- (NSMenuItem*)volumeMenuItem {
  if (self.volumeItem) return self.volumeItem;

  NSView* holder = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 54)];

  NSTextField* caption = [NSTextField labelWithString:@"Global volume"];
  caption.font = [NSFont menuFontOfSize:0];
  caption.textColor = NSColor.labelColor;
  caption.frame = NSMakeRect(20, 30, 200, 18);
  [holder addSubview:caption];

  self.volumeSlider = [NSSlider sliderWithValue:synth_gain
                                       minValue:0
                                       maxValue:MAX_SYNTH_GAIN
                                         target:self
                                         action:@selector(volumeChanged:)];
  self.volumeSlider.frame = NSMakeRect(20, 6, 220, 20);
  self.volumeSlider.continuous = YES;
  [holder addSubview:self.volumeSlider];

  self.volumeItem = [[NSMenuItem alloc] init];
  self.volumeItem.view = holder;
  return self.volumeItem;
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

  [menu addItem:[NSMenuItem separatorItem]];
  self.volumeSlider.doubleValue = synth_gain;  // in case it changed elsewhere
  [menu addItem:[self volumeMenuItem]];
}

// ---------------------------------------------------------------------------
// The Whistle menu
//
// Everything here describes the microphone and the room rather than the tune:
// which input to listen to, how far above the room noise a note has to stand,
// what counts as blowing full tilt, and what range to believe.  You set them
// once against a rig and then leave them alone, which is why none of them is
// on a key.
// ---------------------------------------------------------------------------

- (void)chooseWhistleDevice:(NSMenuItem*)item {
  NSString* uid = item.representedObject ?: @"";
  [NSUserDefaults.standardUserDefaults setObject:uid forKey:@"whistleInput"];
  // Deliberately not under the lock.  Opening a device waits -- for the
  // device to settle on a rate, for the audio thread to come out of the mix,
  // for the ring to prime -- and a second of that with jammer_lock held would
  // stall the tick thread and every MIDI message with it, which you would
  // hear.  Nothing here needs the lock: what makes the swap safe is
  // whistle_engine_silence, not jammer_lock.  The status strings it writes
  // are read by take_snapshot, so a device change can garble that line for
  // one frame of a 60Hz redraw; that is the whole of the cost.
  whistle_input_start(uid.UTF8String, synth_sample_rate);
  [self rebuildWhistleMenu];
}

- (void)chooseWhistleGate:(NSMenuItem*)item {
  LOCK();
  whistle_gate = (int)item.tag;
  whistle_publish();
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setInteger:item.tag
                                           forKey:@"whistleGate"];
  [self rebuildWhistleMenu];
}

- (void)chooseWhistleLevel:(NSMenuItem*)item {
  LOCK();
  whistle_level_full = (int)item.tag;
  whistle_publish();
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setInteger:item.tag
                                           forKey:@"whistleLevel"];
  [self rebuildWhistleMenu];
}

- (void)chooseWhistleLowNote:(NSMenuItem*)item {
  LOCK();
  whistle_low_note = (int)item.tag;
  if (whistle_high_note < whistle_low_note) whistle_high_note = whistle_low_note;
  whistle_publish();
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setInteger:whistle_low_note
                                           forKey:@"whistleLowNote"];
  [self rebuildWhistleMenu];
}

- (void)chooseWhistleHighNote:(NSMenuItem*)item {
  LOCK();
  whistle_high_note = (int)item.tag;
  if (whistle_low_note > whistle_high_note) whistle_low_note = whistle_high_note;
  whistle_publish();
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setInteger:whistle_high_note
                                           forKey:@"whistleHighNote"];
  [self rebuildWhistleMenu];
}

- (void)toggleWhistlePassthrough:(NSMenuItem*)item {
  LOCK();
  whistle_passthrough = !whistle_passthrough;
  whistle_publish();
  UNLOCK();
  [self rebuildWhistleMenu];
}

// Deliberately not folded into the global volume: this is a second synthesis
// engine, and the balance between it and fluidsynth is something you set once
// and then leave alone while the global knob moves the whole rig.
- (void)whistleVolumeChanged:(NSSlider*)slider {
  LOCK();
  whistle_gain = slider.doubleValue;
  whistle_publish();
  UNLOCK();
  [NSUserDefaults.standardUserDefaults setDouble:whistle_gain
                                          forKey:@"whistleGain"];
  [self.view setNeedsDisplay:YES];
}

- (NSMenuItem*)whistleVolumeMenuItem {
  if (self.whistleVolumeItem) return self.whistleVolumeItem;

  NSView* holder = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 54)];

  NSTextField* caption = [NSTextField labelWithString:@"Whistle volume"];
  caption.font = [NSFont menuFontOfSize:0];
  caption.textColor = NSColor.labelColor;
  caption.frame = NSMakeRect(20, 30, 200, 18);
  [holder addSubview:caption];

  self.whistleVolumeSlider =
    [NSSlider sliderWithValue:whistle_gain
                     minValue:0
                     maxValue:MAX_WHISTLE_GAIN
                       target:self
                       action:@selector(whistleVolumeChanged:)];
  self.whistleVolumeSlider.frame = NSMakeRect(20, 6, 220, 20);
  self.whistleVolumeSlider.continuous = YES;
  [holder addSubview:self.whistleVolumeSlider];

  self.whistleVolumeItem = [[NSMenuItem alloc] init];
  self.whistleVolumeItem.view = holder;
  return self.whistleVolumeItem;
}

// The Speech Recognition menu: the gate in front of the recognizer.  A slider
// rather than a 0-9 knob because it's a level to match to a microphone, set
// against the meter on the speech row.
- (void)speechGateChanged:(NSSlider*)slider {
  double db = round(slider.doubleValue);
  LOCK();
  speech_gate_db = db;
  UNLOCK();
  self.speechGateCaption.stringValue =
    [NSString stringWithFormat:@"Gate: %.0f dB", db];
  [NSUserDefaults.standardUserDefaults setDouble:db forKey:@"speechGate"];
  [self.view setNeedsDisplay:YES];
}

- (void)buildSpeechMenu {
  NSMenu* menu = self.speechMenu;
  [menu removeAllItems];

  NSView* holder = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 54)];
  LOCK();
  double db = speech_gate_db;
  UNLOCK();
  self.speechGateCaption = [NSTextField labelWithString:
    [NSString stringWithFormat:@"Gate: %.0f dB", db]];
  self.speechGateCaption.font = [NSFont menuFontOfSize:0];
  self.speechGateCaption.textColor = NSColor.labelColor;
  self.speechGateCaption.frame = NSMakeRect(20, 30, 200, 18);
  [holder addSubview:self.speechGateCaption];
  NSSlider* slider = [NSSlider sliderWithValue:db
                                      minValue:SPEECH_GATE_MIN_DB
                                      maxValue:SPEECH_GATE_MAX_DB
                                        target:self
                                        action:@selector(speechGateChanged:)];
  slider.frame = NSMakeRect(20, 6, 220, 20);
  slider.continuous = YES;
  [holder addSubview:slider];
  NSMenuItem* item = [[NSMenuItem alloc] init];
  item.view = holder;
  [menu addItem:item];

  NSMenuItem* note = [[NSMenuItem alloc]
    initWithTitle:@"Only what's louder than the gate is heard"
           action:nil keyEquivalent:@""];
  note.enabled = NO;
  [menu addItem:note];

  [menu addItem:[NSMenuItem separatorItem]];
  NSMenuItem* record = [[NSMenuItem alloc]
    initWithTitle:@"Record Number Samples..."
           action:@selector(recordNumberSamples:) keyEquivalent:@""];
  record.target = self;
  [menu addItem:record];
  NSMenuItem* review = [[NSMenuItem alloc]
    initWithTitle:@"Review Number Clips..."
           action:@selector(reviewNumberClips:) keyEquivalent:@""];
  review.target = self;
  [menu addItem:review];
}

// Saying what was really said in the clips the two recognizers disagreed
// about; see numheard.h.
- (void)reviewNumberClips:(id)sender {
  if (!self.numberReview) self.numberReview = [NhReviewController new];
  [self.numberReview show];
}

// Samples of your voice saying the numbers, for the fast recognizer; see
// numtrain.h.
- (void)recordNumberSamples:(id)sender {
  if (!self.numberSamples) self.numberSamples = [NtWindowController new];
  [self.numberSamples show];
}

// A 0-9 knob as a submenu, with what each step actually means beside it where
// there's a number worth showing.
- (NSMenuItem*)knobMenu:(NSString*)title
                current:(int)current
                 action:(SEL)action
                 detail:(NSString* (^)(int))detail {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:NULL
                                         keyEquivalent:@""];
  NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
  for (int step = 0; step <= 9; step++) {
    NSString* label = detail ? detail(step)
                             : [NSString stringWithFormat:@"%d", step];
    NSMenuItem* entry = [submenu addItemWithTitle:label
                                           action:action
                                    keyEquivalent:@""];
    entry.target = self;
    entry.tag = step;
    entry.state = (step == current) ? NSControlStateValueOn
                                    : NSControlStateValueOff;
  }
  item.submenu = submenu;
  return item;
}

- (NSMenuItem*)noteMenu:(NSString*)title
                current:(int)current
                 action:(SEL)action {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:NULL
                                         keyEquivalent:@""];
  NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
  submenu.font = mono_font(13, NSFontWeightRegular);
  for (int note = whistle_lowest_note(); note <= whistle_highest_note();
       note++) {
    NSString* label = [NSString stringWithFormat:@"%@%d",
                       note_name(note), note / 12 - 1];
    NSMenuItem* entry = [submenu addItemWithTitle:label
                                           action:action
                                    keyEquivalent:@""];
    entry.target = self;
    entry.tag = note;
    entry.state = (note == current) ? NSControlStateValueOn
                                    : NSControlStateValueOff;
  }
  item.submenu = submenu;
  return item;
}

- (void)rebuildWhistleMenu {
  NSMenu* menu = self.whistleMenu;
  [menu removeAllItems];

  if (whistle_input_error[0]) {
    NSMenuItem* problem =
      [menu addItemWithTitle:@(whistle_input_error) action:NULL
               keyEquivalent:@""];
    problem.enabled = NO;
    [menu addItem:[NSMenuItem separatorItem]];
  }

  // Picking a device by hand saves its UID for good, so there has to be a way
  // back to "whatever the rig has today".
  char preferred[WHISTLE_DEVICE_NAME_MAX] = "";
  bool have_preferred =
    whistle_preferred_input(preferred, sizeof(preferred)) !=
      kAudioObjectUnknown;
  NSString* automatic = have_preferred
    ? [NSString stringWithFormat:@"Automatic (%s)", preferred]
    : @"Automatic (system default)";
  NSMenuItem* auto_item =
    [menu addItemWithTitle:automatic
                    action:@selector(chooseWhistleDevice:)
             keyEquivalent:@""];
  auto_item.target = self;
  auto_item.representedObject = @"";
  NSString* saved_input =
    [NSUserDefaults.standardUserDefaults stringForKey:@"whistleInput"] ?: @"";
  auto_item.state = saved_input.length == 0 ? NSControlStateValueOn
                                            : NSControlStateValueOff;
  [menu addItem:[NSMenuItem separatorItem]];

  WhistleInputDevice devices[WHISTLE_MAX_INPUT_DEVICES];
  int n = whistle_list_input_devices(devices, WHISTLE_MAX_INPUT_DEVICES);
  for (int i = 0; i < n; i++) {
    NSString* label = devices[i].is_default
      ? [NSString stringWithFormat:@"%s (system default)", devices[i].name]
      : @(devices[i].name);
    NSMenuItem* item = [menu addItemWithTitle:label
                                       action:@selector(chooseWhistleDevice:)
                                keyEquivalent:@""];
    item.target = self;
    item.representedObject = @(devices[i].uid);
    // Matched on the name we are actually listening to rather than on the
    // saved UID, so an unplugged interface shows the fallback as the live one.
    item.state = (strcmp(devices[i].name, whistle_input_name) == 0)
      ? NSControlStateValueOn : NSControlStateValueOff;
  }

  [menu addItem:[NSMenuItem separatorItem]];

  // Higher numbers gate less; the number means "how many times the room
  // noise a note has to be", so it means the same on any microphone.
  [menu addItem:[self knobMenu:@"Gate"
                       current:whistle_gate
                        action:@selector(chooseWhistleGate:)
                        detail:^NSString*(int step) {
    return [NSString stringWithFormat:@"%d — %.1f× the room", step,
            1.5 * pow(10.0, 0.1 * (9 - step))];
  }]];

  // Set this a bit above the level the status row shows while you whistle
  // hard.  Too high and every voice sits dark and quiet; too low and it is
  // permanently maxed out with no dynamics left.
  [menu addItem:[self knobMenu:@"Full blow level"
                       current:whistle_level_full
                        action:@selector(chooseWhistleLevel:)
                        detail:^NSString*(int step) {
    return [NSString stringWithFormat:@"%d — %.3f", step,
            engine_level_full_for_step(step)];
  }]];

  [menu addItem:[self noteMenu:@"Lowest note"
                       current:whistle_low_note
                        action:@selector(chooseWhistleLowNote:)]];
  [menu addItem:[self noteMenu:@"Highest note"
                       current:whistle_high_note
                        action:@selector(chooseWhistleHighNote:)]];

  [menu addItem:[NSMenuItem separatorItem]];

  NSMenuItem* raw =
    [menu addItemWithTitle:@"Raw input (check the microphone)"
                    action:@selector(toggleWhistlePassthrough:)
             keyEquivalent:@""];
  raw.target = self;
  raw.state = whistle_passthrough ? NSControlStateValueOn
                                  : NSControlStateValueOff;

  [menu addItem:[NSMenuItem separatorItem]];
  self.whistleVolumeSlider.doubleValue = whistle_gain;
  [menu addItem:[self whistleVolumeMenuItem]];
}

// Jammer only reads the keyboard while it's frontmost, so that's exactly how
// long it should hold onto the F-keys.  Switch away and they go back to
// controlling brightness and volume.
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
  // The microphone first: it puts the input device's sample rate back, and it
  // has to be done while there is still an audio thread to wait for.  Not
  // under the lock, for the reason in chooseWhistleDevice.
  whistle_input_stop();
  audio_mix_hook = NULL;
  stop_synth();
  // The synth has let go of the output device, so its block size can go back
  // to whatever the rest of the machine was using.
  whistle_restore_buffer_frames(kAudioObjectUnknown);
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

  NSMenuItem* whistle_item = [NSMenuItem new];
  [menubar addItem:whistle_item];
  delegate.whistleMenu = [[NSMenu alloc] initWithTitle:@"Whistle"];
  whistle_item.submenu = delegate.whistleMenu;

  NSMenuItem* speech_item = [NSMenuItem new];
  [menubar addItem:speech_item];
  delegate.speechMenu =
    [[NSMenu alloc] initWithTitle:@"Speech Recognition"];
  speech_item.submenu = delegate.speechMenu;
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
    // Volume and output device are both remembered, so a rig that's set up
    // once stays set up.
    NSNumber* saved_gain =
      [NSUserDefaults.standardUserDefaults objectForKey:@"synthGain"];
    if (saved_gain) synth_gain = saved_gain.doubleValue;

    // An explicit choice beats the system default, which on a laptop is the
    // built-in speakers -- rarely what you want on stage.
    const char* device = getenv("JAMMER_AUDIO_DEVICE");
    if (!device) {
      NSString* saved =
        [NSUserDefaults.standardUserDefaults stringForKey:@"audioDevice"];
      if (saved) device = saved.UTF8String;
    }
    // The whistle before the synth, because the two have to agree on a sample
    // rate and it is the microphone that gets to pick.  The detector works out
    // how fast the signal is wiggling in samples: hand it 48kHz audio while it
    // believes it is at 44.1kHz and every note comes out a semitone and a half
    // sharp.  Taking the rate *from* the microphone means the common case --
    // one interface doing both ends of the rig -- changes no device settings
    // at all.  See whistle_request_rate for the other case.
    whistle_init_state();
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    NSString* whistle_input =
      [defaults stringForKey:@"whistleInput"] ?: @"";
    if ([defaults objectForKey:@"speechGate"]) {
      speech_gate_db = [defaults doubleForKey:@"speechGate"];
    }
    if ([defaults objectForKey:@"whistleGate"]) {
      whistle_gate = (int)[defaults integerForKey:@"whistleGate"];
    }
    if ([defaults objectForKey:@"whistleLevel"]) {
      whistle_level_full = (int)[defaults integerForKey:@"whistleLevel"];
    }
    if ([defaults objectForKey:@"whistleLowNote"]) {
      whistle_low_note = (int)[defaults integerForKey:@"whistleLowNote"];
    }
    if ([defaults objectForKey:@"whistleHighNote"]) {
      whistle_high_note = (int)[defaults integerForKey:@"whistleHighNote"];
    }
    if ([defaults objectForKey:@"whistleGain"]) {
      whistle_gain = [defaults doubleForKey:@"whistleGain"];
    }

    AudioDeviceID mic = whistle_device_for_uid(whistle_input.UTF8String);
    double mic_rate = mic != kAudioObjectUnknown ? whistle_device_rate(mic) : 0;
    if (mic_rate > 0) synth_sample_rate = mic_rate;

    // Before the synth opens it: fluidsynth's CoreAudio driver never asks for
    // a block size, so its client inherits the device's -- and that block is
    // what sets the pace for the whole rig, the whistle's ring included.  A
    // 512-frame default costs 10.7ms on the way out and forces the ring to
    // hold another 10.7ms on the way in.  Measured, asking for 64 takes the
    // whistle from 13.3ms of added latency to 4.0ms and cuts the synth's own
    // output latency with it.
    //
    // This is the way in because the other one doesn't work: setting
    // audio.period-size makes the driver open happily and never pull a sample
    // (see macapi.h; still true, re-measured).  $JAMMER_OUTPUT_BUFFER
    // overrides it, and 0 leaves the device alone.
    const char* out_buffer = getenv("JAMMER_OUTPUT_BUFFER");
    const char* out_device = device ? device : "default";
    whistle_prepare_output_device(out_device, out_buffer ? atoi(out_buffer)
                                                         : 64);

    start_synth(soundfont, out_device);

    // The failure this is guarding against is a device that opens and then
    // never asks for a sample, which is how the buffering settings misbehave
    // on CoreAudio and would be silence on stage.  set_audio_device already
    // checks for it and backs off fluidsynth's own settings, but it cannot
    // undo a block size that belongs to the device, so that is checked here.
    if (!audio_is_flowing()) {
      printf("no audio with a %s-frame output block; putting the device back\n",
             out_buffer ? out_buffer : "64");
      whistle_restore_buffer_frames(whistle_output_device);
      set_audio_device(out_device);
    }

    // Which preset each voice key plays, looked up by name -- see
    // whistle_resolve_voices.  Then the microphone, then the mix: the hook is
    // safe to install before there is an engine, since it checks.
    whistle_resolve_voices();
    audio_mix_hook = whistle_mix;
    whistle_input_start(whistle_input.UTF8String, synth_sample_rate);
    speech_start(synth_sample_rate);

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
