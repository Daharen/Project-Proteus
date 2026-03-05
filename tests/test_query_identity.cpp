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
    EXPECT_EQ(first.canonical_query_id, first.query_id);

    const auto second = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "arcane knight", "v1");
    EXPECT_EQ(second.cluster_id, first.cluster_id);
    EXPECT_EQ(second.decision_band == "alias_hit" || second.decision_band == "hard_duplicate", true);
    EXPECT_EQ(second.canonical_query_id, first.query_id);
    EXPECT_EQ(second.query_id, first.query_id);

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

TEST(QueryIdentityTest, ResolveOrAdmitClusterIdSeedsClassDomainSynonymsDeterministically) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_synonym_seed");

    auto& db = test_db.db();
    const auto result = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "beast trainer", "test");
    EXPECT_EQ(result.cluster_id.empty(), false);

    auto seeded_count = db.prepare("SELECT COUNT(*) FROM domain_synonyms WHERE query_domain = ?1 AND mapping_version = ?2;");
    seeded_count.bind_int64(1, static_cast<std::int64_t>(proteus::query::QueryDomain::Class));
    seeded_count.bind_int64(2, 1);
    ASSERT_EQ(seeded_count.step(), true);
    EXPECT_GT(seeded_count.column_int64(0), 0);
}

TEST(QueryIdentityTest, AdjudicationWritesAliasAndThenResolvesViaAliasHit) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_adjudication_alias");

    auto& db = test_db.db();
    const auto seed = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "animal trainer", "test");
    ASSERT_EQ(seed.cluster_id.empty(), false);

    const auto adjudicated = proteus::query::AdjudicateClusterAliasAndSynonyms(
        db,
        proteus::query::QueryDomain::Class,
        "beast summoner",
        seed.cluster_id,
        {{"beast", "animal"}, {"summoner", "trainer"}},
        1
    );
    EXPECT_EQ(adjudicated.ok, true);
    EXPECT_EQ(adjudicated.alias_written, true);
    EXPECT_EQ(adjudicated.synonyms_written >= 1, true);
    EXPECT_EQ(adjudicated.cluster_id, seed.cluster_id);
    EXPECT_EQ(adjudicated.decision_band, "adjudicated");

    const auto resolved = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "beast summoner", "test");
    EXPECT_EQ(resolved.decision_band, "alias_hit");
    EXPECT_EQ(resolved.cluster_id, seed.cluster_id);
}

TEST(QueryIdentityTest, ResolveClusterGuessProvidesStableAlternatesWhenBestIsNovel) {
    proteus::tests::TestSqliteDbFile test_db("query_identity_cluster_guess");

    auto& db = test_db.db();
    const auto c1 = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "arcane knight", "test");
    const auto c2 = proteus::query::ResolveOrAdmitClusterId(db, proteus::query::QueryDomain::Class, "storm wizard", "test");
    ASSERT_EQ(c1.cluster_id.empty(), false);
    ASSERT_EQ(c2.cluster_id.empty(), false);
    ASSERT_EQ(c1.cluster_id == c2.cluster_id, false);

    const auto g1 = proteus::query::ResolveClusterGuess(db, proteus::query::QueryDomain::Class, "shadow ranger", "test", 5);
    const auto g2 = proteus::query::ResolveClusterGuess(db, proteus::query::QueryDomain::Class, "shadow ranger", "test", 5);

    EXPECT_EQ(g1.best.decision_band, "novel");
    EXPECT_EQ(g1.best.cluster_id, g2.best.cluster_id);
    EXPECT_EQ(g1.alternates.size() <= static_cast<std::size_t>(5), true);
    EXPECT_EQ(g1.alternates.size(), g2.alternates.size());

    for (std::size_t i = 0; i < g1.alternates.size(); ++i) {
        EXPECT_EQ(g1.alternates[i].cluster_id, g2.alternates[i].cluster_id);
        EXPECT_EQ(g1.alternates[i].score, g2.alternates[i].score);
    }
}
