#include "proteus/bootstrap/import_novel_query_artifact.hpp"

#include "proteus/query/query_identity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace proteus::bootstrap {
namespace {

constexpr std::size_t kSynopsisMax = 240;
constexpr std::size_t kIntentTagMaxLen = 24;
constexpr std::size_t kIntentTagMaxCount = 6;
constexpr std::size_t kProposalTitleMax = 48;
constexpr std::size_t kProposalBodyMax = 240;
constexpr std::size_t kChoiceSeedHintMax = 48;
constexpr std::size_t kProposalMinCount = 3;
constexpr std::size_t kProposalMaxCount = 5;

std::string cap(const std::string& in, std::size_t n) {
    if (in.size() <= n) {
        return in;
    }
    return in.substr(0, n);
}

bool valid_risk(const std::string& v) {
    return v == "low" || v == "medium" || v == "high";
}

}  // namespace

bool QueryHasBootstrapProposals(persistence::SqliteDb& db, std::int64_t query_id) {
    auto stmt = db.prepare("SELECT 1 FROM query_bootstrap_proposals WHERE query_id = ?1 LIMIT 1;");
    stmt.bind_int64(1, query_id);
    return stmt.step();
}

bool ImportNovelQueryArtifact(
    persistence::SqliteDb& db,
    const std::string& stable_player_id,
    const std::string& session_id,
    const std::string& raw_query_text,
    const std::string& artifact_json,
    std::int64_t schema_version
) {
    (void)stable_player_id;
    (void)session_id;
    nlohmann::json artifact;
    try {
        artifact = nlohmann::json::parse(artifact_json);
    } catch (...) {
        return false;
    }

    if (!artifact.is_object() || !artifact.contains("proposals") || !artifact.at("proposals").is_array()) {
        return false;
    }

    const auto& proposals = artifact.at("proposals");
    if (proposals.size() < kProposalMinCount || proposals.size() > kProposalMaxCount) {
        return false;
    }

    const std::int64_t query_id = query::GetOrCreateQueryId(db, raw_query_text);

    const std::string normalized = query::NormalizeQuery(raw_query_text);
    std::string synopsis;
    if (artifact.contains("synopsis") && artifact.at("synopsis").is_string()) {
        synopsis = cap(artifact.at("synopsis").get<std::string>(), kSynopsisMax);
    }

    nlohmann::json tags = nlohmann::json::array({});
    if (artifact.contains("intent_tags") && artifact.at("intent_tags").is_array()) {
        for (const auto& tag : artifact.at("intent_tags")) {
            if (!tag.is_string()) {
                continue;
            }
            if (tags.size() >= kIntentTagMaxCount) {
                break;
            }
            tags.push_back(cap(tag.get<std::string>(), kIntentTagMaxLen));
        }
    }

    auto upsert_meta = db.prepare(
        "INSERT INTO query_metadata(query_id, normalized_query_text, synopsis, intent_tags_json, schema_version) VALUES(?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(query_id) DO UPDATE SET normalized_query_text=excluded.normalized_query_text, synopsis=excluded.synopsis, intent_tags_json=excluded.intent_tags_json, schema_version=excluded.schema_version;"
    );
    upsert_meta.bind_int64(1, query_id);
    upsert_meta.bind_text(2, normalized);
    upsert_meta.bind_text(3, synopsis);
    upsert_meta.bind_text(4, tags.dump());
    upsert_meta.bind_int64(5, schema_version);
    upsert_meta.step();

    std::size_t i = 0;
    for (const auto& p : proposals) {
        if (!p.is_object()) {
            return false;
        }
        const std::string title = p.contains("proposal_title") && p.at("proposal_title").is_string() ? cap(p.at("proposal_title").get<std::string>(), kProposalTitleMax) : std::string{};
        const std::string body = p.contains("proposal_body") && p.at("proposal_body").is_string() ? cap(p.at("proposal_body").get<std::string>(), kProposalBodyMax) : std::string{};
        const std::string hint = p.contains("choice_seed_hint") && p.at("choice_seed_hint").is_string() ? cap(p.at("choice_seed_hint").get<std::string>(), kChoiceSeedHintMax) : std::string{};
        const std::string risk = p.contains("risk_profile") && p.at("risk_profile").is_string() ? p.at("risk_profile").get<std::string>() : std::string{"medium"};
        if (!valid_risk(risk) || title.empty() || body.empty()) {
            return false;
        }

        const std::string proposal_id = "bootstrap-q" + std::to_string(query_id) + "-s" + std::to_string(schema_version) + "-i" + std::to_string(i);
        auto ins = db.prepare(
            "INSERT INTO query_bootstrap_proposals(query_id, schema_version, proposal_index, proposal_id, proposal_title, proposal_body, choice_seed_hint, risk_profile) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
            "ON CONFLICT(query_id, schema_version, proposal_index) DO UPDATE SET proposal_id=excluded.proposal_id, proposal_title=excluded.proposal_title, proposal_body=excluded.proposal_body, choice_seed_hint=excluded.choice_seed_hint, risk_profile=excluded.risk_profile;"
        );
        ins.bind_int64(1, query_id);
        ins.bind_int64(2, schema_version);
        ins.bind_int64(3, static_cast<std::int64_t>(i));
        ins.bind_text(4, proposal_id);
        ins.bind_text(5, title);
        ins.bind_text(6, body);
        ins.bind_text(7, hint);
        ins.bind_text(8, risk);
        ins.step();
        ++i;
    }

    return true;
}

}  // namespace proteus::bootstrap
