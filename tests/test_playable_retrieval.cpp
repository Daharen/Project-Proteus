#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_selector.hpp"
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

TEST(PlayableCoreTest, CacheHitReturnsSameProposalIdAndRegistryDedupes) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_cache_hits.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    const proteus::playable::DeterministicSelector selector;
    const proteus::playable::RetrievalRequest request{
        .domain = "rpg",
        .raw_prompt = "help me find a quest",
        .session_id = "s-cache",
    };

    const auto first = proteus::playable::run_retrieval(db, request, selector);
    const auto second = proteus::playable::run_retrieval(db, request, selector);

    const auto first_id = first.at("proposal_id").get<std::string>();
    const auto second_id = second.at("proposal_id").get<std::string>();
    EXPECT_EQ(first_id, second_id);
    EXPECT_EQ(proteus::playable::count_proposal_registry_rows(db, first_id), 1);

    const auto hash = proteus::playable::compute_prompt_hash(
        request.policy_version,
        request.domain,
        proteus::playable::canonicalize_prompt(request.raw_prompt)
    );
    const auto cached = proteus::playable::find_prompt_cache(db, hash);
    EXPECT_EQ(cached.has_value(), true);
    EXPECT_EQ(cached->proposal_id, first_id);
    EXPECT_EQ(cached->hit_count, 1);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, InteractionLogRowsAndNoveltyFlagsAreCorrect) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_interaction.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    const proteus::playable::DeterministicSelector selector;
    const std::string session_id = "s-interaction";
    const proteus::playable::RetrievalRequest request{
        .domain = "rpg",
        .raw_prompt = "help me find a quest",
        .session_id = session_id,
    };

    const auto first = proteus::playable::run_retrieval(db, request, selector);
    const auto proposal_id = first.at("proposal_id").get<std::string>();
    const auto latest_miss = proteus::playable::latest_interaction_for_session_and_arm(db, session_id, proposal_id);
    EXPECT_EQ(latest_miss.has_value(), true);
    EXPECT_EQ(latest_miss->novelty_flag, 1);
    EXPECT_EQ(latest_miss->reward_is_null, true);

    (void)proteus::playable::run_retrieval(db, request, selector);
    const auto latest_hit = proteus::playable::latest_interaction_for_session_and_arm(db, session_id, proposal_id);
    EXPECT_EQ(latest_hit.has_value(), true);
    EXPECT_EQ(latest_hit->novelty_flag, 0);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, RewardUpdateModifiesLatestMatchingInteraction) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_reward.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    const proteus::playable::DeterministicSelector selector;
    const std::string session_id = "s-reward";
    const proteus::playable::RetrievalRequest request{
        .domain = "rpg",
        .raw_prompt = "help me find a quest",
        .session_id = session_id,
    };

    const auto result = proteus::playable::run_retrieval(db, request, selector);
    const auto proposal_id = result.at("proposal_id").get<std::string>();

    proteus::playable::log_reward(db, session_id, proposal_id, 0.75);
    const auto latest = proteus::playable::latest_interaction_for_session_and_arm(db, session_id, proposal_id);
    EXPECT_EQ(latest.has_value(), true);
    EXPECT_EQ(latest->reward_is_null, false);
    EXPECT_GT(latest->reward_signal, 0.74);
    EXPECT_GT(0.76, latest->reward_signal);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, PolicyVersionChangeSeparatesCacheKeys) {
    const std::string canonical = proteus::playable::canonicalize_prompt("help me find a quest");
    const auto v1 = proteus::playable::compute_prompt_hash("playable_core_v1", "rpg", canonical);
    const auto v2 = proteus::playable::compute_prompt_hash("playable_core_v2", "rpg", canonical);

    EXPECT_EQ(v1 == v2, false);
}

TEST(PlayableCoreTest, SchemaValidationRejectsMalformedProposal) {
    nlohmann::json invalid = {
        {"proposal_id", "bad"},
        {"domain", "rpg"},
        {"type", "quest_variant"},
        {"text", ""},
        {"tags", nlohmann::json::array({1, 2, 3})},
    };

    const auto report = proteus::playable::validate_proposal_json(invalid);
    EXPECT_EQ(report.ok, false);
}
