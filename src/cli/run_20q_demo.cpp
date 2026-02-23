#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/content/in_memory_graph.hpp"
#include "proteus/inference/identity.hpp"
#include "proteus/inference/novelty_hooks.hpp"
#include "proteus/inference/question_selector.hpp"
#include "proteus/inference/trace_log.hpp"

#include <iostream>
#include <unordered_map>
#include <unordered_set>

int main() {
    using namespace proteus;

    const std::string domain = "identity";
    content::InMemoryContentGraph graph;
    graph.seed_identity_v1_domain();

    const auto validation = graph.validate_likelihood_tables(domain);
    if (!validation.hard_violations.empty()) {
        std::cerr << "Likelihood validation hard violations:\n";
        for (const auto& issue : validation.hard_violations) {
            std::cerr << "  - " << issue.question_id << ": " << issue.message << "\n";
        }
        return 1;
    }
    if (!validation.warnings.empty()) {
        std::cerr << "Likelihood validation warnings:\n";
        for (const auto& issue : validation.warnings) {
            std::cerr << "  - " << issue.question_id << ": " << issue.message << "\n";
        }
    }

    std::cout << "Validation debug (expected information gain in bits):\n";
    for (const auto& [qid, ig] : validation.information_gain_bits_by_question) {
        std::cout << "  - " << qid << ": " << ig << "\n";
    }

    const auto targets = graph.get_candidate_targets(domain);
    std::vector<inference::TargetScore> priors;
    priors.reserve(targets.size());
    for (const auto& t : targets) {
        priors.push_back({t, 1.0});
    }

    inference::BeliefState belief(std::move(priors));
    inference::QuestionSelector selector;
    inference::NoveltyDetector novelty({.max_questions = 10, .min_top_posterior = 0.55, .max_normalized_entropy = 0.65, .max_idk_answers = 3, .max_degenerate_recoveries = 2});
    inference::InferenceTraceLog trace{.session_id = "identity-demo-session"};

    const auto candidate_questions = graph.get_domain_questions(domain);
    std::unordered_set<std::string> asked;

    std::size_t idk_answers = 0;
    std::size_t degenerate_recoveries = 0;

    while (true) {
        std::vector<std::string> remaining;
        for (const auto& q : candidate_questions) {
            if (!asked.contains(q)) {
                remaining.push_back(q);
            }
        }
        if (remaining.empty()) {
            break;
        }

        const auto next_qid = selector.select_next_question(belief, graph, remaining);
        if (next_qid.empty()) {
            break;
        }

        const auto question = graph.get_question(next_qid);
        asked.insert(next_qid);

        std::cout << "\nQ(" << asked.size() << "): " << question.prompt << "\n";
        for (std::size_t i = 0; i < question.options.size(); ++i) {
            std::cout << "  " << (i + 1) << ") " << question.options[i] << "\n";
        }
        std::cout << "Select [1-7]: ";

        std::size_t input = 7;
        std::cin >> input;
        if (input < 1 || input > 7) {
            input = 7;
        }
        const auto answer = static_cast<inference::AnswerOption>(input - 1);
        if (static_cast<std::size_t>(answer) == question.idk_index) {
            ++idk_answers;
        }

        const auto likelihoods = graph.get_likelihoods(next_qid, answer, targets);
        const auto status = belief.update(likelihoods);
        if (status == inference::BeliefUpdateStatus::RecoveredFromDegenerateMass) {
            ++degenerate_recoveries;
        }

        trace.asked_questions.push_back({.question_id = next_qid, .answer = answer, .time_to_answer_seconds = std::nullopt});

        const auto top = belief.top_n(1).front();
        const auto signal = novelty.evaluate(belief, asked.size(), idk_answers, degenerate_recoveries);
        if (top.posterior >= 0.75 || asked.size() >= 10 || signal.triggered) {
            trace.novelty_triggered = signal.triggered;
            break;
        }
    }

    const auto picks = belief.top_n(4);
    trace.final_primary_target = picks.empty() ? "" : picks[0].target_id;
    for (std::size_t i = 1; i < picks.size(); ++i) {
        trace.final_backup_targets.push_back(picks[i].target_id);
    }

    std::unordered_map<std::string, inference::IdentityArchetype> archetype_map;
    for (const auto& archetype : graph.get_identity_archetypes()) {
        archetype_map.emplace(archetype.id, archetype);
    }

    const auto novelty_signal = novelty.evaluate(belief, asked.size(), idk_answers, degenerate_recoveries);
    const auto result = inference::build_identity_result(belief.distribution(), archetype_map, novelty_signal, 4);


    bandits::PlayerContext player_context{
        .identity_axes = result.derived_axes,
        .identity_confidence = static_cast<float>(result.confidence.top_posterior_strength),
        .identity_entropy = static_cast<float>(result.confidence.normalized_entropy),
        .questions_answered = static_cast<std::uint32_t>(asked.size()),
        .idk_rate = asked.empty() ? 0.0F : static_cast<float>(idk_answers) / static_cast<float>(asked.size()),
        .session_id = 0,
        .niche_id = 0,
    };

    std::cout << "\nPrimary archetype: " << trace.final_primary_target << "\n";
    std::cout << "Backups:\n";
    for (const auto& b : trace.final_backup_targets) {
        std::cout << "  - " << b << "\n";
    }

    std::cout << "\nDerived identity axes [-1,+1]:\n";
    for (std::size_t i = 0; i < result.derived_axes.size(); ++i) {
        std::cout << "  - " << inference::axis_name(static_cast<inference::IdentityAxis>(i)) << ": " << result.derived_axes[i] << "\n";
    }

    std::cout << "\nConfidence:\n";
    std::cout << "  - top_posterior_strength: " << result.confidence.top_posterior_strength << "\n";
    std::cout << "  - normalized_entropy: " << result.confidence.normalized_entropy << "\n";
    std::cout << "  - novelty_triggered: " << (trace.novelty_triggered ? "yes" : "no") << "\n";

    std::cout << "\nPlayerContext v1:\n";
    std::cout << "  - identity_confidence: " << player_context.identity_confidence << "\n";
    std::cout << "  - identity_entropy: " << player_context.identity_entropy << "\n";
    std::cout << "  - questions_answered: " << player_context.questions_answered << "\n";
    std::cout << "  - idk_rate: " << player_context.idk_rate << "\n";
    std::cout << "  - session_id: " << player_context.session_id << "\n";
    std::cout << "  - niche_id: " << player_context.niche_id << "\n";

    return 0;
}
