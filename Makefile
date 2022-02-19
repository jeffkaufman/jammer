jammer: jammer.c jammermidilib.h linuxapi.h common.h
	gcc jammer.c -lm -lasound -o jammer -std=c99 -Wall -Werror

jammer-fakeinput: jammer.c jammermidilib.h linuxapi.h common.h
	gcc jammer.c -lm -lasound -o jammer-fakeinput -std=c99 \
	  -Wall -Werror -DFAKE_FEET -DFAKE_CHANGE_PITCH

run: jammer
	./jammer $(CURDIR)/kbd-config

run-fakeinput: jammer-fakeinput
	./jammer-fakeinput $(CURDIR)/kbd-config
