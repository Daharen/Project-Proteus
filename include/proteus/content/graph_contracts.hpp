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
    virtual std::vector<double> get_likelihoods(
        const std::string& question_id,
        inference::AnswerOption answer,
        const std::vector<std::string>& target_ids
    ) const = 0;
};

}  // namespace proteus::content
