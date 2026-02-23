#pragma once

#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/proposal.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::playable {

struct SelectionDecision {
    std::string proposal_id;
    std::int64_t selection_seed = 0;
    bool explored = false;
    double epsilon_used = 0.0;
    std::vector<double> decision_features;
};

class ProposalSelector {
public:
    virtual ~ProposalSelector() = default;
    virtual SelectionDecision select(
        const std::vector<Proposal>& candidates,
        const bandits::PlayerContext& player_context,
        const std::string& domain,
        const std::string& prompt_hash,
        const std::string& session_id
    ) = 0;

    virtual void update_with_reward(const std::vector<double>& decision_features, double reward) = 0;
    virtual std::string feature_version() const = 0;
};

class BanditSelector final : public ProposalSelector {
public:
    explicit BanditSelector(persistence::SqliteDb& db, const std::string& policy_version);

    SelectionDecision select(
        const std::vector<Proposal>& candidates,
        const bandits::PlayerContext& player_context,
        const std::string& domain,
        const std::string& prompt_hash,
        const std::string& session_id
    ) override;

    void update_with_reward(const std::vector<double>& decision_features, double reward) override;
    std::string feature_version() const override;

private:
    persistence::SqliteDb& db_;
    std::string policy_version_;
    std::vector<double> weights_;
    double epsilon_ = 0.10;
    double learning_rate_ = 0.01;
    std::string feature_version_ = "v1";

    void ensure_loaded();
    void persist_state();
};

}  // namespace proteus::playable
