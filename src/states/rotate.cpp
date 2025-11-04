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

    static void handle_resume(int signo) {
        paused = 0;
    }

public:
    void init() override {
        signal(SIGUSR1, handle_pause);
        signal(SIGUSR2, handle_resume);
    }

    void run() override {
        while (true) {
            if (!paused) {
                void action();
                sleep(1);
            } else {
                pause();  
            }
        }
    }

    void pause() override {
        paused = 1;
    }

};

