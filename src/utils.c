#include "utils.h"

inline int signals_install(int signo, void (*handler)(int)) {
  struct sigaction sa = {0};

  sa.sa_handler = handler;
  sa.sa_flags = 0;

  if (sigemptyset(&sa.sa_mask) < 0) {
    return -1;
  };

  if (sigaction(signo, &sa, NULL) < 0) {
    return -1;
  }
  return 0;
}

inline int compare_input_mask(uint8_t *inputs, Transition transition) {
  for (size_t i = 0; i < WORDS; i++) {
    if ((inputs[i] & transition.care[i]) !=
        (transition.expect[i] & transition.care[i])) {
      return 0;
    }
  }
  return 1;
}
