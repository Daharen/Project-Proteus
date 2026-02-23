#include "proteus/playable/retrieval_engine.hpp"

#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"

#include <openssl/sha.h>

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
    return player.dump();
}

std::string serialize_features(const std::vector<double>& features) {
    nlohmann::json arr = nlohmann::json::array({});
    for (double v : features) {
        arr.push_back(v);
    }
    return arr.dump();
}

std::vector<double> parse_features_json(const std::string& text) {
    std::vector<double> out;
    if (text.empty()) {
        return out;
    }
    const auto arr = nlohmann::json::parse(text);
    if (!arr.is_array()) {
        return out;
    }
    for (const auto& v : arr) {
        if (v.is_number()) {
            out.push_back(v.get<double>());
        }
    }
    return out;
}

std::vector<Proposal> generate_candidates(
    const std::string& prompt_hash,
    const std::string& canonical_prompt,
    const std::string& domain,
    std::size_t k
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
        const std::string seed_str = sha256_hex(prompt_hash + "|seed|" + std::to_string(i));
        std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(std::stoull(seed_str.substr(0, 16), nullptr, 16)));
        std::uniform_real_distribution<double> bias_dist(-0.8, 0.8);

        const std::string proposal_id = sha256_hex(prompt_hash + "|proposal|" + std::to_string(i));
        const std::string type = kTypes[i % kTypes.size()];
        const std::string tone = kTones[(i + 1) % kTones.size()];

        nlohmann::json payload;
        payload["proposal_id"] = proposal_id;
        payload["domain"] = domain;
        payload["type"] = type;
        payload["text"] = "[" + tone + "] " + canonical_prompt + " -> " + type + " #" + std::to_string(i + 1);
        payload["tags"] = nlohmann::json::array({"procedural", tone});
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

}  // namespace

std::string compute_prompt_hash(
    const std::string& policy_version,
    const std::string& domain,
    const std::string& canonical_prompt
) {
    return sha256_hex(policy_version + "|" + domain + "|" + canonical_prompt);
}

nlohmann::json run_retrieval(
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

    auto candidate_ids = list_prompt_candidate_ids(db, prompt_hash);
    if (candidate_ids.empty() || candidate_ids.size() < kMinCandidates) {
        const std::size_t to_generate = kMinCandidates - candidate_ids.size();
        const auto generated = generate_candidates(prompt_hash, canonical_prompt, request.domain, kMinCandidates);
        persistence::SqliteTransaction tx(db);
        std::size_t added = 0;
        for (const auto& proposal : generated) {
            if (added >= to_generate) {
                break;
            }
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
            ++added;
        }
        tx.commit();
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
            .decision_features_json = serialize_features(decision.decision_features),
            .timestamp = now,
        }
    );

    return selected.payload;
}

void log_reward(
    persistence::SqliteDb& db,
    ProposalSelector& selector,
    const std::string& session_id,
    const std::string& proposal_id,
    double reward_value
) {
    log_reward_interaction(db, session_id, proposal_id, reward_value);
    const auto interaction = latest_interaction_for_session_and_arm(db, session_id, proposal_id);
    if (!interaction.has_value()) {
        return;
    }
    const auto features = parse_features_json(interaction->decision_features_json);
    selector.update_with_reward(features, reward_value);
}

}  // namespace proteus::playable
