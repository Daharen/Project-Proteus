#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <cstdint>
#include <string>

namespace proteus::bootstrap {

bool ImportNovelQueryArtifact(
    persistence::SqliteDb& db,
    const std::string& stable_player_id,
    const std::string& session_id,
    const std::string& raw_query_text,
    const std::string& artifact_json,
    std::int64_t schema_version
);

bool QueryHasBootstrapProposals(persistence::SqliteDb& db, std::int64_t query_id);

}  // namespace proteus::bootstrap
