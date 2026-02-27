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

TEST(QueryIdentityTest, FingerprintV1Deterministic) {
    const auto a = proteus::query::ComputeSemanticFingerprintV1("arcane knight");
    const auto b = proteus::query::ComputeSemanticFingerprintV1("arcane knight");
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.size(), static_cast<std::size_t>(512));
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

TEST(QueryIdentityTest, ResolveOrAdmitClusterIdPersistsAliasAndSearchesFacetTypes) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_cluster_alias");

    auto& db = test_db.db();
    const auto first = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "Arcane Knight", "v1");
    EXPECT_EQ(first.decision_band, "novel");
    EXPECT_EQ(first.cluster_id.empty(), false);

    const auto second = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "arcane knight", "v1");
    EXPECT_EQ(second.cluster_id, first.cluster_id);
    EXPECT_EQ(second.decision_band == "alias_hit" || second.decision_band == "hard_duplicate", true);

    auto alias_count = db.prepare("SELECT COUNT(*) FROM concept_alias WHERE query_domain = ?1 AND normalized_alias = ?2 AND cluster_id = ?3;");
    alias_count.bind_int64(1, static_cast<std::int64_t>(proteus::query::QueryDomain::Class));
    alias_count.bind_text(2, "arcane knight");
    alias_count.bind_text(3, first.cluster_id);
    EXPECT_EQ(alias_count.step(), true);
    EXPECT_EQ(alias_count.column_int64(0), 1);

    const auto hits = proteus::query::SearchFacetTypes(db, proteus::query::QueryDomain::Class, "arc", 5);
    EXPECT_EQ(hits.empty(), false);
    EXPECT_EQ(hits.front().cluster_id, first.cluster_id);
}
