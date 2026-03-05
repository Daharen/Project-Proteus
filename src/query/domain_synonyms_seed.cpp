#include "proteus/query/domain_synonyms_seed.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace proteus::query {
namespace {

bool is_single_token(const std::string& normalized) {
    return normalized.find(' ') == std::string::npos && !normalized.empty();
}

std::vector<std::pair<std::string, std::string>> class_domain_seed_pairs_v1() {
    return {
        {"beast", "animal"},
        {"creature", "animal"},
        {"pet", "animal"},
        {"summoner", "trainer"},
        {"tamer", "trainer"},
        {"handler", "trainer"},
        {"wrangler", "trainer"},
    };
}

int count_rows_for(persistence::SqliteDb& db, QueryDomain domain, int mapping_version) {
    auto stmt = db.prepare(
        "SELECT COUNT(1) FROM domain_synonyms WHERE query_domain = ?1 AND mapping_version = ?2;"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_int64(2, static_cast<std::int64_t>(mapping_version));
    if (!stmt.step()) return 0;
    return static_cast<int>(stmt.column_int64(0));
}

int insert_ignore(
    persistence::SqliteDb& db,
    QueryDomain domain,
    const std::string& term,
    const std::string& canonical,
    int mapping_version
) {
    auto stmt = db.prepare(
        "INSERT OR IGNORE INTO domain_synonyms(query_domain, term, canonical_term, mapping_version, created_at_utc) "
        "VALUES(?1, ?2, ?3, ?4, strftime('%Y-%m-%dT%H:%M:%fZ','now'));"
    );
    stmt.bind_int64(1, static_cast<std::int64_t>(domain));
    stmt.bind_text(2, term);
    stmt.bind_text(3, canonical);
    stmt.bind_int64(4, static_cast<std::int64_t>(mapping_version));
    stmt.step();

    auto changes = db.prepare("SELECT changes();");
    if (!changes.step()) return 0;
    return static_cast<int>(changes.column_int64(0));
}

}  // namespace

SynonymSeedStats EnsureSeededDomainSynonyms(persistence::SqliteDb& db, QueryDomain domain, int mapping_version) {
    SynonymSeedStats out;
    out.existing_rows = count_rows_for(db, domain, mapping_version);

    if (out.existing_rows > 0) {
        out.did_seed = false;
        return out;
    }

    const auto now_pairs =
        (domain == QueryDomain::Class && mapping_version == 1)
            ? class_domain_seed_pairs_v1()
            : std::vector<std::pair<std::string, std::string>>{};

    int inserted = 0;
    for (const auto& p : now_pairs) {
        const std::string term = NormalizeQuery(p.first);
        const std::string canonical = NormalizeQuery(p.second);

        if (!is_single_token(term) || !is_single_token(canonical)) {
            throw std::runtime_error("Seed synonym entries must be single-token after NormalizeQuery()");
        }

        inserted += insert_ignore(db, domain, term, canonical, mapping_version);
    }

    out.inserted_rows = inserted;
    out.did_seed = inserted > 0;
    return out;
}

}  // namespace proteus::query
