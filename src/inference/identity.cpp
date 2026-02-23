#include "proteus/inference/identity.hpp"

#include <algorithm>
#include <cmath>

namespace proteus::inference {
namespace {
constexpr std::size_t idx(IdentityAxis axis) {
    return static_cast<std::size_t>(axis);
}
}  // namespace

const char* axis_name(IdentityAxis axis) {
    switch (axis) {
        case IdentityAxis::AgencyStyle:
            return "agency_style";
        case IdentityAxis::ChallengeAppetite:
            return "challenge_appetite";
        case IdentityAxis::ExplorationDrive:
            return "exploration_drive";
        case IdentityAxis::SystemAppetite:
            return "system_appetite";
        case IdentityAxis::RiskPosture:
            return "risk_posture";
        case IdentityAxis::SocialPosture:
            return "social_posture";
        case IdentityAxis::NarrativePreference:
            return "narrative_preference";
        case IdentityAxis::MoralOrientation:
            return "moral_orientation";
    }
    return "unknown";
}

std::vector<IdentityArchetype> identity_archetypes_v1() {
    return {
        {"identity:planner", "The Planner", {0.9F, -0.2F, -0.8F, 0.9F, -0.7F, -0.2F, -0.4F, 0.1F}},
        {"identity:wanderer", "The Wanderer", {-0.2F, -0.4F, 0.95F, -0.8F, 0.1F, -0.5F, -0.9F, 0.0F}},
        {"identity:challenger", "The Challenger", {0.6F, 0.95F, -0.2F, 0.8F, 0.7F, -0.3F, -0.3F, -0.2F}},
        {"identity:storybound", "The Storybound", {0.1F, -0.2F, -0.1F, -0.1F, -0.4F, 0.2F, 0.95F, 0.8F}},
        {"identity:opportunist", "The Opportunist", {0.8F, 0.3F, 0.1F, 0.4F, 0.95F, -0.2F, -0.5F, -0.9F}},
        {"identity:guardian", "The Guardian", {0.2F, -0.7F, -0.3F, 0.0F, -0.8F, 0.95F, 0.4F, 0.95F}},
        {"identity:lone_wolf", "The Lone Wolf", {-0.4F, 0.7F, -0.1F, 0.2F, 0.3F, -0.95F, -0.4F, -0.6F}},
        {"identity:tactician", "The Tactician", {0.95F, 0.7F, -0.4F, 0.95F, -0.2F, 0.1F, -0.3F, 0.1F}},
        {"identity:immersionist", "The Immersionist", {-0.2F, -0.8F, 0.3F, -0.95F, -0.6F, -0.1F, 0.8F, 0.4F}},
        {"identity:experimenter", "The Experimenter", {0.5F, 0.4F, 0.8F, 0.7F, 0.9F, -0.4F, -0.2F, -0.1F}},
        {"identity:speedrunner", "The Speedrunner", {0.95F, 0.8F, -0.9F, 0.95F, 0.3F, -0.5F, -0.8F, -0.4F}},
        {"identity:diplomat", "The Diplomat", {0.3F, -0.4F, 0.1F, -0.2F, -0.5F, 0.9F, 0.8F, 0.9F}},
    };
}

AxisVector derive_axis_vector(
    const std::vector<TargetScore>& posterior,
    const std::unordered_map<std::string, IdentityArchetype>& archetypes
) {
    AxisVector derived{};

    for (const auto& score : posterior) {
        const auto it = archetypes.find(score.target_id);
        if (it == archetypes.end()) {
            continue;
        }
        for (std::size_t i = 0; i < derived.size(); ++i) {
            derived[i] += static_cast<float>(score.posterior * it->second.axes[i]);
        }
    }

    return derived;
}

double normalized_entropy(const std::vector<TargetScore>& posterior) {
    if (posterior.empty()) {
        return 1.0;
    }

    double entropy = 0.0;
    for (const auto& score : posterior) {
        if (score.posterior > 0.0) {
            entropy -= score.posterior * std::log2(score.posterior);
        }
    }

    const auto max_entropy = std::log2(static_cast<double>(posterior.size()));
    if (max_entropy <= 0.0) {
        return 0.0;
    }
    return std::clamp(entropy / max_entropy, 0.0, 1.0);
}

IdentityInferenceResult build_identity_result(
    const std::vector<TargetScore>& posterior,
    const std::unordered_map<std::string, IdentityArchetype>& archetypes,
    NoveltySignal novelty,
    std::size_t top_n
) {
    IdentityInferenceResult result;

    result.archetype_posterior = posterior;
    std::ranges::sort(result.archetype_posterior, [](const TargetScore& a, const TargetScore& b) {
        return a.posterior > b.posterior;
    });
    if (result.archetype_posterior.size() > top_n) {
        result.archetype_posterior.resize(top_n);
    }

    result.derived_axes = derive_axis_vector(posterior, archetypes);
    result.confidence.top_posterior_strength = result.archetype_posterior.empty() ? 0.0 : result.archetype_posterior.front().posterior;
    result.confidence.normalized_entropy = normalized_entropy(posterior);
    result.novelty = novelty;

    return result;
}

}  // namespace proteus::inference
