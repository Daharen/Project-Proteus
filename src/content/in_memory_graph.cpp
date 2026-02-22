#include "proteus/content/in_memory_graph.hpp"

#include <algorithm>
#include <stdexcept>

namespace proteus::content {

void InMemoryContentGraph::add_question(inference::Question question) {
    questions_.emplace(question.id, std::move(question));
}

void InMemoryContentGraph::set_domain_targets(const std::string& domain, std::vector<std::string> targets) {
    for (const auto& target : targets) {
        domain_by_target_[target] = domain;
    }
    targets_by_domain_[domain] = std::move(targets);
}

void InMemoryContentGraph::set_likelihoods(
    const std::string& question_id,
    inference::AnswerOption answer,
    std::unordered_map<std::string, double> target_likelihoods
) {
    likelihoods_[question_id][static_cast<std::size_t>(answer)] = std::move(target_likelihoods);
}

void InMemoryContentGraph::add_similarity(const std::string& source_id, const std::string& target_id, double weight) {
    similarity_[source_id].push_back({target_id, weight});
}

std::vector<GraphNode> InMemoryContentGraph::nearest_targets(const std::string& node_id, std::size_t k) const {
    std::vector<GraphNode> out;
    const auto it = similarity_.find(node_id);
    if (it == similarity_.end()) {
        return out;
    }

    auto neighbors = it->second;
    std::ranges::sort(neighbors, [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < std::min(k, neighbors.size()); ++i) {
        out.push_back(GraphNode{.id = neighbors[i].first, .type = "target"});
    }
    return out;
}

inference::Question InMemoryContentGraph::get_question(const std::string& question_id) const {
    const auto it = questions_.find(question_id);
    if (it == questions_.end()) {
        throw std::invalid_argument("unknown question_id");
    }
    return it->second;
}

std::vector<std::string> InMemoryContentGraph::get_candidate_targets(const std::string& domain) const {
    const auto it = targets_by_domain_.find(domain);
    if (it == targets_by_domain_.end()) {
        return {};
    }
    return it->second;
}

std::vector<double> InMemoryContentGraph::get_likelihoods(
    const std::string& question_id,
    inference::AnswerOption answer,
    const std::vector<std::string>& target_ids
) const {
    std::vector<double> out;
    out.reserve(target_ids.size());

    const auto qit = likelihoods_.find(question_id);
    if (qit == likelihoods_.end()) {
        return std::vector<double>(target_ids.size(), 0.0);
    }

    const auto& answer_map = qit->second[static_cast<std::size_t>(answer)];
    for (const auto& target_id : target_ids) {
        const auto it = answer_map.find(target_id);
        out.push_back(it == answer_map.end() ? 0.0 : std::clamp(it->second, 0.0, 1.0));
    }

    return out;
}

}  // namespace proteus::content
