#include "proteus/inference/belief_state.hpp"
#include "proteus/simulation/entity.hpp"

#include <iostream>

int main() {
    proteus::simulation::ReinforcementModel model;
    const auto score = model.score(3.0, 2.0);

    std::cout << "Proteus skeleton initialized. Example reinforcement score: " << score << '\n';

    return 0;
}
