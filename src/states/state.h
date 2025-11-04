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

    static void handle_resume(int signo) {
        paused = 0;
    }

public:
    virtual void init() {
        signal(SIGUSR1, handle_pause);
        signal(SIGUSR2, handle_resume);
    }

    virtual void run() ;

    virtual void pause();

    virtual ~State() {}
};

volatile sig_atomic_t State::paused = 0;

#endif 
