#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "jammermidilib.h"

int main(int argc, char** argv) {
  jml_setup();
  while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE)) {
    usleep(TICK_MS * 1000);
    jml_tick();
  }
  return 0;
}
