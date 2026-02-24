#include "proteus/query/query_identity.hpp"
#include "test_db_raii.hpp"

#include <gtest/gtest.h>

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
}

TEST(QueryIdentityTest, RegistryUpsertAndSimilarityWork) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_registry");

    {
        auto& db = test_db.db();
        const auto id1 = proteus::query::GetOrCreateQueryId(db, "Find me a quest!");
        const auto id2 = proteus::query::GetOrCreateQueryId(db, "find me a quest");
        const auto id3 = proteus::query::GetOrCreateQueryId(db, "combat mission planning");

        EXPECT_EQ(id1, id2);
        EXPECT_EQ(id1 == id3, false);

        const auto similar = proteus::query::FindSimilarQueries(db, "find me quest", 5, 0.0);
        EXPECT_EQ(similar.empty(), false);
    }
}

TEST(QueryIdentityTest, SameTextDifferentDomainProducesDistinctIds) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_domain_sep");

    {
        auto& db = test_db.db();
        const auto class_id = proteus::query::GetOrCreateQueryId(db, "Arcane Knight", proteus::query::QueryDomain::Class);
        const auto skill_id = proteus::query::GetOrCreateQueryId(db, "Arcane Knight", proteus::query::QueryDomain::Skill);
        EXPECT_EQ(class_id == skill_id, false);
    }
}
