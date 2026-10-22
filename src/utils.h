#ifndef UTILS_H
#define UTILS_H

#include "state_table.h"
#include <signal.h>
#include <stdio.h>

/// Function that assigns a handler to a signal with empty mask and no flags
/*
 * Returns 0 on success, -1 on failure
 */
int signals_install(int signo, void (*handler)(int));
int compare_input_mask(uint8_t *inputs, Transition transition);

#endif // !UTILS_H
