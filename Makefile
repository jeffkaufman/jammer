jammer: jammer.c jammermidilib.h linuxapi.h common.h
	gcc -lasound -lm jammer.c -o jammer -std=c99 -Wall -Werror

jammer-fakeinput: jammer.c jammermidilib.h linuxapi.h common.h
	gcc -lasound -lm jammer.c -o jammer-fakeinput -std=c99 \
	  -Wall -Werror -DFAKE_FEET -DFAKE_CHANGE_PITCH

run: jammer
	./jammer $(CURDIR)/kbd-config

run-fakeinput: jammer-fakeinput
	./jammer-fakeinput $(CURDIR)/kbd-config
