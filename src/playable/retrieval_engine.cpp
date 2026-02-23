#include "proteus/playable/retrieval_engine.hpp"

#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace proteus::playable {

namespace {

constexpr std::int64_t kPruneMinRewardCount = 3;
constexpr double kPruneMeanRewardThreshold = 0.25;

std::int64_t unix_timestamp_now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
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

std::string serialize_player_context(const bandits::PlayerContext& context) {
    nlohmann::json player;
    player["identity_confidence"] = context.identity_confidence;
    player["identity_entropy"] = context.identity_entropy;
    player["questions_answered"] = static_cast<double>(context.questions_answered);
    player["idk_rate"] = context.idk_rate;
    player["session_id"] = static_cast<double>(context.session_id);
    player["niche_id"] = static_cast<double>(context.niche_id);
    player["stable_player_id"] = context.stable_player_id;
    return player.dump();
}

std::string serialize_features(const std::vector<double>& features, const std::string& feature_version) {
    nlohmann::json arr = nlohmann::json::array({});
    for (double v : features) {
        arr.push_back(v);
    }
    nlohmann::json root;
    root["feature_version"] = feature_version;
    root["features"] = arr;
    return root.dump();
}

bool parse_features_json(const std::string& text, std::string& feature_version, std::vector<double>& out) {
    out.clear();
    if (text.empty()) {
        return false;
    }
    const auto root = nlohmann::json::parse(text);
    if (!root.is_object() || !root.contains("feature_version") || !root.contains("features")) {
        return false;
    }
    if (!root.at("feature_version").is_string() || !root.at("features").is_array()) {
        return false;
    }
    feature_version = root.at("feature_version").get<std::string>();
    for (const auto& v : root.at("features")) {
        if (v.is_number()) {
            out.push_back(v.get<double>());
        }
    }
    return true;
}

std::vector<Proposal> generate_candidates(
    const std::string& prompt_hash,
    const std::string& canonical_prompt,
    const std::string& domain,
    std::size_t k,
    int regen_index,
    std::size_t start_offset
) {
    static constexpr std::array<const char*, 4> kTypes = {
        "quest_variant", "dialog_variant", "lore_hook", "challenge_hook"
    };
    static constexpr std::array<const char*, 4> kTones = {
        "grim", "hopeful", "comedic", "mysterious"
    };

    std::vector<Proposal> out;
    out.reserve(k);

    for (std::size_t i = 0; i < k; ++i) {
        const std::size_t idx = start_offset + i;
        const std::string seed_material = prompt_hash + "|regen|" + std::to_string(regen_index) + "|i|" + std::to_string(idx);
        const std::string seed_str = sha256_hex(seed_material);
        std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(std::stoull(seed_str.substr(0, 16), nullptr, 16)));
        std::uniform_real_distribution<double> bias_dist(-0.8, 0.8);

        const std::string proposal_id = sha256_hex(prompt_hash + "|proposal|" + seed_material);
        const std::string type = kTypes[idx % kTypes.size()];
        const std::string tone = kTones[(idx + 1) % kTones.size()];

        nlohmann::json payload;
        payload["proposal_id"] = proposal_id;
        payload["domain"] = domain;
        payload["type"] = type;
        payload["text"] = "[" + tone + "] " + canonical_prompt + " -> " + type + " #" + std::to_string(idx + 1);
        payload["tags"] = nlohmann::json::array({"procedural", tone, "regen_" + std::to_string(regen_index)});
        payload["axis_bias"] = nlohmann::json{{"novelty", bias_dist(rng)}, {"complexity", bias_dist(rng)}};

        out.push_back(Proposal{
            .proposal_id = proposal_id,
            .domain = domain,
            .source = "procedural_seeded",
            .payload = payload,
        });
    }

    return out;
}

void prune_prompt_candidates(persistence::SqliteDb& db, const std::string& prompt_hash) {
    auto candidate_ids = list_prompt_candidate_ids(db, prompt_hash);
    if (candidate_ids.size() <= kMinCandidates) {
        return;
    }

    struct CandidateScore {
        std::string proposal_id;
        double mean_reward;
        std::int64_t reward_count;
    };

    std::vector<CandidateScore> removable;
    for (const auto& proposal_id : candidate_ids) {
        const auto stats = get_proposal_stats(db, proposal_id);
        if (!stats.has_value() || stats->reward_count < kPruneMinRewardCount) {
            continue;
        }
        const double mean = stats->reward_sum / static_cast<double>(stats->reward_count);
        if (mean < kPruneMeanRewardThreshold) {
            removable.push_back(CandidateScore{proposal_id, mean, stats->reward_count});
        }
    }

    std::sort(removable.begin(), removable.end(), [](const CandidateScore& a, const CandidateScore& b) {
        if (a.mean_reward == b.mean_reward) {
            return a.reward_count > b.reward_count;
        }
        return a.mean_reward < b.mean_reward;
    });

    for (const auto& candidate : removable) {
        if (candidate_ids.size() <= kMinCandidates) {
            break;
        }
        remove_prompt_candidate(db, prompt_hash, candidate.proposal_id);
        candidate_ids = list_prompt_candidate_ids(db, prompt_hash);
    }
}

void seed_prompt_candidates(
    persistence::SqliteDb& db,
    const std::string& prompt_hash,
    const std::string& canonical_prompt,
    const std::string& domain,
    std::size_t required_count,
    bool reseed
) {
    if (required_count == 0) {
        return;
    }

    int regen_index = get_prompt_regen_count(db, prompt_hash);
    if (reseed) {
        regen_index = increment_prompt_regen_count(db, prompt_hash);
    }

    const auto existing_count = list_prompt_candidate_ids(db, prompt_hash).size();
    const auto generated = generate_candidates(
        prompt_hash,
        canonical_prompt,
        domain,
        required_count,
        regen_index,
        existing_count
    );

    const std::int64_t now = unix_timestamp_now();
    persistence::SqliteTransaction tx(db);
    for (const auto& proposal : generated) {
        const ValidationReport report = validate_proposal_json(proposal.payload);
        if (!report.ok) {
            throw std::runtime_error("Generated proposal failed schema validation");
        }
        upsert_proposal_registry(db, proposal.proposal_id, proposal.domain, proposal.payload, proposal.source, now);
        insert_prompt_candidate(
            db,
            PromptCandidateRecord{
                .prompt_hash = prompt_hash,
                .proposal_id = proposal.proposal_id,
                .weight = 1.0,
                .created_at = now,
            }
        );
    }
    tx.commit();
}

}  // namespace

std::string compute_prompt_hash(
    const std::string& policy_version,
    const std::string& domain,
    const std::string& canonical_prompt
) {
    return sha256_hex(policy_version + "|" + domain + "|" + canonical_prompt);
}

RetrievalResult run_retrieval_detailed(
    persistence::SqliteDb& db,
    const RetrievalRequest& request,
    ProposalSelector& selector
) {
    const std::string canonical_prompt = canonicalize_prompt(request.raw_prompt);
    const std::string prompt_hash = compute_prompt_hash(request.policy_version, request.domain, canonical_prompt);
    const std::int64_t now = unix_timestamp_now();

    bool cache_hit = false;
    if (auto cached = find_prompt_cache(db, prompt_hash)) {
        (void)cached;
        cache_hit = true;
        mark_prompt_cache_hit(db, prompt_hash, now);
    } else {
        persistence::SqliteTransaction tx(db);
        insert_prompt_cache(
            db,
            PromptCacheRecord{
                .prompt_hash = prompt_hash,
                .domain = request.domain,
                .canonical_prompt = canonical_prompt,
                .model_id = "",
                .policy_version = request.policy_version,
                .created_at = now,
                .last_used_at = now,
                .hit_count = 0,
            }
        );
        tx.commit();
    }

    prune_prompt_candidates(db, prompt_hash);
    auto candidate_ids = list_prompt_candidate_ids(db, prompt_hash);

    if (candidate_ids.size() < kMinCandidates) {
        seed_prompt_candidates(
            db,
            prompt_hash,
            canonical_prompt,
            request.domain,
            kMinCandidates - candidate_ids.size(),
            !candidate_ids.empty()
        );
        candidate_ids = list_prompt_candidate_ids(db, prompt_hash);
    }

    if (candidate_ids.empty()) {
        throw std::runtime_error("No prompt candidates available for prompt_hash: " + prompt_hash);
    }

    std::vector<Proposal> candidates;
    for (const auto& id : candidate_ids) {
        candidates.push_back(load_proposal(db, id));
    }

    const SelectionDecision decision = selector.select(candidates, request.player_context, request.domain, prompt_hash, request.session_id);

    Proposal selected;
    bool found = false;
    for (const auto& c : candidates) {
        if (c.proposal_id == decision.proposal_id) {
            selected = c;
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::runtime_error("Selected proposal_id not found in candidates");
    }

    const std::string stable_player_id = request.player_context.stable_player_id.empty() ? request.session_id : request.player_context.stable_player_id;
    insert_interaction_log(
        db,
        InteractionLogRecord{
            .session_id = request.session_id,
            .prompt_hash = prompt_hash,
            .player_context_json = serialize_player_context(request.player_context),
            .chosen_arm = selected.proposal_id,
            .novelty_flag = cache_hit ? 0 : 1,
            .reward_signal = 0.0,
            .reward_is_null = true,
            .selection_seed = decision.selection_seed,
            .decision_features_json = serialize_features(decision.decision_features, selector.feature_version()),
            .stable_player_id = stable_player_id,
            .base_score = decision.base_score,
            .topology_modifier = decision.topology_modifier,
            .final_score = decision.final_score,
            .timestamp = now,
        }
    );

    update_proposal_stats_on_show(db, selected.proposal_id, now);

    return RetrievalResult{
        .session_id = request.session_id,
        .prompt_hash = prompt_hash,
        .decision = decision,
        .proposal = selected.payload,
    };
}

nlohmann::json run_retrieval(
    persistence::SqliteDb& db,
    const RetrievalRequest& request,
    ProposalSelector& selector
) {
    return run_retrieval_detailed(db, request, selector).proposal;
}

void log_reward(
    persistence::SqliteDb& db,
    ProposalSelector& selector,
    const std::string& session_id,
    const std::string& proposal_id,
    double reward_value
) {
    const bool applied = log_reward_interaction_once(db, session_id, proposal_id, reward_value);
    if (!applied) {
        return;
    }
    update_proposal_stats_on_reward(db, proposal_id, reward_value);
    const auto interaction = latest_interaction_for_session_and_arm(db, session_id, proposal_id);
    if (!interaction.has_value()) {
        return;
    }
    std::string feature_version;
    std::vector<double> features;
    if (!parse_features_json(interaction->decision_features_json, feature_version, features)) {
        return;
    }
    if (feature_version != selector.feature_version()) {
        return;
    }
    selector.update_with_reward(features, reward_value);
}

}  // namespace proteus::playable
