jammermidi: jammermidi.m jammermidilib.h
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
	  jammermidi.m -o jammermidi -std=c99

run: jammermidi
	./jammermidi
