#include "proteus/content/in_memory_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace proteus::content {
namespace {
constexpr std::size_t idx(inference::IdentityAxis axis) {
    return static_cast<std::size_t>(axis);
}

inference::AxisVector axis_profile(
    std::initializer_list<std::pair<inference::IdentityAxis, float>> values
) {
    inference::AxisVector profile{};
    for (const auto& [axis, value] : values) {
        profile[idx(axis)] = value;
    }
    return profile;
}

double entropy_bits(const std::vector<double>& distribution) {
    double h = 0.0;
    for (const auto value : distribution) {
        if (value > 0.0) {
            h -= value * std::log2(value);
        }
    }
    return h;
}

double cosine_similarity(const std::vector<double>& a, const std::vector<double>& b) {
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 0.0 || nb <= 0.0) {
        return 1.0;
    }
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

struct AuthoredIdentityQuestion {
    inference::Question question;
    std::array<inference::AxisVector, 6> answer_profiles;
};

std::vector<AuthoredIdentityQuestion> identity_questions_v1() {
    using inference::IdentityAxis;
    return {
        {{"identity_q01", "A new zone opens. What is your first move?", {"Set a route and objective", "Follow whatever looks interesting", "Hunt the hardest encounter", "Read the questline setup first", "Find what can be exploited early", "Find players and coordinate", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::ExplorationDrive, -1.0F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::NarrativePreference, -0.8F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::MoralOrientation, 0.5F}}), axis_profile({{IdentityAxis::RiskPosture, 1.0F}, {IdentityAxis::MoralOrientation, -1.0F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}})}},
        {{"identity_q02", "When plans break mid-fight, you usually...", {"Stick to preplanned contingencies", "Improvise from moment to moment", "Double down and push damage", "Stabilize and protect the team", "Look for a clever shortcut", "Reset and re-approach safely", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::RiskPosture, -0.5F}}), axis_profile({{IdentityAxis::AgencyStyle, -1.0F}, {IdentityAxis::ExplorationDrive, 0.7F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 1.0F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.8F}}), axis_profile({{IdentityAxis::SystemAppetite, 0.8F}, {IdentityAxis::RiskPosture, 0.4F}}), axis_profile({{IdentityAxis::RiskPosture, -1.0F}, {IdentityAxis::ChallengeAppetite, -0.6F}})}},
        {{"identity_q03", "What kind of progression feels best?", {"Optimizing builds and breakpoints", "Collecting story moments", "Unlocking harder modes quickly", "Trying strange combos", "Steady reliable upgrades", "Power that supports allies", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::ExplorationDrive, -0.6F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::SystemAppetite, -0.8F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::AgencyStyle, 0.6F}}), axis_profile({{IdentityAxis::SystemAppetite, 0.9F}, {IdentityAxis::ExplorationDrive, 0.8F}}), axis_profile({{IdentityAxis::ChallengeAppetite, -0.7F}, {IdentityAxis::RiskPosture, -0.7F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}})}},
        {{"identity_q04", "A quest offers a risky shortcut. You...", {"Take it for speed", "Avoid it; prefer certainty", "Only if reward is huge", "Do it if team agrees", "Do it if it fits the story", "Exploit it if rules allow", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::RiskPosture, 0.8F}, {IdentityAxis::AgencyStyle, 0.8F}}), axis_profile({{IdentityAxis::RiskPosture, -1.0F}, {IdentityAxis::ChallengeAppetite, -0.6F}}), axis_profile({{IdentityAxis::RiskPosture, 1.0F}, {IdentityAxis::MoralOrientation, -0.6F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::RiskPosture, -0.2F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}}), axis_profile({{IdentityAxis::MoralOrientation, -1.0F}, {IdentityAxis::SystemAppetite, 0.8F}})}},
        {{"identity_q05", "In downtime, you prefer to...", {"Route farm/material loops", "Talk with NPCs and lore", "Scout unexplored map edges", "Run tests on mechanics", "Queue content with friends", "Take a break until needed", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::ExplorationDrive, -0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::SystemAppetite, -0.8F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::NarrativePreference, -0.5F}}), axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::RiskPosture, 0.5F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::ChallengeAppetite, -0.2F}}), axis_profile({{IdentityAxis::AgencyStyle, -0.9F}, {IdentityAxis::ChallengeAppetite, -0.5F}})}},
        {{"identity_q06", "Which reward motivates you most?", {"Unique tactical utility", "Lore revelations", "Proof you beat hard content", "High-variance jackpot drops", "Teamwide unlocks", "Clean consistent efficiency", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::AgencyStyle, 0.6F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::MoralOrientation, 0.4F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 0.5F}}), axis_profile({{IdentityAxis::RiskPosture, 1.0F}, {IdentityAxis::ExplorationDrive, 0.5F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.8F}}), axis_profile({{IdentityAxis::ExplorationDrive, -0.9F}, {IdentityAxis::SystemAppetite, 0.6F}})}},
        {{"identity_q07", "How do you handle moral grey choices?", {"Follow principle even if costly", "Choose what keeps momentum", "Pick whichever helps allies", "Pick whichever opens more paths", "Pick what the authored arc implies", "Pick what maximizes systems gain", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::MoralOrientation, 1.0F}, {IdentityAxis::RiskPosture, -0.3F}}), axis_profile({{IdentityAxis::MoralOrientation, -1.0F}, {IdentityAxis::AgencyStyle, 0.8F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.8F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::NarrativePreference, -0.7F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::MoralOrientation, 0.5F}}), axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::MoralOrientation, -0.8F}})}},
        {{"identity_q08", "What role sounds most natural?", {"Shot-caller strategist", "Frontline pressure dealer", "Safety net protector", "Flexible wildcard", "Independent specialist", "Story-facing envoy", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::SystemAppetite, 0.8F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 0.8F}, {IdentityAxis::RiskPosture, 0.6F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.9F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::RiskPosture, 0.6F}}), axis_profile({{IdentityAxis::SocialPosture, -1.0F}, {IdentityAxis::ChallengeAppetite, 0.5F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::SocialPosture, 0.7F}})}},
        {{"identity_q09", "How much authored guidance do you want?", {"Strong chapters and arcs", "Loose framing, then sandbox", "Almost none; let systems drive", "Guidance only for group goals", "Guidance for hard encounters", "I skip guidance and self-direct", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::ExplorationDrive, -0.6F}}), axis_profile({{IdentityAxis::NarrativePreference, -0.2F}, {IdentityAxis::ExplorationDrive, 0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, -1.0F}, {IdentityAxis::SystemAppetite, 1.0F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::NarrativePreference, 0.5F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::NarrativePreference, 0.4F}}), axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::NarrativePreference, -0.8F}})}},
        {{"identity_q10", "Before a dungeon, you invest most time in...", {"Route and timing plan", "Learning encounter story context", "Practicing execution loops", "Exploring alternate entrances", "Comms and team composition", "Building fail-safe contingencies", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::SystemAppetite, 0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::SystemAppetite, -0.6F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::SystemAppetite, 0.7F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::RiskPosture, 0.4F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::AgencyStyle, 0.3F}}), axis_profile({{IdentityAxis::RiskPosture, -1.0F}, {IdentityAxis::AgencyStyle, 0.7F}})}},
        {{"identity_q11", "Your favorite success feeling is...", {"Everything went to plan", "I adapted through chaos", "I survived real danger", "I uncovered hidden content", "We succeeded together", "I honored my character values", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::RiskPosture, -0.6F}}), axis_profile({{IdentityAxis::AgencyStyle, -1.0F}, {IdentityAxis::RiskPosture, 0.6F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 1.0F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::NarrativePreference, -0.5F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}}), axis_profile({{IdentityAxis::MoralOrientation, 1.0F}, {IdentityAxis::NarrativePreference, 0.8F}})}},
        {{"identity_q12", "When a build gets nerfed, you...", {"Rebuild optimally right away", "Stick with it for fantasy", "Switch to harder challenge anyway", "Try weird alternatives", "Adopt what helps group comp", "Wait and see before changing", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::AgencyStyle, 0.8F}}), axis_profile({{IdentityAxis::SystemAppetite, -1.0F}, {IdentityAxis::NarrativePreference, 0.8F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 0.5F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::SystemAppetite, 0.7F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::SystemAppetite, 0.4F}}), axis_profile({{IdentityAxis::RiskPosture, -1.0F}, {IdentityAxis::ChallengeAppetite, -0.5F}})}},
        {{"identity_q13", "How do you prefer pacing?", {"Fast and efficient", "Slow and atmospheric", "Escalating difficulty spikes", "Open-ended meandering", "Steady shared progression", "Controlled low-risk cadence", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::ExplorationDrive, -1.0F}, {IdentityAxis::SystemAppetite, 0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::ChallengeAppetite, -0.6F}}), axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::RiskPosture, 0.8F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::AgencyStyle, -0.5F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::ChallengeAppetite, -0.4F}}), axis_profile({{IdentityAxis::RiskPosture, -1.0F}, {IdentityAxis::ExplorationDrive, -0.6F}})}},
        {{"identity_q14", "Which failure bothers you most?", {"Bad planning", "Missing hidden opportunities", "Not mastering mechanics", "Breaking story consistency", "Letting teammates down", "Compromising your principles", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::AgencyStyle, 1.0F}, {IdentityAxis::ExplorationDrive, -0.7F}}), axis_profile({{IdentityAxis::ExplorationDrive, 1.0F}, {IdentityAxis::NarrativePreference, -0.5F}}), axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::ChallengeAppetite, 0.8F}}), axis_profile({{IdentityAxis::NarrativePreference, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.8F}}), axis_profile({{IdentityAxis::MoralOrientation, 1.0F}, {IdentityAxis::NarrativePreference, 0.5F}})}},
        {{"identity_q15", "You discover an exploit that trivializes content. You...", {"Avoid it to preserve challenge", "Use it for speed and efficiency", "Use it if everyone consents", "Use it if it creates funny stories", "Report it and move on", "Use it until patched", "Not sure / depends"}, inference::kIdkIndex}, {axis_profile({{IdentityAxis::ChallengeAppetite, 1.0F}, {IdentityAxis::MoralOrientation, 0.6F}}), axis_profile({{IdentityAxis::SystemAppetite, 1.0F}, {IdentityAxis::MoralOrientation, -0.8F}}), axis_profile({{IdentityAxis::SocialPosture, 1.0F}, {IdentityAxis::MoralOrientation, 0.7F}}), axis_profile({{IdentityAxis::NarrativePreference, -0.8F}, {IdentityAxis::ExplorationDrive, 0.8F}}), axis_profile({{IdentityAxis::MoralOrientation, 1.0F}, {IdentityAxis::RiskPosture, -0.2F}}), axis_profile({{IdentityAxis::RiskPosture, 1.0F}, {IdentityAxis::MoralOrientation, -1.0F}})}}};
}

}  // namespace

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

void InMemoryContentGraph::seed_identity_v1_domain() {
    const std::string domain = "identity";
    auto archetypes = inference::identity_archetypes_v1();
    identity_archetypes_.clear();

    std::vector<std::string> target_ids;
    target_ids.reserve(archetypes.size());
    for (const auto& archetype : archetypes) {
        identity_archetypes_.emplace(archetype.id, archetype);
        target_ids.push_back(archetype.id);
    }

    set_domain_targets(domain, target_ids);

    const auto authored_questions = identity_questions_v1();
    questions_by_domain_[domain].clear();

    constexpr double kEpsilon = 1e-6;

    for (const auto& authored : authored_questions) {
        add_question(authored.question);
        questions_by_domain_[domain].push_back(authored.question.id);

        std::vector<std::vector<double>> table(
            inference::kTotalAnswerOptions,
            std::vector<double>(archetypes.size(), kEpsilon)
        );

        for (std::size_t answer_index = 0; answer_index < inference::kIdkIndex; ++answer_index) {
            const auto& profile = authored.answer_profiles[answer_index];
            for (std::size_t i = 0; i < archetypes.size(); ++i) {
                double dot = 0.0;
                for (std::size_t axis = 0; axis < inference::kIdentityAxisCount; ++axis) {
                    dot += static_cast<double>(profile[axis] * archetypes[i].axes[axis]);
                }
                table[answer_index][i] += std::exp(1.25 * dot);
            }
        }

        for (std::size_t i = 0; i < archetypes.size(); ++i) {
            // Keep IDK low-impact and near-uniform by anchoring a fixed floor before per-target normalization.
            table[inference::kIdkIndex][i] += 0.35 + 0.002 * static_cast<double>(i % 2);

            double sum = 0.0;
            for (std::size_t answer_index = 0; answer_index < inference::kTotalAnswerOptions; ++answer_index) {
                sum += table[answer_index][i];
            }
            for (std::size_t answer_index = 0; answer_index < inference::kTotalAnswerOptions; ++answer_index) {
                table[answer_index][i] /= sum;
            }
        }

        for (std::size_t answer_index = 0; answer_index < inference::kTotalAnswerOptions; ++answer_index) {
            std::unordered_map<std::string, double> likelihood_by_target;
            for (std::size_t i = 0; i < archetypes.size(); ++i) {
                likelihood_by_target[archetypes[i].id] = table[answer_index][i];
            }
            set_likelihoods(authored.question.id, static_cast<inference::AnswerOption>(answer_index), std::move(likelihood_by_target));
        }
    }
}

std::vector<inference::IdentityArchetype> InMemoryContentGraph::get_identity_archetypes() const {
    std::vector<inference::IdentityArchetype> archetypes;
    archetypes.reserve(identity_archetypes_.size());
    for (const auto& [_, archetype] : identity_archetypes_) {
        archetypes.push_back(archetype);
    }
    std::ranges::sort(archetypes, [](const auto& a, const auto& b) { return a.id < b.id; });
    return archetypes;
}

std::vector<std::string> InMemoryContentGraph::get_domain_questions(const std::string& domain) const {
    const auto it = questions_by_domain_.find(domain);
    if (it == questions_by_domain_.end()) {
        return {};
    }
    return it->second;
}

InMemoryContentGraph::LikelihoodValidationReport InMemoryContentGraph::validate_likelihood_tables(const std::string& domain) const {
    constexpr double kEpsilon = 1e-6;
    constexpr double kSubstantiveMinRatio = 2.0;
    constexpr double kAnswerDuplicateCosineMax = 0.995;
    constexpr double kIdkMaxKlBits = 0.02;
    constexpr double kQuestionMinIgBits = 0.02;

    LikelihoodValidationReport report;

    const auto domain_it = targets_by_domain_.find(domain);
    if (domain_it == targets_by_domain_.end()) {
        report.ok = false;
        report.hard_violations.push_back({"<domain>", "domain has no targets", ValidationSeverity::HardViolation});
        return report;
    }

    const auto qit = questions_by_domain_.find(domain);
    if (qit == questions_by_domain_.end() || qit->second.empty()) {
        report.ok = false;
        report.hard_violations.push_back({"<domain>", "domain has no questions", ValidationSeverity::HardViolation});
        return report;
    }

    const auto& targets = domain_it->second;
    const auto uniform_prior = std::vector<double>(targets.size(), 1.0 / static_cast<double>(targets.size()));
    const auto prior_entropy = entropy_bits(uniform_prior);

    for (const auto& qid : qit->second) {
        std::vector<std::vector<double>> answer_rows;
        answer_rows.reserve(inference::kTotalAnswerOptions);

        for (std::size_t answer_index = 0; answer_index < inference::kTotalAnswerOptions; ++answer_index) {
            const auto likelihoods = get_likelihoods(qid, static_cast<inference::AnswerOption>(answer_index), targets);
            if (likelihoods.size() != targets.size()) {
                report.ok = false;
                report.hard_violations.push_back({qid, "wrong likelihood vector length", ValidationSeverity::HardViolation});
                continue;
            }

            for (const auto value : likelihoods) {
                if (!std::isfinite(value)) {
                    report.ok = false;
                    report.hard_violations.push_back({qid, "contains NaN or Inf likelihood", ValidationSeverity::HardViolation});
                    break;
                }
                if (value <= 0.0) {
                    report.ok = false;
                    report.hard_violations.push_back({qid, "contains non-positive likelihood", ValidationSeverity::HardViolation});
                    break;
                }
            }
            answer_rows.push_back(likelihoods);

            const auto [min_it, max_it] = std::minmax_element(likelihoods.begin(), likelihoods.end());
            if (*min_it <= kEpsilon) {
                report.warnings.push_back({qid, "contains near-epsilon likelihood", ValidationSeverity::Warning});
            }

            if (answer_index != inference::kIdkIndex) {
                const auto ratio = *max_it / std::max(*min_it, kEpsilon);
                if (ratio < kSubstantiveMinRatio) {
                    report.warnings.push_back({qid, "substantive answer under differentiability ratio", ValidationSeverity::Warning});
                }
            }
        }

        if (answer_rows.size() != inference::kTotalAnswerOptions) {
            continue;
        }

        for (std::size_t t = 0; t < targets.size(); ++t) {
            double sum_across_answers = 0.0;
            for (std::size_t a = 0; a < inference::kTotalAnswerOptions; ++a) {
                sum_across_answers += answer_rows[a][t];
            }
            if (std::abs(sum_across_answers - 1.0) > 1e-3) {
                report.ok = false;
                report.hard_violations.push_back({qid, "per-target normalization violated", ValidationSeverity::HardViolation});
                break;
            }
        }

        for (std::size_t a = 0; a < inference::kTotalAnswerOptions; ++a) {
            for (std::size_t b = a + 1; b < inference::kTotalAnswerOptions; ++b) {
                if (cosine_similarity(answer_rows[a], answer_rows[b]) >= kAnswerDuplicateCosineMax) {
                    report.warnings.push_back({qid, "two answers are nearly duplicates", ValidationSeverity::Warning});
                }
            }
        }

        // IDK should have low posterior impact from a neutral prior.
        const auto& idk = answer_rows[inference::kIdkIndex];
        double mass = 0.0;
        for (std::size_t t = 0; t < targets.size(); ++t) {
            mass += uniform_prior[t] * idk[t];
        }
        std::vector<double> idk_posterior(targets.size(), 0.0);
        for (std::size_t t = 0; t < targets.size(); ++t) {
            idk_posterior[t] = (uniform_prior[t] * idk[t]) / std::max(mass, kEpsilon);
        }
        double idk_kl = 0.0;
        for (std::size_t t = 0; t < targets.size(); ++t) {
            idk_kl += idk_posterior[t] * std::log2(idk_posterior[t] / uniform_prior[t]);
        }
        if (idk_kl > kIdkMaxKlBits) {
            report.warnings.push_back({qid, "IDK has high posterior impact", ValidationSeverity::Warning});
        }

        // Expected information gain under neutral prior.
        double expected_entropy = 0.0;
        double total_answer_mass = 0.0;
        for (std::size_t a = 0; a < inference::kTotalAnswerOptions; ++a) {
            double answer_mass = 0.0;
            for (std::size_t t = 0; t < targets.size(); ++t) {
                answer_mass += uniform_prior[t] * answer_rows[a][t];
            }
            if (answer_mass <= 0.0) {
                continue;
            }

            std::vector<double> posterior(targets.size(), 0.0);
            for (std::size_t t = 0; t < targets.size(); ++t) {
                posterior[t] = (uniform_prior[t] * answer_rows[a][t]) / answer_mass;
            }
            expected_entropy += answer_mass * entropy_bits(posterior);
            total_answer_mass += answer_mass;
        }
        if (total_answer_mass > 0.0) {
            expected_entropy /= total_answer_mass;
        }
        const auto ig_bits = prior_entropy - expected_entropy;
        report.information_gain_bits_by_question[qid] = ig_bits;
        if (ig_bits < kQuestionMinIgBits) {
            report.warnings.push_back({qid, "expected information gain is low", ValidationSeverity::Warning});
        }
    }

    return report;
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
