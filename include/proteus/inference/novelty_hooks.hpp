#pragma once

#include "proteus/inference/belief_state.hpp"

#include <cstddef>

namespace proteus::inference {

struct NoveltyConfig {
    std::size_t max_questions = 20;
    double min_top_posterior = 0.45;
    double max_normalized_entropy = 0.70;
    std::size_t max_idk_answers = 4;
};

struct NoveltySignal {
    bool triggered = false;
    bool high_entropy = false;
    bool weak_top_posterior = false;
    bool idk_overuse = false;
};

class NoveltyDetector {
public:
    explicit NoveltyDetector(NoveltyConfig config = {});

    NoveltySignal evaluate(const BeliefState& belief, std::size_t asked_questions, std::size_t idk_answers) const;

private:
    NoveltyConfig config_;
};

}  // namespace proteus::inference
