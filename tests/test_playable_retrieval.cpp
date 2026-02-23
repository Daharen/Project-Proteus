#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"
#include "proteus/playable/retrieval_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

void upsert_bandit_state(
    proteus::persistence::SqliteDb& db,
    const std::string& key,
    double epsilon,
    double lr,
    const std::vector<double>& weights
) {
    nlohmann::json state;
    state["policy_version"] = key;
    state["epsilon"] = epsilon;
    state["lr"] = lr;
    state["feature_version"] = "v1";
    nlohmann::json arr = nlohmann::json::array({});
    for (double w : weights) {
        arr.push_back(w);
    }
    state["weight_vector"] = arr;

    auto stmt = db.prepare(
        "INSERT INTO bandit_state(key, value_json, updated_at) VALUES(?1, ?2, strftime('%s','now')) "
        "ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json, updated_at=excluded.updated_at;"
    );
    stmt.bind_text(1, key);
    stmt.bind_text(2, state.dump());
    stmt.step();
}

}  // namespace

TEST(PlayableCoreTest, CandidateSeedingAndStability) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_candidates.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, proteus::playable::kPlayableCorePolicyVersion, 0.0, 0.01, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector(db, proteus::playable::kPlayableCorePolicyVersion);

    const proteus::playable::RetrievalRequest req{.domain = "rpg", .raw_prompt = "help me find a quest", .session_id = "s1"};
    const auto first = proteus::playable::run_retrieval(db, req, selector);
    const auto hash = proteus::playable::compute_prompt_hash(req.policy_version, req.domain, proteus::playable::canonicalize_prompt(req.raw_prompt));

    const auto ids1 = proteus::playable::list_prompt_candidate_ids(db, hash);
    EXPECT_EQ(ids1.size(), proteus::playable::kMinCandidates);

    const auto second = proteus::playable::run_retrieval(db, req, selector);
    const auto ids2 = proteus::playable::list_prompt_candidate_ids(db, hash);
    EXPECT_EQ(ids2.size(), proteus::playable::kMinCandidates);
    EXPECT_EQ(ids1.front(), ids2.front());
    EXPECT_EQ(first.at("proposal_id").get<std::string>(), second.at("proposal_id").get<std::string>());

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, BanditSelectionDeterministicForEpsilonExtremes) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_bandit_eps.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, "eps0", 0.0, 0.01, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector0(db, "eps0");
    const proteus::playable::RetrievalRequest req0{.domain = "rpg", .raw_prompt = "alpha", .session_id = "s-0", .policy_version = "eps0"};
    const auto a = proteus::playable::run_retrieval(db, req0, selector0);
    const auto b = proteus::playable::run_retrieval(db, req0, selector0);
    EXPECT_EQ(a.at("proposal_id").get<std::string>(), b.at("proposal_id").get<std::string>());

    upsert_bandit_state(db, "eps1", 1.0, 0.01, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector1(db, "eps1");
    const proteus::playable::RetrievalRequest req1{.domain = "rpg", .raw_prompt = "beta", .session_id = "s-1", .policy_version = "eps1"};
    const auto c = proteus::playable::run_retrieval(db, req1, selector1);
    const auto d = proteus::playable::run_retrieval(db, req1, selector1);
    EXPECT_EQ(c.at("proposal_id").get<std::string>(), d.at("proposal_id").get<std::string>());

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, RewardUpdateLearnsAndPersistsState) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_bandit_learn.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, "learn", 0.0, 0.05, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector(db, "learn");

    const proteus::playable::RetrievalRequest req{.domain = "rpg", .raw_prompt = "gamma", .session_id = "s-learn", .policy_version = "learn"};
    const auto first = proteus::playable::run_retrieval(db, req, selector);
    const auto proposal_id = first.at("proposal_id").get<std::string>();

    for (int i = 0; i < 5; ++i) {
        proteus::playable::log_reward(db, selector, "s-learn", proposal_id, 1.0);
    }

    auto stmt = db.prepare("SELECT value_json FROM bandit_state WHERE key = ?1;");
    stmt.bind_text(1, "learn");
    EXPECT_EQ(stmt.step(), true);
    const auto saved = nlohmann::json::parse(stmt.column_text(0));
    EXPECT_EQ(saved.contains("weight_vector"), true);

    proteus::playable::BanditSelector reloaded(db, "learn");
    const auto second = proteus::playable::run_retrieval(db, req, reloaded);
    EXPECT_EQ(second.contains("proposal_id"), true);

    std::filesystem::remove(db_path);
}


TEST(PlayableCoreTest, RewardDedupAppliesOnlyOnceAndTracksStats) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_reward_dedup.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, "dedup", 0.0, 0.05, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector(db, "dedup");

    const proteus::playable::RetrievalRequest req{.domain = "rpg", .raw_prompt = "delta", .session_id = "s-dedup", .policy_version = "dedup"};
    const auto first = proteus::playable::run_retrieval(db, req, selector);
    const auto proposal_id = first.at("proposal_id").get<std::string>();

    const bool first_apply = proteus::playable::log_reward_interaction_once(db, "s-dedup", proposal_id, 1.0);
    const bool second_apply = proteus::playable::log_reward_interaction_once(db, "s-dedup", proposal_id, 1.0);
    EXPECT_EQ(first_apply, true);
    EXPECT_EQ(second_apply, false);

    proteus::playable::update_proposal_stats_on_reward(db, proposal_id, 1.0);
    auto stmt = db.prepare("SELECT shown_count, reward_sum, reward_count FROM proposal_stats WHERE proposal_id = ?1;");
    stmt.bind_text(1, proposal_id);
    EXPECT_EQ(stmt.step(), true);
    EXPECT_GT(stmt.column_int64(0), 0);
    EXPECT_GT(stmt.column_double(1), 0.9);
    EXPECT_EQ(stmt.column_int64(2), 1);

    std::filesystem::remove(db_path);
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

TEST(PlayableCoreTest, CandidateReseedingUsesPromptRegenCounter) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_reseed_regen.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, "regen", 0.0, 0.01, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector(db, "regen");

    const proteus::playable::RetrievalRequest req{.domain = "rpg", .raw_prompt = "regen target", .session_id = "regen-s", .policy_version = "regen"};
    proteus::playable::run_retrieval(db, req, selector);

    const auto hash = proteus::playable::compute_prompt_hash(req.policy_version, req.domain, proteus::playable::canonicalize_prompt(req.raw_prompt));
    auto ids = proteus::playable::list_prompt_candidate_ids(db, hash);
    ASSERT_EQ(ids.size(), proteus::playable::kMinCandidates);

    proteus::playable::remove_prompt_candidate(db, hash, ids.front());
    EXPECT_EQ(proteus::playable::list_prompt_candidate_ids(db, hash).size(), proteus::playable::kMinCandidates - 1);

    proteus::playable::run_retrieval(db, req, selector);
    ids = proteus::playable::list_prompt_candidate_ids(db, hash);
    EXPECT_EQ(ids.size(), proteus::playable::kMinCandidates);
    EXPECT_EQ(proteus::playable::get_prompt_regen_count(db, hash), 1);

    std::filesystem::remove(db_path);
}

TEST(PlayableCoreTest, CandidatePruningRemovesWeakArmsWhenAboveMin) {
    const std::filesystem::path db_path = std::filesystem::temp_directory_path() / "proteus_test_prune.db";
    std::filesystem::remove(db_path);

    proteus::persistence::SqliteDb db;
    db.open(db_path.string());
    proteus::persistence::ensure_schema(db);

    upsert_bandit_state(db, "prune", 0.0, 0.01, std::vector<double>(18, 0.0));
    proteus::playable::BanditSelector selector(db, "prune");

    const proteus::playable::RetrievalRequest req{.domain = "rpg", .raw_prompt = "prune target", .session_id = "prune-s", .policy_version = "prune"};
    proteus::playable::run_retrieval(db, req, selector);

    const auto hash = proteus::playable::compute_prompt_hash(req.policy_version, req.domain, proteus::playable::canonicalize_prompt(req.raw_prompt));
    const std::string extra_id = "extra-low";
    nlohmann::json payload = {
        {"proposal_id", extra_id},
        {"domain", "rpg"},
        {"type", "quest_variant"},
        {"text", "weak arm"},
        {"tags", nlohmann::json::array({"weak"})}
    };
    proteus::playable::upsert_proposal_registry(db, extra_id, "rpg", payload, "test", 1);
    proteus::playable::insert_prompt_candidate(db, proteus::playable::PromptCandidateRecord{.prompt_hash = hash, .proposal_id = extra_id, .weight = 1.0, .created_at = 1});
    db.exec("INSERT INTO proposal_stats(proposal_id, shown_count, reward_sum, reward_count, last_shown_at) VALUES('extra-low', 5, 0.0, 4, 1) "
            "ON CONFLICT(proposal_id) DO UPDATE SET shown_count=5, reward_sum=0.0, reward_count=4, last_shown_at=1;");

    EXPECT_EQ(proteus::playable::list_prompt_candidate_ids(db, hash).size(), proteus::playable::kMinCandidates + 1);
    proteus::playable::run_retrieval(db, req, selector);

    const auto ids = proteus::playable::list_prompt_candidate_ids(db, hash);
    EXPECT_EQ(ids.size(), proteus::playable::kMinCandidates);
    EXPECT_EQ(std::find(ids.begin(), ids.end(), extra_id) == ids.end(), true);

    std::filesystem::remove(db_path);
}
