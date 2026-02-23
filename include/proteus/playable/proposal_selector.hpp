#pragma once

#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/playable/proposal.hpp"

#include <string>
#include <vector>

namespace proteus::playable {

class ProposalSelector {
public:
    virtual ~ProposalSelector() = default;
    virtual std::string select(
        const std::vector<Proposal>& candidates,
        const bandits::PlayerContext& player_context,
        const std::string& domain
    ) const = 0;
};

class DeterministicSelector final : public ProposalSelector {
public:
    std::string select(
        const std::vector<Proposal>& candidates,
        const bandits::PlayerContext& player_context,
        const std::string& domain
    ) const override;
};

}  // namespace proteus::playable
