#pragma once

#include <string>
#include <vector>

namespace proteus::bandits {

struct PlayerContext {
    std::vector<double> features;
    std::string niche_id;
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
