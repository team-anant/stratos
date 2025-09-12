#include <unistd.h>
#include <signal.h>

class reader {
    public:
        virtual void init() = 0;
        virtual void action() = 0;


        void sigusrhandler(int signo, siginfo_t *info, void *context) {
        }

        virtual ~reader() {}
};