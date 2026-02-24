#include "proteus/bootstrap/import_novel_query_artifact.hpp"

#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include <cstdint>
#include <string>

namespace proteus::bootstrap {
namespace {
constexpr std::size_t kSynopsisMax = 240;
constexpr std::size_t kTitleMax = 48;
constexpr std::size_t kTagMaxLen = 24;
constexpr std::size_t kTagMaxCount = 6;
constexpr std::size_t kProposalMinCount = 3;
constexpr std::size_t kProposalMaxCount = 5;

std::string cap(const std::string& in, std::size_t n) { return in.size() <= n ? in : in.substr(0, n); }

std::string get_s(const nlohmann::json& obj, const char* key) {
    if (obj.is_object() && obj.contains(key) && obj.at(key).is_string()) return obj.at(key).get<std::string>();
    return {};
}

ProposalKind proposal_kind_for_domain(query::QueryDomain d) {
    if (d == query::QueryDomain::Class) return ProposalKind::ClassOption;
    if (d == query::QueryDomain::Skill) return ProposalKind::SkillOption;
    if (d == query::QueryDomain::NpcIntent) return ProposalKind::NpcCandidate;
    if (d == query::QueryDomain::DialogueLine || d == query::QueryDomain::DialogueOption) return ProposalKind::DialogueOption;
    return ProposalKind::GenericArm;
}
}  // namespace

bool QueryHasBootstrapProposals(persistence::SqliteDb& db, std::int64_t query_id, query::QueryDomain query_domain) {
    auto stmt = db.prepare("SELECT 1 FROM query_bootstrap_proposals WHERE query_id = ?1 AND query_domain = ?2 LIMIT 1;");
    stmt.bind_int64(1, query_id);
    stmt.bind_int64(2, static_cast<std::int64_t>(query_domain));
    return stmt.step();
}

std::vector<std::uint8_t> DeterministicNpcId(std::int64_t query_id, const std::string& npc_name, const std::string& npc_role, std::int64_t schema_version) {
    const std::string seed = std::to_string(query_id) + "|" + npc_name + "|" + npc_role + "|" + std::to_string(schema_version);
    std::vector<std::uint8_t> out(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(seed.data()), seed.size(), out.data());
    return out;
}

bool UpsertNpcFromCandidate(persistence::SqliteDb& db, std::int64_t query_id, const std::string& npc_name, const std::string& npc_role, std::int64_t schema_version) {
    const auto npc_id = DeterministicNpcId(query_id, cap(npc_name, 48), cap(npc_role, 48), schema_version);
    auto stmt = db.prepare("INSERT INTO npc_registry(npc_id, npc_name, npc_role, npc_seed_material, created_from_query_id) VALUES(?1, ?2, ?3, ?4, ?5) ON CONFLICT(npc_id) DO UPDATE SET npc_name=excluded.npc_name, npc_role=excluded.npc_role, npc_seed_material=excluded.npc_seed_material, created_from_query_id=excluded.created_from_query_id;");
    stmt.bind_blob(1, npc_id);
    stmt.bind_text(2, cap(npc_name, 48));
    stmt.bind_text(3, cap(npc_role, 48));
    stmt.bind_text(4, std::to_string(query_id) + "|" + cap(npc_name, 48) + "|" + cap(npc_role, 48));
    stmt.bind_int64(5, query_id);
    stmt.step();
    return true;
}

bool ImportBootstrapArtifactForDomain(persistence::SqliteDb& db, const std::string&, const std::string&, const std::string& raw_query_text, query::QueryDomain query_domain, const std::string& artifact_json, std::int64_t schema_version) {
    nlohmann::json artifact;
    try { artifact = nlohmann::json::parse(artifact_json); } catch (...) { return false; }

    const std::int64_t query_id = query::GetOrCreateQueryId(db, raw_query_text, query_domain);
    const std::string normalized = query::NormalizeQuery(raw_query_text);

    std::string synopsis = cap(get_s(artifact, "synopsis"), kSynopsisMax);
    if (synopsis.empty()) synopsis = cap(get_s(artifact, "intent_summary"), kSynopsisMax);
    const std::string title = cap(get_s(artifact, "title"), kTitleMax);

    nlohmann::json tags = nlohmann::json::array({});
    const char* tkey = artifact.contains("tags") ? "tags" : "intent_tags";
    if (artifact.contains(tkey) && artifact.at(tkey).is_array()) {
        for (const auto& x : artifact.at(tkey)) {
            if (!x.is_string()) continue;
            if (tags.size() >= kTagMaxCount) break;
            tags.push_back(cap(x.get<std::string>(), kTagMaxLen));
        }
    }

    auto up = db.prepare("INSERT INTO query_metadata(query_id, query_domain, normalized_query_text, title, synopsis, tags_json, intent_tags_json, schema_version) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) ON CONFLICT(query_id) DO UPDATE SET query_domain=excluded.query_domain, normalized_query_text=excluded.normalized_query_text, title=excluded.title, synopsis=excluded.synopsis, tags_json=excluded.tags_json, intent_tags_json=excluded.intent_tags_json, schema_version=excluded.schema_version;");
    up.bind_int64(1, query_id);
    up.bind_int64(2, static_cast<std::int64_t>(query_domain));
    up.bind_text(3, normalized);
    up.bind_text(4, title);
    up.bind_text(5, synopsis);
    up.bind_text(6, tags.dump());
    up.bind_text(7, tags.dump());
    up.bind_int64(8, schema_version);
    up.step();

    const char* pkey = artifact.contains("proposals") ? "proposals" : (artifact.contains("npc_candidates") ? "npc_candidates" : (artifact.contains("options") ? "options" : nullptr));
    if (pkey == nullptr || !artifact.at(pkey).is_array()) return false;
    const auto& proposals = artifact.at(pkey);
    if (proposals.size() < kProposalMinCount || proposals.size() > kProposalMaxCount) return false;

    std::size_t i = 0;
    for (const auto& p : proposals) {
        std::string ptitle = get_s(p, "proposal_title");
        if (ptitle.empty()) ptitle = get_s(p, "option_title");
        if (ptitle.empty()) ptitle = get_s(p, "npc_name");
        if (ptitle.empty()) ptitle = get_s(p, "option_text");
        std::string pbody = get_s(p, "proposal_body");
        if (pbody.empty()) pbody = get_s(p, "option_pitch");
        if (pbody.empty()) pbody = get_s(p, "why_relevant");
        const std::string pid = "bootstrap-q" + std::to_string(query_id) + "-d" + std::to_string(static_cast<std::int64_t>(query_domain)) + "-s" + std::to_string(schema_version) + "-i" + std::to_string(i);

        auto ins = db.prepare("INSERT INTO query_bootstrap_proposals(query_id, query_domain, schema_version, proposal_index, proposal_id, proposal_kind, proposal_json, proposal_title, proposal_body, choice_seed_hint, risk_profile) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, '', 'medium') ON CONFLICT(query_id, schema_version, proposal_index) DO UPDATE SET query_domain=excluded.query_domain, proposal_id=excluded.proposal_id, proposal_kind=excluded.proposal_kind, proposal_json=excluded.proposal_json, proposal_title=excluded.proposal_title, proposal_body=excluded.proposal_body;");
        ins.bind_int64(1, query_id);
        ins.bind_int64(2, static_cast<std::int64_t>(query_domain));
        ins.bind_int64(3, schema_version);
        ins.bind_int64(4, static_cast<std::int64_t>(i));
        ins.bind_text(5, pid);
        ins.bind_int64(6, static_cast<std::int64_t>(proposal_kind_for_domain(query_domain)));
        ins.bind_text(7, p.dump());
        ins.bind_text(8, cap(ptitle, 48));
        ins.bind_text(9, cap(pbody, 240));
        ins.step();
        ++i;
    }
    return true;
}

bool ImportNovelQueryArtifact(persistence::SqliteDb& db, const std::string& stable_player_id, const std::string& session_id, const std::string& raw_query_text, const std::string& artifact_json, std::int64_t schema_version) {
    return ImportBootstrapArtifactForDomain(db, stable_player_id, session_id, raw_query_text, query::QueryDomain::Generic, artifact_json, schema_version);
}

}  // namespace proteus::bootstrap
