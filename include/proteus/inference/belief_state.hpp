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
    Option7,
    Unknown,
};

struct Question {
    std::string id;
    std::string prompt;
    std::array<std::string, 8> options;
};

struct TargetScore {
    std::string target_id;
    double posterior;
};

class BeliefState {
public:
    explicit BeliefState(std::vector<TargetScore> priors);

    void update(const std::string& question_id, AnswerOption answer, const std::vector<double>& likelihoods);

    std::vector<TargetScore> top_n(std::size_t n) const;

private:
    void normalize();
    std::vector<TargetScore> posteriors_;
};

}  // namespace proteus::inference
