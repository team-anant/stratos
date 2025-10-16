#include <unistd.h>
#include <signal.h>
#include <bits/std++.h>

class reader {
    public:
        virtual void init() = 0;
        virtual void action() = 0;


        void sigusrhandler(int signo, siginfo_t *info, void *context) {
        }

	// there will also be handlers for graceful stop 
	
	// create a reader helper function that sends the signals to all subscribed processes

        virtual ~reader() {}
};
