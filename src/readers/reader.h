#ifndef READER_H
#define READER_H

#include <iostream>
#include <vector>
#include <algorithm>
#include <csignal>
#include <cstring>    // memset
#include <unistd.h>   // kill, pid_t
#include "State.h"

class Reader {
public:
    // inline static to avoid needing a separate definition in a .cpp file
    inline static std::vector<pid_t> subscribers;

    virtual void init() = 0;
    virtual void action() = 0;

    // signal handler for adding a subscriber
    static void add(int /*signo*/, siginfo_t *info, void * /*context*/) {
        if (info)
            subscribers.push_back(info->si_pid);
    }

    // signal handler for removing a subscriber
    static void remove(int /*signo*/, siginfo_t *info, void * /*context*/) {
        if (!info) return;
        pid_t pid_to_remove = info->si_pid;
        subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), pid_to_remove),
                          subscribers.end());
    }

    void notifyAll(int signo = SIGUSR1) {
        for (pid_t pid : subscribers) {
            // best-effort notify; ignore errors here (could check return value)
            ::kill(pid, signo);
        }
    }

    virtual ~Reader() = default;

protected:
    // helper to install signal handlers; call this from derived init() if desired
    void setupSignalHandlers() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO;       // use sa_sigaction
        sa.sa_sigaction = &Reader::add; // assign three-arg handler
        sigaction(SIGUSR1, &sa, nullptr);

        std::memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = &Reader::remove;
        sigaction(SIGUSR2, &sa, nullptr);
    }

    inline static Reader* instance = nullptr;
};

#endif // READER_H
