#include "proteus/playable/retrieval_engine.hpp"

#include "proteus/persistence/sqlite_db.hpp"
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

nlohmann::json procedural_generate(
    const std::string& prompt_hash,
    const std::string& domain
) {
    static constexpr std::array<const char*, 4> kQuestVariants = {
        "A local magistrate needs discreet help recovering a stolen ledger.",
        "A wandering alchemist seeks an escort through haunted wetlands.",
        "A guild courier vanished on the mountain pass with urgent treaties.",
        "A village elder asks you to uncover why sacred wells have gone silent.",
    };

    const std::size_t pick = static_cast<std::size_t>(std::stoull(prompt_hash.substr(0, 8), nullptr, 16) % kQuestVariants.size());

    nlohmann::json proposal;
    proposal["proposal_id"] = prompt_hash;
    proposal["domain"] = domain;
    proposal["type"] = "quest_variant";
    proposal["text"] = kQuestVariants[pick];
    proposal["tags"] = nlohmann::json::array({"procedural", "deterministic"});
    proposal["axis_bias"] = nlohmann::json{{"novelty", 0.25}, {"complexity", 0.1}};
    return proposal;
}

}  // namespace

std::string compute_prompt_hash(
    const std::string& policy_version,
    const std::string& domain,
    const std::string& canonical_prompt
) {
    return sha256_hex(policy_version + "|" + domain + "|" + canonical_prompt);
}

nlohmann::json retrieve_or_generate(persistence::SqliteDb& db, const RetrievalRequest& request) {
    const std::string canonical_prompt = canonicalize_prompt(request.raw_prompt);
    const std::string prompt_hash = compute_prompt_hash(request.policy_version, request.domain, canonical_prompt);

    if (auto cached = find_prompt_cache(db, prompt_hash)) {
        mark_prompt_cache_hit(db, prompt_hash, unix_timestamp_now());
        return cached->response_json;
    }

    nlohmann::json proposal = procedural_generate(prompt_hash, request.domain);
    const ValidationReport report = validate_proposal_json(proposal);
    if (!report.ok) {
        std::ostringstream errors;
        errors << "Proposal validation failed:";
        for (const auto& issue : report.issues) {
            errors << " " << issue << ";";
        }
        throw std::runtime_error(errors.str());
    }

    const std::int64_t now = unix_timestamp_now();
    persistence::SqliteTransaction tx(db);
    insert_prompt_cache(
        db,
        PromptCacheRecord{
            .prompt_hash = prompt_hash,
            .domain = request.domain,
            .canonical_prompt = canonical_prompt,
            .response_json = proposal,
            .model_id = "",
            .policy_version = request.policy_version,
            .created_at = now,
            .last_used_at = now,
            .hit_count = 0,
        }
    );
    tx.commit();

    return proposal;
}

}  // namespace proteus::playable
