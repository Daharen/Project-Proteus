#pragma once

#include "proteus/content/graph_contracts.hpp"

#include <string>
#include <vector>

namespace proteus::inference {

class QuestionSelector {
public:
    std::string select_next_question(
        const BeliefState& belief,
        const content::ContentGraph& graph,
        const std::vector<std::string>& candidate_question_ids
    ) const;

private:
    double entropy(const std::vector<double>& distribution) const;
};

}  // namespace proteus::inference
