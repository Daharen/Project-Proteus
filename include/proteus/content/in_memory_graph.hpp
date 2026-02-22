#pragma once

#include "proteus/content/graph_contracts.hpp"

#include <array>
#include <unordered_map>

namespace proteus::content {

class InMemoryContentGraph final : public ContentGraph {
public:
    void add_question(inference::Question question);
    void set_domain_targets(const std::string& domain, std::vector<std::string> targets);
    void set_likelihoods(
        const std::string& question_id,
        inference::AnswerOption answer,
        std::unordered_map<std::string, double> target_likelihoods
    );
    void add_similarity(const std::string& source_id, const std::string& target_id, double weight);

    std::vector<GraphNode> nearest_targets(const std::string& node_id, std::size_t k) const override;
    inference::Question get_question(const std::string& question_id) const override;
    std::vector<std::string> get_candidate_targets(const std::string& domain) const override;
    std::vector<double> get_likelihoods(
        const std::string& question_id,
        inference::AnswerOption answer,
        const std::vector<std::string>& target_ids
    ) const override;

private:
    std::unordered_map<std::string, inference::Question> questions_;
    std::unordered_map<std::string, std::vector<std::string>> targets_by_domain_;
    std::unordered_map<std::string, std::string> domain_by_target_;
    std::unordered_map<std::string, std::array<std::unordered_map<std::string, double>, inference::kTotalAnswerOptions>>
        likelihoods_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> similarity_;
};

}  // namespace proteus::content
