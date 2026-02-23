#include "proteus/simulation/entity.hpp"

#include <cmath>

namespace proteus::simulation {

ReinforcementModel::ReinforcementModel() : config_{} {}

ReinforcementModel::ReinforcementModel(Config config) : config_(config) {}

double ReinforcementModel::score(const ReinforcementInputs& inputs) const {
    const auto compounded_depth = std::pow(inputs.depth, config_.depth_exponent);
    const auto scaled_breadth = inputs.breadth * config_.breadth_linear_weight;
    return compounded_depth + scaled_breadth;
}

}  // namespace proteus::simulation
