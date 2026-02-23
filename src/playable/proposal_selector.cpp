#include "proteus/playable/proposal_selector.hpp"

#include "proteus/playable/feature_extractor.hpp"
#include "proteus/playable/governor.hpp"
#include "proteus/playable/topology.hpp"

#include <cmath>
#include <functional>
#include <random>
#include <stdexcept>

namespace proteus::playable {

namespace {

double sigmoid(double x) {
    if (x > 35.0) {
        return 1.0;
    }
    if (x < -35.0) {
        return 0.0;
    }
    return 1.0 / (1.0 + std::exp(-x));
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    const auto n = std::min(a.size(), b.size());
    double v = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        v += a[i] * b[i];
    }
    return v;
}

std::int64_t make_seed(const std::string& policy, const std::string& session_id, const std::string& prompt_hash) {
    std::hash<std::string> hasher;
    return static_cast<std::int64_t>(hasher(policy + "|" + session_id + "|" + prompt_hash));
}

}  // namespace

BanditSelector::BanditSelector(persistence::SqliteDb& db, const std::string& policy_version)
    : db_(db), policy_version_(policy_version) {
    ensure_loaded();
}

void BanditSelector::ensure_loaded() {
    auto stmt = db_.prepare("SELECT value_json FROM bandit_state WHERE key = ?1;");
    stmt.bind_text(1, policy_version_);
    if (!stmt.step()) {
        weights_.assign(18, 0.0);
        persist_state();
        return;
    }

    try {
        const auto state = nlohmann::json::parse(stmt.column_text(0));
        if (state.contains("epsilon") && state.at("epsilon").is_number()) {
            epsilon_ = state.at("epsilon").get<double>();
        }
        if (state.contains("lr") && state.at("lr").is_number()) {
            learning_rate_ = state.at("lr").get<double>();
        }
        if (state.contains("feature_version") && state.at("feature_version").is_string()) {
            feature_version_ = state.at("feature_version").get<std::string>();
        }

        weights_.clear();
        if (state.contains("weight_vector") && state.at("weight_vector").is_array()) {
            for (const auto& v : state.at("weight_vector")) {
                if (v.is_number()) {
                    weights_.push_back(v.get<double>());
                }
            }
        }
        if (weights_.empty()) {
            weights_.assign(18, 0.0);
        }
    } catch (const std::exception&) {
        epsilon_ = 0.10;
        learning_rate_ = 0.01;
        feature_version_ = "v1";
        weights_.assign(18, 0.0);
        persist_state();
    }
}

void BanditSelector::persist_state() {
    nlohmann::json state;
    state["policy_version"] = policy_version_;
    state["epsilon"] = epsilon_;
    state["lr"] = learning_rate_;
    state["feature_version"] = feature_version_;

    nlohmann::json arr = nlohmann::json::array({});
    for (double v : weights_) {
        arr.push_back(v);
    }
    state["weight_vector"] = arr;

    auto upsert = db_.prepare(
        "INSERT INTO bandit_state(key, value_json, updated_at) VALUES(?1, ?2, strftime('%s','now')) "
        "ON CONFLICT(key) DO UPDATE SET value_json = excluded.value_json, updated_at = excluded.updated_at;"
    );
    upsert.bind_text(1, policy_version_);
    upsert.bind_text(2, state.dump());
    upsert.step();
}

SelectionDecision BanditSelector::select(
    const std::vector<Proposal>& candidates,
    const bandits::PlayerContext& player_context,
    const std::string& domain,
    const std::string& prompt_hash,
    const std::string& session_id
) {
    ensure_loaded();
    if (candidates.empty()) {
        throw std::runtime_error("No proposal candidates for bandit selection");
    }

    const auto context_features = extract_context_features(player_context, domain);
    const std::string stable_player_id = player_context.stable_player_id.empty() ? session_id : player_context.stable_player_id;
    const std::string identity_axes_summary = quantized_identity_axis_material(player_context.identity_axes);
    const std::string topology_seed = compute_topology_seed(stable_player_id, player_context.identity_axes, domain);
    const auto seed = make_seed(policy_version_, session_id, prompt_hash);
    std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(seed));
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    std::uniform_int_distribution<std::size_t> index_pick(0, candidates.size() - 1);

    const bool explore = u01(rng) < epsilon_;
    std::size_t selected_idx = 0;
    std::vector<double> selected_x;
    double selected_base_score = 0.0;
    double selected_modifier = 0.0;
    double selected_final_score = 0.0;
    double selected_governor_factor = 1.0;
    std::string selected_governor_reason = "noop";
    std::vector<CandidateScoreDebug> candidate_scores;
    candidate_scores.reserve(candidates.size());

    double best_score = -1e9;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto x = concat_features(context_features, extract_arm_features(candidates[i]));
        if (weights_.size() < x.size()) {
            weights_.resize(x.size(), 0.0);
        }
        const double base_score = sigmoid(dot(weights_, x));
        const double modifier = topology_modifier(topology_seed, "bandit_scoring_weight:" + candidates[i].proposal_id);
        const double topology_score = base_score * (1.0 + modifier);
        const auto governor = compute_governor_adjustments(GovernorInputs{
            .domain = domain,
            .prompt_hash = prompt_hash,
            .player_id = stable_player_id,
            .topology_seed = topology_seed,
            .identity_axes_summary = identity_axes_summary,
            .history_interactions = 0,
        });
        const double final_score = topology_score * governor.dampening_factor;
        candidate_scores.push_back(CandidateScoreDebug{
            .proposal_id = candidates[i].proposal_id,
            .base_score = base_score,
            .modifier = modifier,
            .topology_score = topology_score,
            .governor_factor = governor.dampening_factor,
            .final_score = final_score,
        });

        if (!explore && final_score > best_score) {
            best_score = final_score;
            selected_idx = i;
            selected_x = x;
            selected_base_score = base_score;
            selected_modifier = modifier;
            selected_final_score = final_score;
            selected_governor_factor = governor.dampening_factor;
            selected_governor_reason = governor.reason;
        }
    }

    if (explore) {
        selected_idx = index_pick(rng);
        selected_x = concat_features(context_features, extract_arm_features(candidates[selected_idx]));
        selected_base_score = candidate_scores[selected_idx].base_score;
        selected_modifier = candidate_scores[selected_idx].modifier;
        selected_final_score = candidate_scores[selected_idx].final_score;
        selected_governor_factor = candidate_scores[selected_idx].governor_factor;
        selected_governor_reason = "noop";
    }

    return SelectionDecision{
        .proposal_id = candidates[selected_idx].proposal_id,
        .selection_seed = seed,
        .explored = explore,
        .epsilon_used = epsilon_,
        .decision_features = selected_x,
        .topology_modifier = selected_modifier,
        .base_score = selected_base_score,
        .final_score = selected_final_score,
        .governor_factor = selected_governor_factor,
        .governor_reason = selected_governor_reason,
        .topology_seed = topology_seed,
        .candidate_scores = candidate_scores,
    };
}

std::string BanditSelector::feature_version() const {
    return feature_version_;
}

void BanditSelector::update_with_reward(const std::vector<double>& decision_features, double reward) {
    if (weights_.size() < decision_features.size()) {
        weights_.resize(decision_features.size(), 0.0);
    }
    const double pred = sigmoid(dot(weights_, decision_features));
    const double err = reward - pred;
    for (std::size_t i = 0; i < decision_features.size(); ++i) {
        weights_[i] += learning_rate_ * err * decision_features[i];
    }
    persist_state();
}

}  // namespace proteus::playable
