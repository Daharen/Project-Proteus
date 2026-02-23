#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"
#include "proteus/playable/retrieval_engine.hpp"

#include <gtest/gtest.h>

#include <filesystem>

TEST(PlayableCoreTest, CanonicalizationProducesStableHash) {
    const std::string first = proteus::playable::canonicalize_prompt("  Help\tme find\nA QUEST!!! ");
    const std::string second = proteus::playable::canonicalize_prompt("help me find a quest??");

    EXPECT_EQ(first, "help me find a quest");
    EXPECT_EQ(first, second);

    const std::string hash1 = proteus::playable::compute_prompt_hash("playable_core_v1", "rpg", first);
    const std::string hash2 = proteus::playable::compute_prompt_hash("playable_core_v1", "rpg", second);
    EXPECT_EQ(hash1, hash2);
}

TEST(PlayableCoreTest, CacheHitIncrementsCounter) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_cache_hits.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    const proteus::playable::RetrievalRequest request{
        .domain = "rpg",
        .raw_prompt = "help me find a quest",
    };

    const auto first = proteus::playable::retrieve_or_generate(db, request);
    const auto second = proteus::playable::retrieve_or_generate(db, request);
    EXPECT_EQ(first.dump(), second.dump());

    const auto hash = proteus::playable::compute_prompt_hash(
        request.policy_version,
        request.domain,
        proteus::playable::canonicalize_prompt(request.raw_prompt)
    );
    const auto cached = proteus::playable::find_prompt_cache(db, hash);
    EXPECT_EQ(cached.has_value(), true);
    EXPECT_EQ(cached->hit_count, 1);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, SchemaValidationRejectsMalformedProposal) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_schema_reject.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    nlohmann::json invalid = {
        {"proposal_id", "bad"},
        {"domain", "rpg"},
        {"type", "quest_variant"},
        {"text", ""},
        {"tags", nlohmann::json::array({1, 2, 3})},
    };
    const auto report = proteus::playable::validate_proposal_json(invalid);
    EXPECT_EQ(report.ok, false);

    if (report.ok) {
        proteus::playable::insert_prompt_cache(
            db,
            proteus::playable::PromptCacheRecord{
                .prompt_hash = "h",
                .domain = "rpg",
                .canonical_prompt = "p",
                .response_json = invalid,
                .policy_version = "playable_core_v1",
                .created_at = 1,
                .last_used_at = 1,
                .hit_count = 0,
            }
        );
    }

    const auto existing = proteus::playable::find_prompt_cache(db, "h");
    EXPECT_EQ(existing.has_value(), false);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, PolicyVersionChangeSeparatesCacheKeys) {
    const std::string canonical = proteus::playable::canonicalize_prompt("help me find a quest");
    const auto v1 = proteus::playable::compute_prompt_hash("playable_core_v1", "rpg", canonical);
    const auto v2 = proteus::playable::compute_prompt_hash("playable_core_v2", "rpg", canonical);

    EXPECT_EQ(v1 == v2, false);
}
