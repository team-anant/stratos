#ifndef STATE_TABLE_H
#define STATE_TABLE_H

#include "states/state.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_INPUT_FIELDS 2
#define WORDS ((MAX_INPUT_FIELDS + 7) / 8)

#define NO_OF_STATES 2
#define MAX_TRANSITIONS 2 // per state not total

typedef struct {
  const uint8_t care[WORDS];   // bits that must match, ie aren't X. 0 = don't care / are X
  const uint8_t expect[WORDS]; // value those bits must have
  const size_t next;
} Transition;

typedef struct {
  pid_t pid;
  const State *state;
  const Transition transitions[MAX_TRANSITIONS];
  const int transition_count;
} StateEntry;

extern StateEntry state_table[NO_OF_STATES];

#endif
