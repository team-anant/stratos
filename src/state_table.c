#include "state_table.h"

extern const State state_stationary;
extern const State state_rotate;

StateEntry state_table[NO_OF_STATES] = {
    {
        .state = &state_stationary,
        .transition_count = 2,
        .transitions =
            {
                {.care = {0xFF}, .expect = {0x40}, .next = 1},
                {.care = {0xFF}, .expect = {0x80}, .next = 0},
            },
    },

    {
        .state = &state_rotate,
        .transition_count = 2,
        .transitions =
            {
                {.care = {0xFF}, .expect = {0x80}, .next = 0},
                {.care = {0xFF}, .expect = {0x40}, .next = 1},
            },
    },
};
