#include "proteus/inference/belief_state.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace proteus::inference {
namespace {
constexpr double kLikelihoodEpsilon = 1e-9;
constexpr double kMassEpsilon = 1e-12;
}

BeliefState::BeliefState(std::vector<TargetScore> priors) : priors_(std::move(priors)), posteriors_(priors_) {
    normalize();
    priors_ = posteriors_;
}

BeliefUpdateStatus BeliefState::update(const std::vector<double>& likelihoods_for_selected_answer) {
    if (likelihoods_for_selected_answer.size() != posteriors_.size()) {
        throw std::invalid_argument("likelihood size mismatch");
    }

    const auto all_non_positive = std::ranges::all_of(
        likelihoods_for_selected_answer,
        [](double likelihood) { return likelihood <= 0.0; }
    );
    if (all_non_positive) {
        reset_to_priors();
        return BeliefUpdateStatus::RecoveredFromDegenerateMass;
    }

    for (std::size_t i = 0; i < posteriors_.size(); ++i) {
        const auto likelihood = std::max(likelihoods_for_selected_answer[i], kLikelihoodEpsilon);
        posteriors_[i].posterior *= likelihood;
    }

    const auto total = std::accumulate(
        posteriors_.begin(),
        posteriors_.end(),
        0.0,
        [](double acc, const auto& item) { return acc + item.posterior; }
    );

    if (total <= kMassEpsilon) {
        reset_to_priors();
        return BeliefUpdateStatus::RecoveredFromDegenerateMass;
    }

    normalize();
    return BeliefUpdateStatus::Normal;
}

std::vector<TargetScore> BeliefState::top_n(std::size_t n) const {
    auto sorted = posteriors_;
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.posterior > b.posterior; });
    if (n < sorted.size()) {
        sorted.resize(n);
    }
    return sorted;
}

const std::vector<TargetScore>& BeliefState::distribution() const {
    return posteriors_;
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

void BeliefState::reset_to_priors() {
    posteriors_ = priors_;
    normalize();
}

}  // namespace proteus::inference
