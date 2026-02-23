#pragma once

#include "proteus/inference/belief_state.hpp"

#include <string>
#include <vector>

namespace proteus::content {

struct GraphNode {
    std::string id;
    std::string type;
};

struct GraphEdge {
    std::string source_id;
    std::string target_id;
    double weight = 0.0;
    std::string relation;
};

class ContentGraph {
public:
    virtual ~ContentGraph() = default;

    virtual std::vector<GraphNode> nearest_targets(const std::string& node_id, std::size_t k) const = 0;
    virtual inference::Question get_question(const std::string& question_id) const = 0;
    virtual std::vector<std::string> get_candidate_targets(const std::string& domain) const = 0;
    // Contract for likelihood semantics:
    // - Questions must have exactly 7 options where index 6 is IDK/Unknown (`inference::kIdkIndex`).
    // - Returns L[t] for each target t in `target_ids`, where L[t] is P(answer=a | target=t, question=Q).
    // - Implementations should guarantee finite values and an epsilon floor (strictly > 0).
    // - Normalization is per-target across answers for the same question:
    //     sum_a P(answer=a | target=t, question=Q) = 1
    //   This method returns one answer slice from that table.
    virtual std::vector<double> get_likelihoods(
        const std::string& question_id,
        inference::AnswerOption answer,
        const std::vector<std::string>& target_ids
    ) const = 0;
};

}  // namespace proteus::content
