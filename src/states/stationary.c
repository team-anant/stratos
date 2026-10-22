#include "state.h"

#include <stdio.h>
#include <unistd.h>

int stationary_init(void) {
  printf("Stationary start\n");
  return 0;
}

void stationary_run(void) {
  printf("Stationarying\n");
  sleep(1);
}

void stationary_exit(void) { printf("Stationaried\n"); }

const State state_stationary = {
    .init = &stationary_init,
    .run = &stationary_run,
    .exit = &stationary_exit,
};
