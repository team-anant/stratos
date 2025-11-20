#ifndef STATE_H
#define STATE_H

#include <csignal>
#include <string>
#include <iostream>
#include <unistd.h>

class State {
protected:
    static volatile sig_atomic_t paused;  

    static void handle_pause(int signo) {
        paused = 1;
    }

public:
    virtual void init() {
        signal(SIGUSR1, handle_pause);
    }

    virtual void run() ;

    virtual ~State() {}
};

volatile sig_atomic_t State::paused = 0;

#endif 
