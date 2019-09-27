#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "jammermidilib.h"

int main(int argc, char** argv) {
  jml_setup();
  while (true) {
    usleep(1000000);
  }
  return 0;
}
