jammer: jammer.c jammermidilib.h linuxapi.h common.h
	gcc -lasound -lm jammer.c -o jammer -std=c99 -Wall -Werror

run: jammer
	./jammer $(CURDIR)/kbd-config
