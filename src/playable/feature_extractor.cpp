#include "proteus/playable/feature_extractor.hpp"

#include <array>
#include <functional>

namespace proteus::playable {

std::vector<double> extract_context_features(const bandits::PlayerContext& context, const std::string& domain) {
    std::vector<double> out;
    out.reserve(context.identity_axes.size() + 4);
    for (float axis : context.identity_axes) {
        out.push_back(static_cast<double>(axis));
    }
    out.push_back(static_cast<double>(context.identity_confidence));
    out.push_back(static_cast<double>(context.identity_entropy));
    out.push_back(static_cast<double>(context.idk_rate));

    std::hash<std::string> hasher;
    out.push_back(static_cast<double>(hasher(domain) % 101) / 100.0);
    return out;
}

std::vector<double> extract_arm_features(const Proposal& proposal) {
    std::vector<double> out;
    out.reserve(6);

    const auto& payload = proposal.payload;
    const auto type = payload.contains("type") && payload.at("type").is_string() ? payload.at("type").get<std::string>() : std::string{"unknown"};

    const std::array<std::string, 4> types = {"quest_variant", "dialog_variant", "lore_hook", "challenge_hook"};
    for (const auto& t : types) {
        out.push_back(type == t ? 1.0 : 0.0);
    }

    double novelty = 0.0;
    double complexity = 0.0;
    if (payload.contains("axis_bias") && payload.at("axis_bias").is_object()) {
        const auto& axis = payload.at("axis_bias");
        if (axis.contains("novelty") && axis.at("novelty").is_number()) {
            novelty = axis.at("novelty").get<double>();
        }
        if (axis.contains("complexity") && axis.at("complexity").is_number()) {
            complexity = axis.at("complexity").get<double>();
        }
    }
    out.push_back(novelty);
    out.push_back(complexity);

    return out;
}

std::vector<double> concat_features(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> out;
    out.reserve(a.size() + b.size());
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

}  // namespace proteus::playable
