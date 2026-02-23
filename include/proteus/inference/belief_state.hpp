#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace proteus::inference {

enum class AnswerOption : std::size_t {
    Option1 = 0,
    Option2,
    Option3,
    Option4,
    Option5,
    Option6,
    Unknown,
};

constexpr std::size_t kTotalAnswerOptions = 7;
constexpr std::size_t kIdkIndex = 6;

struct Question {
    std::string id;
    std::string prompt;
    std::array<std::string, kTotalAnswerOptions> options;
    std::size_t idk_index = kIdkIndex;
};

struct TargetScore {
    std::string target_id;
    double posterior;
};

enum class BeliefUpdateStatus {
    Normal,
    RecoveredFromDegenerateMass,
};

class BeliefState {
public:
    // Priors are normalized on construction and stored as the fallback distribution.
    explicit BeliefState(std::vector<TargetScore> priors);

    BeliefUpdateStatus update(const std::vector<double>& likelihoods_for_selected_answer);

    std::vector<TargetScore> top_n(std::size_t n) const;
    const std::vector<TargetScore>& distribution() const;

private:
    void normalize();
    void reset_to_priors();

    std::vector<TargetScore> priors_;
    std::vector<TargetScore> posteriors_;
};

}  // namespace proteus::inference
