#pragma once

#include "proteus/inference/identity.hpp"

#include <string>

namespace proteus::playable {

constexpr double kTopologyModifierBound = 0.08;

std::string quantized_identity_axis_material(const inference::AxisVector& identity_axes);
std::string compute_topology_seed(const std::string& stable_player_id, const inference::AxisVector& identity_axes, const std::string& domain);
double topology_noise_unit(const std::string& topology_seed, const std::string& mechanic_id);
double topology_modifier(const std::string& topology_seed, const std::string& mechanic_id, double max_abs = kTopologyModifierBound);

}  // namespace proteus::playable
