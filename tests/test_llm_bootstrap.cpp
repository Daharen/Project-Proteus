#include "proteus/bootstrap/import_novel_query_artifact.hpp"
#include "proteus/llm/llm_cache_client.hpp"
#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class TestDbLifetime {
public:
    explicit TestDbLifetime(const std::string& test_name) {
        const auto count = counter_.fetch_add(1);
        const std::string key = test_name + ":" + std::to_string(count);
        const auto suffix = static_cast<unsigned long long>(std::hash<std::string>{}(key) & 0xFFFFFFFFULL);

        std::ostringstream name;
        name << "proteus_test_llm_" << std::hex << suffix << ".db";
        path_ = std::filesystem::temp_directory_path() / name.str();

        std::filesystem::remove(path_);
        db_.open(path_.string());
        proteus::persistence::ensure_schema(db_);
    }

    ~TestDbLifetime() {
        db_.Close();
        std::filesystem::remove(path_);
    }

    proteus::persistence::SqliteDb& db() { return db_; }
    const std::filesystem::path& path() const { return path_; }

private:
    static std::atomic<std::uint64_t> counter_;
    std::filesystem::path path_;
    proteus::persistence::SqliteDb db_;
};

std::atomic<std::uint64_t> TestDbLifetime::counter_{0};

}  // namespace

TEST(LlmBootstrapTest, OfflineCacheMissIsDeterministicAndNoRowsWritten) {
    {
        TestDbLifetime test_db("offline_cache_miss");
        auto& db = test_db.db();

        const auto request = proteus::llm::BuildDeterministicRequest("openai", "gpt-4.1-mini", "proteus_novel_query_bootstrap", 1, "novel query text");
        proteus::llm::LlmCacheClient client;
        const auto result = client.TryGetOrCaptureArtifact(db, request, proteus::llm::LlmMode::Offline);
        EXPECT_EQ(result.status, proteus::llm::LlmArtifactStatus::CacheMissOffline);

        auto stmt = db.prepare("SELECT COUNT(*) FROM llm_response_cache;");
        ASSERT_EQ(stmt.step(), true);
        EXPECT_EQ(stmt.column_int64(0), 0);
    }
}

TEST(LlmBootstrapTest, CacheReplayIsByteIdentical) {
    {
        TestDbLifetime test_db("cache_replay");
        auto& db = test_db.db();

        const auto request = proteus::llm::BuildDeterministicRequest("openai", "gpt-4.1-mini", "proteus_novel_query_bootstrap", 1, "fixed prompt");
        const std::string artifact = R"({"normalized_query_text":"fixed prompt","intent_tags":["tag1"],"synopsis":"A synopsis","proposals":[{"proposal_title":"A","proposal_body":"B","choice_seed_hint":"H1","risk_profile":"low"},{"proposal_title":"C","proposal_body":"D","choice_seed_hint":"H2","risk_profile":"medium"},{"proposal_title":"E","proposal_body":"F","choice_seed_hint":"H3","risk_profile":"high"}],"safety_flags":[]})";

        auto ins = db.prepare(
            "INSERT INTO llm_response_cache(created_at_utc, provider, model, schema_name, schema_version, prompt_hash, request_json, response_json, response_sha256, raw_response_text, raw_response_text_trunc, error_code) "
            "VALUES('2020-01-01T00:00:00.000Z', ?1, ?2, ?3, ?4, ?5, ?6, ?7, 'h', ?8, ?9, NULL);"
        );
        ins.bind_text(1, request.provider);
        ins.bind_text(2, request.model);
        ins.bind_text(3, request.schema_name);
        ins.bind_int64(4, request.schema_version);
        ins.bind_text(5, request.prompt_hash_hex);
        ins.bind_text(6, request.request_json);
        ins.bind_text(7, artifact);
        ins.bind_text(8, artifact);
        ins.bind_text(9, artifact.substr(0, 50));
        ins.step();

        proteus::llm::LlmCacheClient client;
        const auto first = client.TryGetOrCaptureArtifact(db, request, proteus::llm::LlmMode::Offline);
        const auto second = client.TryGetOrCaptureArtifact(db, request, proteus::llm::LlmMode::Offline);
        EXPECT_EQ(first.status, proteus::llm::LlmArtifactStatus::CacheHit);
        EXPECT_EQ(second.status, proteus::llm::LlmArtifactStatus::CacheHit);
        EXPECT_EQ(first.artifact_json, second.artifact_json);
    }
}

TEST(LlmBootstrapTest, ImportDeterminismProposalIdsStableAcrossReruns) {
    {
        TestDbLifetime test_db("import_det");
        auto& db = test_db.db();

        const std::string artifact = R"({"normalized_query_text":"where healer","intent_tags":["healer","quest"],"synopsis":"Seek healer herbs around temples.","proposals":[{"proposal_title":"Temple Route","proposal_body":"Ask priests.","choice_seed_hint":"route-1","risk_profile":"low"},{"proposal_title":"Forest Route","proposal_body":"Search glades.","choice_seed_hint":"route-2","risk_profile":"medium"},{"proposal_title":"Bandit Route","proposal_body":"Loot caches.","choice_seed_hint":"route-3","risk_profile":"high"}],"safety_flags":[]})";

        ASSERT_EQ(proteus::bootstrap::ImportNovelQueryArtifact(db, "player_B", "session_1", "Where healer herbs?", artifact, 1), true);
        std::vector<std::string> first_ids;
        auto first = db.prepare("SELECT proposal_id FROM query_bootstrap_proposals ORDER BY proposal_index ASC;");
        while (first.step()) {
            first_ids.push_back(first.column_text(0));
        }

        ASSERT_EQ(proteus::bootstrap::ImportNovelQueryArtifact(db, "player_B", "session_1", "Where healer herbs?", artifact, 1), true);
        std::vector<std::string> second_ids;
        auto second = db.prepare("SELECT proposal_id FROM query_bootstrap_proposals ORDER BY proposal_index ASC;");
        while (second.step()) {
            second_ids.push_back(second.column_text(0));
        }

        EXPECT_EQ(first_ids, second_ids);
        EXPECT_EQ(first_ids.size(), 3);
    }
}

TEST(LlmBootstrapTest, ImportAppliesDeterministicStringCaps) {
    {
        TestDbLifetime test_db("import_caps");
        auto& db = test_db.db();

        const std::string long_text(500, 'z');
        const std::string artifact =
            "{\"normalized_query_text\":\"caps\",\"intent_tags\":[\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\"],"
            "\"synopsis\":\"" + long_text + "\",\"proposals\":[{\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"choice_seed_hint\":\"" + long_text + "\",\"risk_profile\":\"low\"},{\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"choice_seed_hint\":\"" + long_text + "\",\"risk_profile\":\"medium\"},{\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"choice_seed_hint\":\"" + long_text + "\",\"risk_profile\":\"high\"}],\"safety_flags\":[]}";

        ASSERT_EQ(proteus::bootstrap::ImportNovelQueryArtifact(db, "player_B", "session_2", "caps input", artifact, 1), true);

        auto m = db.prepare("SELECT LENGTH(synopsis) FROM query_metadata LIMIT 1;");
        ASSERT_EQ(m.step(), true);
        EXPECT_EQ(m.column_int64(0), 240);

        auto p_stmt = db.prepare("SELECT LENGTH(proposal_title), LENGTH(proposal_body), LENGTH(choice_seed_hint) FROM query_bootstrap_proposals ORDER BY proposal_index ASC LIMIT 1;");
        ASSERT_EQ(p_stmt.step(), true);
        EXPECT_EQ(p_stmt.column_int64(0), 48);
        EXPECT_EQ(p_stmt.column_int64(1), 240);
        EXPECT_EQ(p_stmt.column_int64(2), 48);
    }
}

TEST(LlmBootstrapTest, DbHandleClosesAndFileIsDeletable) {
    std::filesystem::path db_path;
    {
        TestDbLifetime test_db("close_delete");
        db_path = test_db.path();
        EXPECT_EQ(test_db.db().is_open(), true);
        test_db.db().Close();
        EXPECT_EQ(test_db.db().is_open(), false);
    }
    EXPECT_EQ(std::filesystem::exists(db_path), false);
}
