jammer: jammer.c jammermidilib.h voices.h linuxapi.h common.h
	gcc jammer.c -lm -lasound -o jammer -std=c99 -Wall -Werror

jammer-fakeinput: jammer.c jammermidilib.h voices.h linuxapi.h common.h
	gcc jammer.c -lm -lasound -o jammer-fakeinput -std=c99 \
	  -Wall -Werror -DFAKE_FEET -DFAKE_CHANGE_PITCH

jammermidimac: jammermidimac.m jammermidimaclib.h
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
	  jammermidimac.m -o jammermidimac -std=c99 -Wall

run: jammer
	./jammer $(CURDIR)/kbd-config

run-fakeinput: jammer-fakeinput
	./jammer-fakeinput $(CURDIR)/kbd-config

runmac: jammermidimac
	./jammermidimac

### Mac version #############################################################

FLUIDSYNTH := $(shell brew --prefix fluid-synth 2>/dev/null)

# The whistle bass is whistle-synth's engine, compiled unchanged from that
# repo rather than copied in here -- copies drift, and both of these live side
# by side in ~/code.  Point this elsewhere if it doesn't.  Mac-only: the Pi
# runs whistle-synth as its own service and the `jammer` target below doesn't
# know about any of this.
WHISTLE_DIR ?= ../whistle-synth
WHISTLE_SRCS := $(WHISTLE_DIR)/pitch.c $(WHISTLE_DIR)/synth.c \
                $(WHISTLE_DIR)/engine.c
WHISTLE_OBJS := whistle-build/pitch.o whistle-build/synth.o \
                whistle-build/engine.o
WHISTLE_HDRS := $(WHISTLE_DIR)/pitch.h $(WHISTLE_DIR)/synth.h \
                $(WHISTLE_DIR)/engine.h

whistle-build/%.o: $(WHISTLE_DIR)/%.c $(WHISTLE_HDRS)
	@test -d $(WHISTLE_DIR) || \
	  { echo "no whistle-synth at $(WHISTLE_DIR); set WHISTLE_DIR"; exit 1; }
	@mkdir -p whistle-build
	clang -c $< -o $@ -I$(WHISTLE_DIR) -std=c11 -Wall -O2

SOUNDFONT := FluidR3_GM.sf2
# The same fluid-soundfont-gm package the Pi installs, so the Mac build sounds
# identical.  Grabs whatever the current version in the pool is.
SOUNDFONT_POOL := http://deb.debian.org/debian/pool/main/f/fluid-soundfont/

MAC_SRCS := jammer-mac.m macapi.h keylayout.h keypad.h fkeys.h \
            jammermidilib.h voices.h common.h whistle.h whistleinput.h \
            speech.h speechwords.h Info.plist

jammer-mac: $(MAC_SRCS) $(WHISTLE_OBJS)
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang jammer-mac.m $(WHISTLE_OBJS) -o jammer-mac \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -I$(WHISTLE_DIR) \
	  -framework Cocoa -framework CoreMIDI -framework Carbon -framework IOKit \
	  -framework AudioToolbox -framework CoreAudio \
	  -framework Speech -framework AVFoundation \
	  -Wl,-sectcreate,__TEXT,__info_plist,Info.plist \
	  -fobjc-arc -std=gnu11 -Wall -O2

$(SOUNDFONT):
	rm -rf sf2-tmp && mkdir sf2-tmp
	deb=$$(curl -sf $(SOUNDFONT_POOL) \
	         | grep -o 'fluid-soundfont-gm_[^"]*_all\.deb' \
	         | sort -V | tail -n 1) && \
	  echo "fetching $$deb" && \
	  curl -Lf --progress-bar -o sf2-tmp/gm.deb "$(SOUNDFONT_POOL)$$deb"
	cd sf2-tmp && ar x gm.deb && tar xf data.tar.*
	mv sf2-tmp/usr/share/sounds/sf2/$(SOUNDFONT) $(SOUNDFONT)
	rm -rf sf2-tmp

soundfont: $(SOUNDFONT)

# Run straight out of the source directory.
run-mac: jammer-mac $(SOUNDFONT) $(SPEECH_MODEL)
	./jammer-mac

# Jammer.app is self-contained: it carries the soundfont and its own copies of
# libfluidsynth and everything that links against, so it runs on a Mac with no
# homebrew installed.
APP := Jammer.app
app: jammer-mac $(SOUNDFONT) $(SPEECH_MODEL)
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources \
	         $(APP)/Contents/Frameworks
	cp jammer-mac $(APP)/Contents/MacOS/jammer
	cp $(SOUNDFONT) $(SPEECH_MODEL) $(APP)/Contents/Resources/
	cp Info.plist $(APP)/Contents/
	./vendor-dylibs.sh $(APP)
	codesign --force --deep --sign - $(APP)
	@echo "built $(APP)"

clean-mac:
	rm -rf jammer-mac $(APP) audition pads speechphrases speechmodel \
	  $(SPEECH_MODEL) kitlevels whistlelevels whistle-build

# Hear the soundfont's drum sounds one at a time; see the top of audition.c.
audition: audition.c macapi.h common.h
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang audition.c -o audition \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -framework CoreFoundation \
	  -std=gnu11 -Wall -O2

# The speech recognizer's dictionary: every phrase it should expect, printed
# from the jammer's own tables, made into training data for a custom language
# model.  See speechmodel.swift.
speechphrases: speechphrases.c $(MAC_SRCS) $(WHISTLE_OBJS)
	clang speechphrases.c $(WHISTLE_OBJS) -o speechphrases \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -I$(WHISTLE_DIR) \
	  -framework Carbon -framework IOKit \
	  -framework AudioToolbox -framework CoreAudio -std=gnu11 -Wall -w

speechmodel: speechmodel.swift
	swiftc -O speechmodel.swift -o speechmodel

SPEECH_MODEL := speech-model.bin
$(SPEECH_MODEL): speechphrases speechmodel
	./speechphrases | ./speechmodel $(SPEECH_MODEL)

# Hear pads as the drone bass and drone chord would play them; see the top
# of pads.c.
pads: pads.c macapi.h jammermidilib.h voices.h common.h aweight.h
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang pads.c -o pads \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -framework CoreFoundation \
	  -std=gnu11 -Wall -O2

# Are the kits at the same perceived volume?  See the top of kitlevels.c.
kitlevels: kitlevels.c aweight.h jammermidilib.h voices.h common.h macapi.h
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang kitlevels.c -o kitlevels \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -std=gnu11 -Wall -O2

# Is the whistle at the same perceived volume as the endpoints it plays
# beside?  See the top of whistlelevels.c.
whistlelevels: whistlelevels.c aweight.h jammermidilib.h voices.h common.h \
               macapi.h $(WHISTLE_OBJS)
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang whistlelevels.c $(WHISTLE_OBJS) -o whistlelevels \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -I$(WHISTLE_DIR) \
	  -std=gnu11 -Wall -O2

.PHONY: run run-fakeinput runmac soundfont run-mac app clean-mac test-mac

test-mac: test-keypad.c test-startup.c kitlevels.c aweight.h \
          $(MAC_SRCS) keypad.h $(WHISTLE_OBJS)
	clang test-keypad.c $(WHISTLE_OBJS) -o /tmp/jammer-test-keypad \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -I$(WHISTLE_DIR) \
	  -framework Carbon -framework IOKit \
	  -framework AudioToolbox -framework CoreAudio -std=gnu11 -Wall
	/tmp/jammer-test-keypad
	clang test-startup.c -o /tmp/jammer-test-startup -I. -std=gnu11 -w
	/tmp/jammer-test-startup
	$(MAKE) kitlevels
	./kitlevels --check
	$(MAKE) pads
	./pads --check
