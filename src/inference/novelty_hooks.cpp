#include "proteus/inference/novelty_hooks.hpp"

#include <cmath>

namespace proteus::inference {
namespace {
double entropy(const std::vector<TargetScore>& scores) {
    double h = 0.0;
    for (const auto& s : scores) {
        if (s.posterior > 0.0) {
            h -= s.posterior * std::log2(s.posterior);
        }
    }
    return h;
}
}

NoveltyDetector::NoveltyDetector(NoveltyConfig config) : config_(config) {}

NoveltySignal NoveltyDetector::evaluate(
    const BeliefState& belief,
    std::size_t asked_questions,
    std::size_t idk_answers,
    std::size_t degenerate_recoveries
) const {
    NoveltySignal signal;

    const auto& distribution = belief.distribution();
    if (distribution.empty()) {
        signal.triggered = true;
        signal.high_entropy = true;
        return signal;
    }

    const auto top = belief.top_n(1).front().posterior;
    const auto h = entropy(distribution);
    const auto max_h = std::log2(static_cast<double>(distribution.size()));
    const auto normalized_h = max_h > 0.0 ? h / max_h : 0.0;

    const auto question_budget_hit = asked_questions >= config_.max_questions;
    signal.high_entropy = question_budget_hit && normalized_h >= config_.max_normalized_entropy;
    signal.weak_top_posterior = question_budget_hit && top < config_.min_top_posterior;
    signal.idk_overuse = idk_answers >= config_.max_idk_answers;
    signal.excessive_degenerate_recoveries = degenerate_recoveries >= config_.max_degenerate_recoveries;
    signal.triggered = signal.high_entropy || signal.weak_top_posterior || signal.idk_overuse ||
                       signal.excessive_degenerate_recoveries;

    return signal;
}

}  // namespace proteus::inference
