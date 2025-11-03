#ifndef READER_H
#define READER_H

#include <vector>
#include <signal.h>
#include <unistd.h>

class reader {
public:
    static std::vector<pid_t> subscribers;

    virtual void init() = 0;
    virtual void action() = 0;

    virtual void sigusrhandler(int signo, siginfo_t *info, void *context) {}

    static void notifyAll(int signo = SIGUSR1) {
        for (pid_t pid : subscribers)
            kill(pid, signo);
    }

    virtual ~reader() {}

protected:
    static void handlerDispatch(int signo, siginfo_t *info, void *context) {
        if (instance)
            instance->sigusrhandler(signo, info, context);
    }

    static inline reader* instance = nullptr;
};


#endif 
