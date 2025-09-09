#include "utilities.h"
#include <unordered_map>
// #include "yaml-cpp/yaml.h"

typedef struct {
    pid_t pid;
} state;

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
    public:
        FlightPlan(std::string pathToFlightPlan);
        ~FlightPlan();
};

FlightPlan::FlightPlan(std::string pathToFlightPlan) {
    /*
        open the yaml file with stateno. to state (path to states) mappings,
        go through each path, fork and exec that path. in the parent process, 
        add the state and the corresponding pid struct to the stateMap
    */

    std::unordered_map<int, std::string> pathMap;

    // pathMap = parseConfig("/.config.yml")
    pathMap[0] = "/states/rotatory.cpp";
    pathMap[1] = "/states/stationary.cpp";

    std::cout << pathMap[0000];

}

FlightPlan::~FlightPlan() {
    // add code for memory cleanup 
}

int main() {
    FlightPlan fp = FlightPlan("state_table.csv");
}