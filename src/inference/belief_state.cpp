#include "proteus/inference/belief_state.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace proteus::inference {

BeliefState::BeliefState(std::vector<TargetScore> priors) : posteriors_(std::move(priors)) {
    normalize();
}

void BeliefState::update(
    const std::string& /*question_id*/,
    AnswerOption /*answer*/,
    const std::vector<double>& likelihoods
) {
    if (likelihoods.size() != posteriors_.size()) {
        throw std::invalid_argument("likelihood size mismatch");
    }

    for (std::size_t i = 0; i < posteriors_.size(); ++i) {
        posteriors_[i].posterior *= likelihoods[i];
    }
    normalize();
}

std::vector<TargetScore> BeliefState::top_n(std::size_t n) const {
    auto sorted = posteriors_;
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.posterior > b.posterior; });
    if (n < sorted.size()) {
        sorted.resize(n);
    }
    return sorted;
}

void BeliefState::normalize() {
    const auto total = std::accumulate(
        posteriors_.begin(),
        posteriors_.end(),
        0.0,
        [](double acc, const auto& item) { return acc + item.posterior; }
    );

    if (total <= 0.0) {
        throw std::invalid_argument("posterior mass must be positive");
    }

    for (auto& item : posteriors_) {
        item.posterior /= total;
    }
}

}  // namespace proteus::inference
