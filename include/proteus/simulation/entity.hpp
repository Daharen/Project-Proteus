#pragma once

#include <string>
#include <unordered_map>

namespace proteus::simulation {

struct Entity {
    std::string id;
    std::unordered_map<std::string, double> stats;
    std::unordered_map<std::string, double> identity_axes;
};

struct ReinforcementInputs {
    double depth = 0.0;
    double breadth = 0.0;
    double volatility = 0.0;
    double switching = 0.0;
};

class ReinforcementModel {
public:
    struct Config {
        double depth_exponent = 1.15;
        double breadth_linear_weight = 0.35;
    };

    ReinforcementModel();
    explicit ReinforcementModel(Config config);

    double score(const ReinforcementInputs& inputs) const;

private:
    Config config_;
};

}  // namespace proteus::simulation
