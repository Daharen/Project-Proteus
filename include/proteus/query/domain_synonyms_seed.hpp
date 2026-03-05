#pragma once

#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/query/query_identity.hpp"

namespace proteus::query {

struct SynonymSeedStats {
    int existing_rows = 0;
    int inserted_rows = 0;
    bool did_seed = false;
};

SynonymSeedStats EnsureSeededDomainSynonyms(
    persistence::SqliteDb& db,
    QueryDomain domain,
    int mapping_version
);

}  // namespace proteus::query
