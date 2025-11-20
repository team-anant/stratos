#include <iostream>
#include <csignal>
#include <unistd.h>
#include "State.h"

class Rotate : public State {
private:
    static volatile sig_atomic_t paused;

    static void handle_pause(int signo) {
        paused = 1;
    }

public:
    void init() override {
        signal(SIGUSR1, handle_pause);
    }

    void run() override {
        while (true) {
            if (!paused) {
                void action();
            } 
        }
    }

};

