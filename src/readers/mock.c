#include "reader.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fd = -1;

int mock_init(void) {
  fd = open("/tmp/stratos", O_RDWR);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  printf("Mock reader started\n");

  return 0;
}

void mock_run(void) {
  char c;
  int n = read(fd, &c, 1);
  if (n < 0) {
    if (errno == EINTR) {
      return;
    } else {
      perror("read");
      exit(1);
    }
  }
  if (n == 0 || c == '\n') {
    return;
  } else {
    int n = c - '0';

    if (n < 5) {
      sigqueue(getppid(), SIGUSR1, (union sigval){.sival_int = 0xFFFF8000});
    } else if (n >= 5) {
      sigqueue(getppid(), SIGUSR1, (union sigval){.sival_int = 0xFFFF4000});
    }
  }
}

void mock_exit(void) { close(fd); }

const Reader mock = {
    .init = mock_init,
    .run = mock_run,
    .exit = mock_exit,
};
