#pragma once

#include <string>
#include <unordered_map>

namespace proteus::simulation {

struct Entity {
    std::string id;
    std::unordered_map<std::string, double> stats;
    std::unordered_map<std::string, double> identity_axes;
};

class ReinforcementModel {
public:
    struct Config {
        double depth_compounding = 1.15;
        double breadth_scaling = 0.35;
    };

    ReinforcementModel();
    explicit ReinforcementModel(Config config);

    double score(double depth, double breadth) const;

private:
    Config config_;
};

}  // namespace proteus::simulation
