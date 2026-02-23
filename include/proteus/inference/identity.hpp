#pragma once

#include "proteus/inference/belief_state.hpp"
#include "proteus/inference/novelty_hooks.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace proteus::inference {

enum class IdentityAxis : std::size_t {
    AgencyStyle = 0,
    ChallengeAppetite,
    ExplorationDrive,
    SystemAppetite,
    RiskPosture,
    SocialPosture,
    NarrativePreference,
    MoralOrientation,
};

constexpr std::size_t kIdentityAxisCount = 8;
using AxisVector = std::array<float, kIdentityAxisCount>;

struct IdentityArchetype {
    std::string id;
    std::string name;
    AxisVector axes{};
};

struct IdentityConfidence {
    double top_posterior_strength = 0.0;
    double normalized_entropy = 1.0;
};

struct IdentityInferenceResult {
    std::vector<TargetScore> archetype_posterior;
    AxisVector derived_axes{};
    IdentityConfidence confidence;
    NoveltySignal novelty;
};

const char* axis_name(IdentityAxis axis);
std::vector<IdentityArchetype> identity_archetypes_v1();
AxisVector derive_axis_vector(
    const std::vector<TargetScore>& posterior,
    const std::unordered_map<std::string, IdentityArchetype>& archetypes
);
double normalized_entropy(const std::vector<TargetScore>& posterior);
IdentityInferenceResult build_identity_result(
    const std::vector<TargetScore>& posterior,
    const std::unordered_map<std::string, IdentityArchetype>& archetypes,
    NoveltySignal novelty,
    std::size_t top_n
);

}  // namespace proteus::inference
