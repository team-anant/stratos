#include "state.h"

#include <stdio.h>
#include <unistd.h>

int rotate_init(void) {
  printf("Rotate start\n");
  return 0;
}

void rotate_run(void) {
  printf("Rotating\n");
  sleep(1);
}

void rotate_exit(void) { printf("Rotated\n"); }

const State state_rotate = {
    .init = &rotate_init,
    .run = &rotate_run,
    .exit = &rotate_exit,
};
