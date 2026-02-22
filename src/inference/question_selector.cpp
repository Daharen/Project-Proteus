#include "proteus/inference/question_selector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace proteus::inference {
namespace {
constexpr double kLikelihoodEpsilon = 1e-9;
}

std::string QuestionSelector::select_next_question(
    const BeliefState& belief,
    const content::ContentGraph& graph,
    const std::vector<std::string>& candidate_question_ids
) const {
    const auto& posterior = belief.distribution();
    std::vector<std::string> target_ids;
    target_ids.reserve(posterior.size());
    for (const auto& s : posterior) {
        target_ids.push_back(s.target_id);
    }

    double best_gain = -std::numeric_limits<double>::infinity();
    std::string best_question;

    std::vector<double> prior_probs;
    prior_probs.reserve(posterior.size());
    for (const auto& s : posterior) {
        prior_probs.push_back(s.posterior);
    }
    const auto prior_entropy = entropy(prior_probs);

    for (const auto& qid : candidate_question_ids) {
        double expected_posterior_entropy = 0.0;
        double answer_mass_total = 0.0;

        for (std::size_t ai = 0; ai < kTotalAnswerOptions; ++ai) {
            const auto answer = static_cast<AnswerOption>(ai);
            const auto raw_likelihoods = graph.get_likelihoods(qid, answer, target_ids);
            if (raw_likelihoods.size() != posterior.size()) {
                continue;
            }

            std::vector<double> weighted(posterior.size(), 0.0);
            double answer_mass = 0.0;
            for (std::size_t i = 0; i < posterior.size(); ++i) {
                const auto bounded = std::clamp(raw_likelihoods[i], 0.0, 1.0);
                const auto likelihood = std::max(bounded, kLikelihoodEpsilon);
                weighted[i] = posterior[i].posterior * likelihood;
                answer_mass += weighted[i];
            }

            if (answer_mass <= 0.0) {
                continue;
            }

            answer_mass_total += answer_mass;
            for (auto& value : weighted) {
                value /= answer_mass;
            }
            expected_posterior_entropy += answer_mass * entropy(weighted);
        }

        if (answer_mass_total <= 0.0) {
            continue;
        }

        // Renormalize by total modeled answer mass so sparse/partial answer support
        // does not create arbitrary scale effects across questions.
        expected_posterior_entropy /= answer_mass_total;
        const auto information_gain = prior_entropy - expected_posterior_entropy;
        if (information_gain > best_gain) {
            best_gain = information_gain;
            best_question = qid;
        }
    }

    return best_question;
}

double QuestionSelector::entropy(const std::vector<double>& distribution) const {
    double result = 0.0;
    for (const auto p : distribution) {
        if (p > 0.0) {
            result -= p * std::log2(p);
        }
    }
    return result;
}

}  // namespace proteus::inference
