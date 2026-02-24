#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/query/query_identity.hpp"

#include <gtest/gtest.h>

#include <filesystem>

TEST(QueryIdentityTest, NormalizationIsDeterministicAcrossVariants) {
    const std::string a = proteus::query::NormalizeQuery("  Hello,   WORLD!!  ");
    const std::string b = proteus::query::NormalizeQuery("hello world");
    const std::string c = proteus::query::NormalizeQuery("HELLO---world");

    EXPECT_EQ(a, "hello world");
    EXPECT_EQ(a, b);
    EXPECT_EQ(b, c);
}

TEST(QueryIdentityTest, Hash64StableForSameInput) {
    const std::string normalized = proteus::query::NormalizeQuery("Quest Finder");
    const std::uint64_t h1 = proteus::query::QueryHash64(normalized);
    const std::uint64_t h2 = proteus::query::QueryHash64(normalized);

    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1 == 14085389326963337723ULL, false);
}

TEST(QueryIdentityTest, RegistryUpsertAndSimilarityWork) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_query_identity.db";
    std::filesystem::remove(db_path);

    {
        proteus::persistence::SqliteDb db;
        db.open(db_path.string());
        proteus::persistence::ensure_schema(db);

        const auto id1 = proteus::query::GetOrCreateQueryId(db, "Find me a quest!");
        const auto id2 = proteus::query::GetOrCreateQueryId(db, "find me a quest");
        const auto id3 = proteus::query::GetOrCreateQueryId(db, "combat mission planning");

        EXPECT_EQ(id1, id2);
        EXPECT_EQ(id1 == id3, false);

        const auto similar = proteus::query::FindSimilarQueries(db, "find me quest", 5, 0.0);
        EXPECT_EQ(similar.empty(), false);
    }

    std::filesystem::remove(db_path);
}


TEST(QueryIdentityTest, SameTextDifferentDomainProducesDistinctIds) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_query_identity_domain.db";
    std::filesystem::remove(db_path);
    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    const auto class_id = proteus::query::GetOrCreateQueryId(db, "Arcane Knight", proteus::query::QueryDomain::Class);
    const auto skill_id = proteus::query::GetOrCreateQueryId(db, "Arcane Knight", proteus::query::QueryDomain::Skill);
    EXPECT_EQ(class_id == skill_id, false);

    std::filesystem::remove(db_path);
}
