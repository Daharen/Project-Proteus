#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"
#include "proteus/playable/canonicalize.hpp"
#include "proteus/playable/prompt_cache.hpp"
#include "proteus/playable/proposal_schema.hpp"
#include "proteus/playable/retrieval_engine.hpp"

#include <gtest/gtest.h>

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
