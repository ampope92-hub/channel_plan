// Channel planner for the line's train radio network.
// usage: planner <survey-dir> <plan-file>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: planner <survey-dir> <plan-file>\n";
        return 2;
    }
    // TODO: read the survey in argv[1], write the plan to argv[2]
    std::cerr << "not implemented\n";
    return 1;
}
