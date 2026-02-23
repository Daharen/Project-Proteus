#include "proteus/playable/proposal_selector.hpp"

#include "proteus/playable/feature_extractor.hpp"

#include <openssl/sha.h>

#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
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

std::string sha256_hex(std::string_view value) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest.data());

    std::ostringstream out;
    for (unsigned char byte : digest) {
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return out.str();
}

std::string stable_identity_axis_material(const bandits::PlayerContext& player_context) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    for (float axis : player_context.identity_axes) {
        const int quantized = static_cast<int>(std::round(axis * 100.0F));
        out << quantized << ',';
    }
    return out.str();
}

std::string make_topology_seed(const std::string& stable_player_id, const bandits::PlayerContext& player_context, const std::string& domain) {
    const std::string seed_material = stable_player_id + "|" + stable_identity_axis_material(player_context) + "|" + domain;
    return sha256_hex(seed_material);
}

double bounded_noise(const std::string& topology_seed, const std::string& mechanic_id) {
    const std::string noise_hash = sha256_hex(topology_seed + "|" + mechanic_id);
    const std::uint64_t raw = std::stoull(noise_hash.substr(0, 16), nullptr, 16);
    const double normalized = static_cast<double>(raw) / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return (normalized * 2.0 - 1.0) * 0.08;
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
    const std::string topology_seed = make_topology_seed(session_id, player_context, domain);
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

    if (explore) {
        selected_idx = index_pick(rng);
        selected_x = concat_features(context_features, extract_arm_features(candidates[selected_idx]));
        selected_base_score = sigmoid(dot(weights_, selected_x));
        selected_modifier = bounded_noise(topology_seed, "bandit_scoring_weight:" + candidates[selected_idx].proposal_id);
        selected_final_score = selected_base_score * (1.0 + selected_modifier);
    } else {
        double best_score = -1e9;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto x = concat_features(context_features, extract_arm_features(candidates[i]));
            if (weights_.size() < x.size()) {
                weights_.resize(x.size(), 0.0);
            }
            const double base_score = sigmoid(dot(weights_, x));
            const double modifier = bounded_noise(topology_seed, "bandit_scoring_weight:" + candidates[i].proposal_id);
            const double final_score = base_score * (1.0 + modifier);
            if (final_score > best_score) {
                best_score = final_score;
                selected_idx = i;
                selected_x = x;
                selected_base_score = base_score;
                selected_modifier = modifier;
                selected_final_score = final_score;
            }
        }
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
        .topology_seed = topology_seed,
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
