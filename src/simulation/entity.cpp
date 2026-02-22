#include "proteus/simulation/entity.hpp"

#include <cmath>

namespace proteus::simulation {

ReinforcementModel::ReinforcementModel() : config_{} {}

ReinforcementModel::ReinforcementModel(Config config) : config_(config) {}

double ReinforcementModel::score(double depth, double breadth) const {
    const auto compounded_depth = std::pow(depth, config_.depth_compounding);
    const auto scaled_breadth = breadth * config_.breadth_scaling;
    return compounded_depth + scaled_breadth;
}

}  // namespace proteus::simulation
