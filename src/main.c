// TODO: Better error handling and gracful exits
// TODO: Maybe encapsulate the spawning of the actors into there respecive
// headers
#include "reader_table.h"
#include "state_table.h"
#include "utils.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static uint8_t inputs[WORDS] = {0};
static int current_state = 0;

static void child_sigtstp_handler(int signo) {
  (void)signo;
  running = 0;
}

void parent_sigusr_handler(int signo, siginfo_t *info, void *context) {
  (void)signo;
  (void)context;

  // get which reader sent signal from its pid
  int r = -1;
  for (int i = 0; i < NO_OF_READERS; i++) {
    if (reader_table[i].pid == info->si_pid) {
      r = i;
      break;
    }
  }
  if (r < 0) {
    return;
  }

  // decode the signal into input update
  uint32_t sigv = (uint32_t)info->si_value.sival_int;
  uint16_t care = (uint16_t)(sigv >> 16);
  uint16_t change = (uint16_t)sigv;

  const struct ReaderEntry *re = &reader_table[r];

  for (size_t j = 0; j < re->scope_len; j++) {
    if (!(care & (0x8000 >> j))) {
      continue;
    }

    unsigned bit_idx = re->scope[j];
    unsigned word_idx = bit_idx / 8;
    unsigned off = bit_idx % 8;
    if (word_idx >= WORDS) {
      continue;
    }

    inputs[word_idx] =
        (inputs[word_idx] & ~(0x80 >> off)) | (((change >> (MAX_AFFECTED_BITS - 1 - j)) & 1u) << (7 - off));
  }

  // switch state if the updated inputs match a transition.
  for (int i = 0; i < state_table[current_state].transition_count; i++) {
    if (!compare_input_mask(inputs,
                            state_table[current_state].transitions[i])) {
      continue;
    }

    size_t next = state_table[current_state].transitions[i].next;
    if (next != (size_t)current_state) {
      kill(state_table[current_state].pid, SIGSTOP);
      current_state = next;
      kill(state_table[current_state].pid, SIGCONT);
    }
    break;
  }
}

int main(void) {
  // install sigusr handler
  struct sigaction sa = {0};
  sa.sa_sigaction = parent_sigusr_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigemptyset(&sa.sa_mask) < 0) {
    perror("sigemptyset");
    return 1;
  }
  if (sigaction(SIGUSR1, &sa, NULL) < 0) {
    perror("sigaction");
    return 1;
  }

  // spawn state actors
  for (int i = 0; i < NO_OF_STATES; i++) {
    pid_t pid = fork();

    if (pid < 0) {
      perror("fork");
      // TODO: kill the procs gracefully and exit func
      return 1;
    }

    // inside child
    if (pid == 0) {
      if (signals_install(SIGTSTP, child_sigtstp_handler) < 0) {
        perror("sigaction");
        return 1;
      }

      // init state
      if (state_table[i].state->init()) {
        perror("state init");
        return 1;
      }

      // pause state after initialization and wait for signal from parent to run
      raise(SIGSTOP);
      while (1) {
        if (!running) {
          running = 1;
          raise(SIGSTOP);
        }

        state_table[i].state->run();
      }
    }

    // parent proc
    else {
      state_table[i].pid = pid;
    }
  }

  // spawn readers
  for (int i = 0; i < NO_OF_READERS; i++) {
    pid_t pid = fork();

    if (pid < 0) {
      perror("fork");
      // TODO: kill the procs gracefully and exit func
      return 1;
    }

    // inside child
    if (pid == 0) {
      // init reader
      if (reader_table[i].reader->init()) {
        perror("reader init");
        return 1;
      }

      while (1) {
        if (!running) {
          running = 1;
          raise(SIGSTOP);
        }

        reader_table[i].reader->run();
      }
    }

    // parent proc
    else {
      reader_table[i].pid = pid;
    }
  }

  // TODO: This seems hacky, change it later to something
  // more graceful

  // start initial state
  sleep(1); // wait for state to pause for the first time
  kill(state_table[current_state].pid, SIGCONT);

  // pause and wait for signal from reader
  while (1) {
    pause();
  }
}
