#include "proteus/simulation/entity.hpp"

#include <iostream>

int main() {
    proteus::simulation::ReinforcementModel model;
    const auto score = model.score({.depth = 3.0, .breadth = 2.0, .volatility = 0.0, .switching = 0.0});

    std::cout << "Proteus skeleton initialized. Example reinforcement score: " << score << '\n';

    return 0;
}
