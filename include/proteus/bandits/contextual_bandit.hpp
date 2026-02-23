#pragma once

#include "proteus/inference/identity.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace proteus::bandits {

struct PlayerContext {
    inference::AxisVector identity_axes{};

    float identity_confidence = 0.0F;
    float identity_entropy = 1.0F;

    std::uint32_t questions_answered = 0;
    float idk_rate = 0.0F;

    std::uint64_t session_id = 0;
    std::uint32_t niche_id = 0;

    std::string stable_player_id;

};

struct ArmSelection {
    std::string arm_id;
    double exploration_weight = 0.0;
};

class ContextualBandit {
public:
    virtual ~ContextualBandit() = default;
    virtual ArmSelection select(const PlayerContext& context, const std::vector<std::string>& candidates) = 0;
    virtual void update(const PlayerContext& context, const std::string& arm_id, double reward) = 0;
};

}  // namespace proteus::bandits
