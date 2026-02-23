#include "proteus/playable/proposal_selector.hpp"

#include <functional>
#include <stdexcept>

namespace proteus::playable {

std::string DeterministicSelector::select(
    const std::vector<Proposal>& candidates,
    const bandits::PlayerContext& player_context,
    const std::string& domain
) const {
    (void)player_context;
    if (candidates.empty()) {
        throw std::runtime_error("No proposal candidates provided to selector");
    }
    if (candidates.size() == 1) {
        return candidates.front().proposal_id;
    }

    std::hash<std::string> hasher;
    const auto idx = hasher(domain + "|" + candidates.front().proposal_id) % candidates.size();
    return candidates[idx].proposal_id;
}

}  // namespace proteus::playable
