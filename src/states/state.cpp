#include "state.h"

class state {
    public:
        virtual void init() = 0;
        virtual void action() = 0;
};