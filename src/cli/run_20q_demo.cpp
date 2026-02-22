#include "proteus/content/in_memory_graph.hpp"
#include "proteus/inference/novelty_hooks.hpp"
#include "proteus/inference/question_selector.hpp"
#include "proteus/inference/trace_log.hpp"

#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace {
using namespace proteus;

content::InMemoryContentGraph build_demo_graph() {
    content::InMemoryContentGraph graph;
    const std::string domain = "dark_magic";
    const std::vector<std::string> targets = {
        "blood_mage", "void_warlock", "dark_berserker", "shadow_executioner", "necromancer", "pyromancer"};
    graph.set_domain_targets(domain, targets);

    std::vector<inference::Question> questions = {
        {"q1", "Your power fantasy?", {"Overwhelming power", "Control", "Support", "Burst", "Sustain", "Trickery", "I don't know"}},
        {"q2", "Primary power source?", {"Blood", "Void", "Rage", "Shadows", "Death", "Flame", "I don't know"}},
        {"q3", "Preferred combat range?", {"Melee", "Close-mid", "Mid", "Long", "Flexible", "Summoner range", "I don't know"}},
        {"q4", "Risk appetite?", {"Self-sacrifice", "Calculated", "All-in", "Opportunistic", "Safe attrition", "Glass cannon", "I don't know"}},
        {"q5", "Sustain feel?", {"Lifesteal", "Mana weave", "Kills reset", "Cooldown dance", "Minion drain", "Burn chain", "I don't know"}},
        {"q6", "Emotional tone?", {"Noble pain", "Cosmic dread", "Fury", "Cold precision", "Macabre control", "Chaotic ecstasy", "I don't know"}},
        {"q7", "Weapon vs resource identity?", {"Resource-first", "Tome focus", "Weapon focus", "Daggers", "Staff/minions", "Catalyst", "I don't know"}},
        {"q8", "Where does tension live?", {"HP management", "Positioning", "Execution", "Stealth windows", "Army setup", "Combustion timing", "I don't know"}},
    };
    for (auto& q : questions) {
        q.idk_index = inference::kIdkIndex;
        graph.add_question(q);
    }

    auto setq = [&](const std::string& qid, const std::vector<std::vector<double>>& rows) {
        for (std::size_t a = 0; a < inference::kTotalAnswerOptions; ++a) {
            std::unordered_map<std::string, double> m;
            for (std::size_t t = 0; t < targets.size(); ++t) {
                m[targets[t]] = rows[a][t];
            }
            graph.set_likelihoods(qid, static_cast<inference::AnswerOption>(a), std::move(m));
        }
    };

    setq("q1", {{0.9,0.6,0.8,0.7,0.5,0.8},{0.5,0.9,0.4,0.7,0.8,0.4},{0.6,0.4,0.3,0.3,0.9,0.2},{0.7,0.6,0.8,0.9,0.4,0.8},{0.9,0.5,0.7,0.5,0.6,0.4},{0.5,0.8,0.5,0.9,0.6,0.7},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q2", {{0.95,0.1,0.2,0.2,0.2,0.1},{0.2,0.95,0.2,0.3,0.3,0.1},{0.3,0.2,0.95,0.2,0.2,0.1},{0.2,0.4,0.2,0.95,0.2,0.1},{0.3,0.3,0.2,0.2,0.95,0.1},{0.2,0.2,0.2,0.2,0.2,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q3", {{0.2,0.1,0.8,0.4,0.2,0.1},{0.6,0.4,0.7,0.7,0.5,0.2},{0.8,0.7,0.4,0.8,0.6,0.4},{0.7,0.9,0.2,0.6,0.8,0.9},{0.6,0.7,0.6,0.8,0.6,0.6},{0.5,0.6,0.1,0.2,0.95,0.3},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q4", {{0.95,0.4,0.8,0.5,0.4,0.6},{0.6,0.9,0.5,0.8,0.7,0.5},{0.8,0.5,0.95,0.7,0.3,0.9},{0.7,0.7,0.8,0.95,0.4,0.8},{0.4,0.8,0.2,0.4,0.95,0.3},{0.7,0.8,0.8,0.8,0.4,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q5", {{0.95,0.2,0.5,0.3,0.6,0.2},{0.4,0.9,0.3,0.5,0.4,0.5},{0.5,0.5,0.9,0.7,0.3,0.6},{0.5,0.7,0.5,0.95,0.4,0.7},{0.6,0.5,0.3,0.3,0.95,0.2},{0.4,0.8,0.7,0.7,0.2,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q6", {{0.95,0.5,0.3,0.5,0.4,0.2},{0.4,0.95,0.2,0.6,0.5,0.3},{0.4,0.2,0.95,0.3,0.2,0.7},{0.6,0.6,0.5,0.95,0.4,0.4},{0.5,0.5,0.2,0.4,0.95,0.2},{0.3,0.5,0.8,0.4,0.2,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q7", {{0.95,0.5,0.3,0.3,0.4,0.6},{0.4,0.95,0.2,0.3,0.6,0.5},{0.3,0.2,0.95,0.4,0.2,0.4},{0.4,0.4,0.5,0.95,0.2,0.3},{0.3,0.5,0.2,0.2,0.95,0.3},{0.5,0.6,0.3,0.2,0.3,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});
    setq("q8", {{0.95,0.4,0.6,0.3,0.4,0.2},{0.4,0.8,0.4,0.6,0.6,0.5},{0.6,0.5,0.95,0.7,0.3,0.7},{0.5,0.7,0.8,0.95,0.4,0.8},{0.4,0.5,0.2,0.3,0.95,0.3},{0.3,0.7,0.7,0.7,0.2,0.95},{0.2,0.2,0.2,0.2,0.2,0.2}});

    return graph;
}
}  // namespace

int main() {
    const std::string domain = "dark_magic";
    auto graph = build_demo_graph();
    const auto targets = graph.get_candidate_targets(domain);
    std::vector<inference::TargetScore> priors;
    priors.reserve(targets.size());
    for (const auto& t : targets) {
        priors.push_back({t, 1.0});
    }

    inference::BeliefState belief(std::move(priors));
    inference::QuestionSelector selector;
    inference::NoveltyDetector novelty({.max_questions = 10, .min_top_posterior = 0.55, .max_normalized_entropy = 0.65, .max_idk_answers = 3, .max_degenerate_recoveries = 2});
    inference::InferenceTraceLog trace{.session_id = "demo-session"};

    std::vector<std::string> candidate_questions = {"q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8"};
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

    std::cout << "\nPrimary: " << trace.final_primary_target << "\n";
    std::cout << "Backups:\n";
    for (const auto& b : trace.final_backup_targets) {
        std::cout << "  - " << b << "\n";
    }
    std::cout << "Novelty triggered: " << (trace.novelty_triggered ? "yes" : "no") << "\n";

    return 0;
}
