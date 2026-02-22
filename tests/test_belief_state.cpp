#include "proteus/inference/belief_state.hpp"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using proteus::inference::BeliefState;
    using proteus::inference::TargetScore;

    BeliefState belief({
        TargetScore{"tank", 0.5},
        TargetScore{"dps", 0.3},
        TargetScore{"support", 0.2},
    });

    belief.update("q1", proteus::inference::AnswerOption::Option1, {0.2, 0.5, 0.3});

    const auto top = belief.top_n(2);
    if (top.size() != 2 || top[0].target_id != "dps") {
        std::cerr << "unexpected ranking\n";
        return 1;
    }

    const auto total = top[0].posterior + top[1].posterior;
    if (!std::isfinite(total)) {
        std::cerr << "non-finite posterior\n";
        return 1;
    }

    return 0;
}
