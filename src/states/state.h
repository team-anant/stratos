class state {
    public:
        virtual void init() = 0;
        virtual void action() = 0;

        virtual ~state() {}
};

// note: after being initialised the state must stop and wait for SIGCONT 
