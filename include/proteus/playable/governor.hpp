#pragma once

#include <string>

namespace proteus::playable {

struct GovernorInputs {
    std::string domain;
    std::string prompt_hash;
    std::string player_id;
    std::string topology_seed;
    std::string identity_axes_summary;
    std::size_t history_interactions = 0;
};

struct GovernorAdjustments {
    double dampening_factor = 1.0;
    std::string reason = "noop";
};

GovernorAdjustments compute_governor_adjustments(const GovernorInputs& inputs);

}  // namespace proteus::playable
