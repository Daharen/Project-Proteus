#include "proteus/playable/retrieval_engine.hpp"

#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"

#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <iomanip>
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

Proposal procedural_generate(const std::string& prompt_hash, const std::string& domain) {
    static constexpr std::array<const char*, 4> kQuestVariants = {
        "A local magistrate needs discreet help recovering a stolen ledger.",
        "A wandering alchemist seeks an escort through haunted wetlands.",
        "A guild courier vanished on the mountain pass with urgent treaties.",
        "A village elder asks you to uncover why sacred wells have gone silent.",
    };

    const std::size_t pick = static_cast<std::size_t>(std::stoull(prompt_hash.substr(0, 8), nullptr, 16) % kQuestVariants.size());

    nlohmann::json payload;
    payload["proposal_id"] = prompt_hash;
    payload["domain"] = domain;
    payload["type"] = "quest_variant";
    payload["text"] = kQuestVariants[pick];
    payload["tags"] = nlohmann::json::array({"procedural", "deterministic"});
    payload["axis_bias"] = nlohmann::json{{"novelty", 0.25}, {"complexity", 0.1}};

    return Proposal{
        .proposal_id = prompt_hash,
        .domain = domain,
        .source = "procedural",
        .payload = payload,
    };
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
    const ProposalSelector& selector
) {
    const std::string canonical_prompt = canonicalize_prompt(request.raw_prompt);
    const std::string prompt_hash = compute_prompt_hash(request.policy_version, request.domain, canonical_prompt);
    const std::int64_t now = unix_timestamp_now();

    Proposal proposal;
    bool cache_hit = false;

    if (auto cached = find_prompt_cache(db, prompt_hash)) {
        mark_prompt_cache_hit(db, prompt_hash, now);
        proposal = Proposal{
            .proposal_id = cached->proposal_id,
            .domain = cached->domain,
            .source = "cache",
            .payload = load_proposal_json(db, cached->proposal_id),
        };
        cache_hit = true;
    } else {
        proposal = procedural_generate(prompt_hash, request.domain);
        const ValidationReport report = validate_proposal_json(proposal.payload);
        if (!report.ok) {
            std::ostringstream errors;
            errors << "Proposal validation failed:";
            for (const auto& issue : report.issues) {
                errors << " " << issue << ";";
            }
            throw std::runtime_error(errors.str());
        }

        persistence::SqliteTransaction tx(db);
        upsert_proposal_registry(db, proposal.proposal_id, proposal.domain, proposal.payload, proposal.source, now);
        insert_prompt_cache(
            db,
            PromptCacheRecord{
                .prompt_hash = prompt_hash,
                .domain = request.domain,
                .canonical_prompt = canonical_prompt,
                .proposal_id = proposal.proposal_id,
                .model_id = "",
                .policy_version = request.policy_version,
                .created_at = now,
                .last_used_at = now,
                .hit_count = 0,
            }
        );
        tx.commit();
    }

    const std::vector<Proposal> candidates = {proposal};
    const std::string selected_id = selector.select(candidates, request.player_context, request.domain);
    if (selected_id != proposal.proposal_id) {
        throw std::runtime_error("Selected proposal_id not found in candidates");
    }

    insert_interaction_log(
        db,
        InteractionLogRecord{
            .session_id = request.session_id,
            .prompt_hash = prompt_hash,
            .player_context_json = serialize_player_context(request.player_context),
            .chosen_arm = selected_id,
            .novelty_flag = cache_hit ? 0 : 1,
            .reward_signal = 0.0,
            .reward_is_null = true,
            .timestamp = now,
        }
    );

    return proposal.payload;
}

void log_reward(
    persistence::SqliteDb& db,
    const std::string& session_id,
    const std::string& proposal_id,
    double reward_value
) {
    log_reward_interaction(db, session_id, proposal_id, reward_value);
}

}  // namespace proteus::playable
