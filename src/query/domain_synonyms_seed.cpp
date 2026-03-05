#include "proteus/query/domain_synonyms_seed.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace proteus::query {

namespace {

struct SeedPair {
  const char* term;
  const char* canonical;
};

std::vector<SeedPair> SeedPairsFor(QueryDomain domain, int mapping_version) {
  std::vector<SeedPair> out;

  if (mapping_version == 1 && domain == QueryDomain::Class) {
    out.push_back({"beast", "animal"});
    out.push_back({"beasts", "animal"});
    out.push_back({"summoner", "trainer"});
    out.push_back({"summoning", "training"});
    out.push_back({"tamer", "trainer"});
    out.push_back({"taming", "training"});
    out.push_back({"ranger", "hunter"});
    out.push_back({"archer", "bowman"});
    out.push_back({"assassin", "killer"});
    out.push_back({"rogue", "thief"});
  }

  return out;
}

int CountExistingRows(persistence::SqliteDb& db, QueryDomain domain, int mapping_version) {
  auto stmt = db.prepare(
    "SELECT COUNT(1) FROM domain_synonyms WHERE query_domain = ?1 AND mapping_version = ?2;"
  );
  stmt.bind_int64(1, static_cast<std::int64_t>(domain));
  stmt.bind_int64(2, static_cast<std::int64_t>(mapping_version));
  if (!stmt.step()) {
    return 0;
  }
  return static_cast<int>(stmt.column_int64(0));
}

bool RowExists(persistence::SqliteDb& db, QueryDomain domain, const std::string& term) {
  auto stmt = db.prepare(
    "SELECT 1 FROM domain_synonyms WHERE query_domain = ?1 AND term = ?2 LIMIT 1;"
  );
  stmt.bind_int64(1, static_cast<std::int64_t>(domain));
  stmt.bind_text(2, term);
  return stmt.step();
}

int InsertIfMissing(persistence::SqliteDb& db, QueryDomain domain, const std::string& term, const std::string& canonical_term, int mapping_version) {
  if (term.empty() || canonical_term.empty()) {
    return 0;
  }
  if (RowExists(db, domain, term)) {
    return 0;
  }

  auto stmt = db.prepare(
    "INSERT OR IGNORE INTO domain_synonyms(query_domain, term, canonical_term, mapping_version, created_at_utc) "
    "VALUES(?1, ?2, ?3, ?4, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
  );
  stmt.bind_int64(1, static_cast<std::int64_t>(domain));
  stmt.bind_text(2, term);
  stmt.bind_text(3, canonical_term);
  stmt.bind_int64(4, static_cast<std::int64_t>(mapping_version));
  stmt.step();

  return 1;
}

} // namespace

SynonymSeedStats EnsureSeededDomainSynonyms(persistence::SqliteDb& db, QueryDomain domain, int mapping_version) {
  SynonymSeedStats stats;
  stats.existing_rows = CountExistingRows(db, domain, mapping_version);

  const auto pairs = SeedPairsFor(domain, mapping_version);
  if (pairs.empty()) {
    stats.did_seed = false;
    return stats;
  }

  int inserted = 0;
  for (const auto& p : pairs) {
    inserted += InsertIfMissing(db, domain, std::string(p.term), std::string(p.canonical), mapping_version);
  }

  stats.inserted_rows = inserted;
  stats.did_seed = (inserted > 0);
  return stats;
}

} // namespace proteus::query
