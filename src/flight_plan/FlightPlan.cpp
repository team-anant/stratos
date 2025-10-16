#include "utilities.h"
#include <unordered_map>
#include <unistd.h>
// #include "yaml-cpp/yaml.h"

typedef struct {
    pid_t pid;
    std::string path;
} state;

// create sigusrhandler
void sigusrhandler(int sig_num, siginfo_t *info, void *context) {
    uint32_t sigv = info->si_value.sival_int;
    // figure out next state 
    // kill(current_state.pid, SIGSTOP)
    // kill(next_state.pid, SIGCONT)
}

std::unordered_map<int, state> stateMap;

// std::unordered_map<int, std::string> parseConfig(std::string path) {

//     std::unordered_map<int, std::string> map;

//     try {
//         YAML::Node config = YAML::LoadFile(path);

//         for (auto it = config.begin(); it != config.end(); ++it) {
//             int key = it->first.as<int>();
//             std::string value = it->second.as<std::string>();
//             map[key] = value;
//         }

//         // Print to check
//         for (const auto& [k, v] : map) {
//             std::cout << k << " -> " << v << "\n";
//         }
//     } catch (const YAML::Exception& e) {
//         std::cerr << "YAML error: " << e.what() << std::endl;
//     }

//     return map;

// }

class FlightPlan {
    private:
        void main();        
    public:
        FlightPlan(std::string pathToFlightPlan);

        state current_state;

        ~FlightPlan();
};

FlightPlan::FlightPlan(std::string pathToFlightPlan) {

    std::unordered_map<int, std::string> pathMap;

    // pathMap = parseConfig("/.config.yml") // should return a dict with state identifier: path to state
    int noOfStates = 2;
    pathMap[0] = "/states/rotatory.cpp";
    pathMap[1] = "/states/stationary.cpp";

    // for loop to create each state 
    for(auto it = pathMap.begin(); it != pathMap.end(); ++it) {
        pid_t pid = fork();
        if(pid==0) { // child
            // copy the string to a path because kill needs a const path
            // swap std::string with const char* in pathMap to remove this for loop and straight up do exec.
            char path[it->second.size()];
            for(int i=0; i<it->second.size(); i++) {
                path[i] = it->second[i];
            }
            const char* real_path = path;
            if(!execv(real_path, NULL)) {
                kill(getppid(), SIGINT);
	    }
            
        } else { // add error handling
            pause();
            state st = {pid, it->second};
            stateMap[it->first] = st;
        }
    }
    main();
}

FlightPlan::~FlightPlan() {
    // add code for memory cleanup 
}

void FlightPlan::main() {
    // write the main code that runs after initial setup
    while(1) {
	    pause();
    }
}
