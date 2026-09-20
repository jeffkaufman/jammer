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
SOUNDFONT := FluidR3_GM.sf2
# The same fluid-soundfont-gm package the Pi installs, so the Mac build sounds
# identical.  Grabs whatever the current version in the pool is.
SOUNDFONT_POOL := http://deb.debian.org/debian/pool/main/f/fluid-soundfont/

MAC_SRCS := jammer-mac.m macapi.h keylayout.h keypad.h fkeys.h \
            jammermidilib.h voices.h common.h

jammer-mac: $(MAC_SRCS)
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang jammer-mac.m -o jammer-mac \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -framework Cocoa -framework CoreMIDI -framework Carbon -framework IOKit \
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
run-mac: jammer-mac $(SOUNDFONT)
	./jammer-mac

# Jammer.app is self-contained: it carries the soundfont and its own copies of
# libfluidsynth and everything that links against, so it runs on a Mac with no
# homebrew installed.
APP := Jammer.app
app: jammer-mac $(SOUNDFONT)
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources \
	         $(APP)/Contents/Frameworks
	cp jammer-mac $(APP)/Contents/MacOS/jammer
	cp $(SOUNDFONT) $(APP)/Contents/Resources/
	cp Info.plist $(APP)/Contents/
	./vendor-dylibs.sh $(APP)
	codesign --force --deep --sign - $(APP)
	@echo "built $(APP)"

clean-mac:
	rm -rf jammer-mac $(APP) audition

# Hear the soundfont's drum sounds one at a time; see the top of audition.c.
audition: audition.c macapi.h common.h
	@test -n "$(FLUIDSYNTH)" || \
	  { echo "fluidsynth not found; run: brew install fluid-synth"; exit 1; }
	clang audition.c -o audition \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -framework CoreFoundation \
	  -std=gnu11 -Wall -O2

.PHONY: run run-fakeinput runmac soundfont run-mac app clean-mac test-mac

test-mac: test-keypad.c test-startup.c $(MAC_SRCS) keypad.h
	clang test-keypad.c -o /tmp/jammer-test-keypad \
	  -I$(FLUIDSYNTH)/include -L$(FLUIDSYNTH)/lib -lfluidsynth \
	  -framework Carbon -framework IOKit -std=gnu11 -Wall
	/tmp/jammer-test-keypad
	clang test-startup.c -o /tmp/jammer-test-startup -I. -std=gnu11 -w
	/tmp/jammer-test-startup
