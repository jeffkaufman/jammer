jammermidi: jammermidi.m jammermidilib.h macapi.h common.h
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework IOKit \
    -framework Foundation \
	  jammermidi.m -o jammermidi -std=c99 -Wall

jammer: jammer.c jammermidilib.h linuxapi.h common.h
	gcc -lasound -lm jammer.c -o jammer -std=c99 -Wall

run: jammer
	./jammer

run-old: jammermidi
	./jammermidi
