#include "proteus/playable/topology.hpp"

#include <openssl/sha.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace proteus::playable {
namespace {

std::string sha256_hex(std::string_view value) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest.data());

    std::ostringstream out;
    for (unsigned char byte : digest) {
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return out.str();
}

}  // namespace

std::string quantized_identity_axis_material(const inference::AxisVector& identity_axes) {
    std::ostringstream out;
    for (float axis : identity_axes) {
        const int quantized = static_cast<int>(std::round(axis * 100.0F));
        out << quantized << ',';
    }
    return out.str();
}

std::string compute_topology_seed(const std::string& stable_player_id, const inference::AxisVector& identity_axes, const std::string& domain) {
    const std::string seed_material = stable_player_id + "|" + quantized_identity_axis_material(identity_axes) + "|" + domain;
    return sha256_hex(seed_material);
}

double topology_noise_unit(const std::string& topology_seed, const std::string& mechanic_id) {
    const std::string noise_hash = sha256_hex(topology_seed + "|" + mechanic_id);
    const std::uint64_t raw = std::stoull(noise_hash.substr(0, 16), nullptr, 16);
    const double normalized = static_cast<double>(raw) / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return normalized * 2.0 - 1.0;
}

double topology_modifier(const std::string& topology_seed, const std::string& mechanic_id, double max_abs) {
    if (max_abs < 0.0) {
        throw std::invalid_argument("max_abs must be non-negative");
    }
    const double unit = topology_noise_unit(topology_seed, mechanic_id);
    return unit * max_abs;
}

}  // namespace proteus::playable
