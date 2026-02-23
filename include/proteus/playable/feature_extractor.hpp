#pragma once

#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/playable/proposal.hpp"

#include <string>
#include <vector>

namespace proteus::playable {

std::vector<double> extract_context_features(const bandits::PlayerContext& context, const std::string& domain);
std::vector<double> extract_arm_features(const Proposal& proposal);
std::vector<double> concat_features(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace proteus::playable
