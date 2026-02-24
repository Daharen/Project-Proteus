#pragma once

#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/query/query_identity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::bootstrap {

enum class ProposalKind : std::int64_t {
    GenericArm = 0,
    ClassOption = 1,
    SkillOption = 2,
    NpcCandidate = 3,
    DialogueOption = 4,
};

bool ImportBootstrapArtifactForDomain(
    persistence::SqliteDb& db,
    const std::string& stable_player_id,
    const std::string& session_id,
    const std::string& raw_query_text,
    query::QueryDomain query_domain,
    const std::string& artifact_json,
    std::int64_t schema_version
);

bool ImportNovelQueryArtifact(
    persistence::SqliteDb& db,
    const std::string& stable_player_id,
    const std::string& session_id,
    const std::string& raw_query_text,
    const std::string& artifact_json,
    std::int64_t schema_version
);

bool QueryHasBootstrapProposals(
    persistence::SqliteDb& db,
    std::int64_t query_id,
    query::QueryDomain query_domain = query::QueryDomain::Generic
);

std::vector<std::uint8_t> DeterministicNpcId(
    std::int64_t query_id,
    const std::string& npc_name,
    const std::string& npc_role,
    std::int64_t schema_version
);

bool UpsertNpcFromCandidate(
    persistence::SqliteDb& db,
    std::int64_t query_id,
    const std::string& npc_name,
    const std::string& npc_role,
    std::int64_t schema_version
);

}  // namespace proteus::bootstrap
