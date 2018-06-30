jammermidi: jammermidi.m jammermidilib.h
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
	  jammermidi.m -o jammermidi -std=c99 -Wall

run: jammermidi
	./jammermidi
