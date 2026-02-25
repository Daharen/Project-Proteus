#include "proteus/bootstrap/import_novel_query_artifact.hpp"
#include "proteus/llm/llm_cache_client.hpp"
#include "test_db_raii.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

TEST(LlmBootstrapTest, OfflineCacheMissIsDeterministicAndNoRowsWritten) {
    {
        proteus::tests::TestSqliteDbFile test_db("offline_cache_miss");
        auto& db = test_db.db();

        const auto request = proteus::llm::BuildDeterministicRequest("openai", "gpt-4.1-mini", "proteus_funnel_bootstrap_v1", 1, "novel query text");
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
        proteus::tests::TestSqliteDbFile test_db("cache_replay");
        auto& db = test_db.db();

        const auto request = proteus::llm::BuildDeterministicRequest("openai", "gpt-4.1-mini", "proteus_funnel_bootstrap_v1", 1, "fixed prompt");
        const std::string artifact = R"({"normalized_query_text":"fixed prompt","intent_tags":["tag1"],"synopsis":"A synopsis","proposals":[{"proposal_id":"p1","proposal_kind":1,"proposal_title":"A","proposal_body":"B","proposal_json":{"mode":"candidate_set","name":"A"}},{"proposal_id":"p2","proposal_kind":1,"proposal_title":"C","proposal_body":"D","proposal_json":{"mode":"candidate_set","name":"C"}},{"proposal_id":"p3","proposal_kind":1,"proposal_title":"E","proposal_body":"F","proposal_json":{"mode":"candidate_set","name":"E"}}],"safety_flags":[]})";

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
        proteus::tests::TestSqliteDbFile test_db("import_det");
        auto& db = test_db.db();

        const std::string artifact = R"({"normalized_query_text":"where healer","intent_tags":["healer","quest"],"synopsis":"Seek healer herbs around temples.","proposals":[{"proposal_id":"p1","proposal_kind":1,"proposal_title":"Temple","proposal_body":"Ask priests.","proposal_json":{"mode":"candidate_set","name":"Temple"}},{"proposal_id":"p2","proposal_kind":1,"proposal_title":"Forest","proposal_body":"Search glades.","proposal_json":{"mode":"candidate_set","name":"Forest"}},{"proposal_id":"p3","proposal_kind":1,"proposal_title":"Bandit","proposal_body":"Loot caches.","proposal_json":{"mode":"candidate_set","name":"Bandit"}}],"safety_flags":[]})";

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
        proteus::tests::TestSqliteDbFile test_db("import_caps");
        auto& db = test_db.db();

        const std::string long_text(500, 'z');
        const std::string artifact =
            "{\"normalized_query_text\":\"caps\",\"intent_tags\":[\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\",\"" + long_text + "\"],"
            "\"synopsis\":\"" + long_text + "\",\"proposals\":[{\"proposal_id\":\"p1\",\"proposal_kind\":1,\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"proposal_json\":{\"mode\":\"candidate_set\",\"name\":\"" + long_text + "\"}},{\"proposal_id\":\"p2\",\"proposal_kind\":1,\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"proposal_json\":{\"mode\":\"candidate_set\",\"name\":\"" + long_text + "\"}},{\"proposal_id\":\"p3\",\"proposal_kind\":1,\"proposal_title\":\"" + long_text + "\",\"proposal_body\":\"" + long_text + "\",\"proposal_json\":{\"mode\":\"candidate_set\",\"name\":\"" + long_text + "\"}}],\"safety_flags\":[]}";

        ASSERT_EQ(proteus::bootstrap::ImportNovelQueryArtifact(db, "player_B", "session_2", "caps input", artifact, 1), true);

        auto m = db.prepare("SELECT LENGTH(synopsis) FROM query_metadata LIMIT 1;");
        ASSERT_EQ(m.step(), true);
        EXPECT_EQ(m.column_int64(0), 240);

        auto p_stmt = db.prepare("SELECT LENGTH(proposal_title), LENGTH(proposal_body), LENGTH(choice_seed_hint) FROM query_bootstrap_proposals ORDER BY proposal_index ASC LIMIT 1;");
        ASSERT_EQ(p_stmt.step(), true);
        EXPECT_EQ(p_stmt.column_int64(0), 48);
        EXPECT_EQ(p_stmt.column_int64(1), 240);
        EXPECT_EQ(p_stmt.column_int64(2) >= 0, true);
    }
}

TEST(LlmBootstrapTest, DbHandleClosesAndFileIsDeletable) {
    std::filesystem::path db_path;
    {
        proteus::tests::TestSqliteDbFile test_db("close_delete");
        db_path = test_db.path();
        EXPECT_EQ(test_db.db().is_open(), true);
        test_db.db().Close();
        EXPECT_EQ(test_db.db().is_open(), false);
    }
    EXPECT_EQ(std::filesystem::exists(db_path), false);
}


TEST(LlmBootstrapTest, NpcIdIsStableAcrossReruns) {
    proteus::tests::TestSqliteDbFile test_db("npc_id_stable");
    auto& db = test_db.db();
    const std::string artifact = R"({"normalized_query_text":"find smith","intent_tags":["smith"],"synopsis":"Find a smith.","proposals":[{"proposal_id":"n1","proposal_kind":3,"proposal_title":"Borin","proposal_body":"Blacksmith","proposal_json":{"mode":"candidate_set","name":"Borin"}},{"proposal_id":"n2","proposal_kind":3,"proposal_title":"Mira","proposal_body":"Alchemist","proposal_json":{"mode":"candidate_set","name":"Mira"}},{"proposal_id":"n3","proposal_kind":3,"proposal_title":"Tarn","proposal_body":"Captain","proposal_json":{"mode":"candidate_set","name":"Tarn"}}],"safety_flags":[]})";
    ASSERT_EQ(proteus::bootstrap::ImportBootstrapArtifactForDomain(db, "p", "s", "find smith", proteus::query::QueryDomain::NpcIntent, artifact, 1), true);

    const auto qid = proteus::query::GetOrCreateQueryId(db, "find smith", proteus::query::QueryDomain::NpcIntent);
    const auto id1 = proteus::bootstrap::DeterministicNpcId(qid, "Borin", "Blacksmith", 1);
    const auto id2 = proteus::bootstrap::DeterministicNpcId(qid, "Borin", "Blacksmith", 1);
    EXPECT_EQ(id1, id2);
}

TEST(LlmBootstrapTest, DialogueProposalIdsStableAcrossReruns) {
    proteus::tests::TestSqliteDbFile test_db("dialogue_stable");
    auto& db = test_db.db();
    const std::string artifact = R"({"normalized_query_text":"talk to smith","intent_tags":["dialogue"],"synopsis":"Talk options.","proposals":[{"proposal_id":"d1","proposal_kind":4,"proposal_title":"Trade?","proposal_body":"Ask to trade","proposal_json":{"mode":"dialogue_options","utterance":"Trade?","intent_tag":"trade","tone":"polite"}},{"proposal_id":"d2","proposal_kind":4,"proposal_title":"Rumors","proposal_body":"Ask rumors","proposal_json":{"mode":"dialogue_options","utterance":"Tell me rumors","intent_tag":"rumors","tone":"curious"}},{"proposal_id":"d3","proposal_kind":4,"proposal_title":"Move","proposal_body":"Threaten","proposal_json":{"mode":"dialogue_options","utterance":"Stand aside","intent_tag":"intimidate","tone":"blunt"}}],"safety_flags":[]})";

    ASSERT_EQ(proteus::bootstrap::ImportBootstrapArtifactForDomain(db, "p", "s", "talk to smith", proteus::query::QueryDomain::DialogueOption, artifact, 1), true);
    std::vector<std::string> first_ids;
    auto f = db.prepare("SELECT proposal_id FROM query_bootstrap_proposals ORDER BY proposal_index ASC;");
    while (f.step()) first_ids.push_back(f.column_text(0));
    ASSERT_EQ(proteus::bootstrap::ImportBootstrapArtifactForDomain(db, "p", "s", "talk to smith", proteus::query::QueryDomain::DialogueOption, artifact, 1), true);
    std::vector<std::string> second_ids;
    auto g = db.prepare("SELECT proposal_id FROM query_bootstrap_proposals ORDER BY proposal_index ASC;");
    while (g.step()) second_ids.push_back(g.column_text(0));
    EXPECT_EQ(first_ids, second_ids);
}

TEST(LlmBootstrapTest, OfflineMissDoesNotMutateBootstrapTables) {
    proteus::tests::TestSqliteDbFile test_db("offline_nomutate");
    auto& db = test_db.db();
    const auto request = proteus::llm::BuildDeterministicRequest("openai", "gpt-4.1-mini", "proteus_funnel_bootstrap_v1", 1, "brand new class");
    proteus::llm::LlmCacheClient client;
    const auto r = client.TryGetOrCaptureArtifact(db, request, proteus::llm::LlmMode::Offline);
    ASSERT_EQ(r.status, proteus::llm::LlmArtifactStatus::CacheMissOffline);

    auto m = db.prepare("SELECT COUNT(*) FROM query_metadata;"); ASSERT_EQ(m.step(), true); EXPECT_EQ(m.column_int64(0), 0);
    auto pcount = db.prepare("SELECT COUNT(*) FROM query_bootstrap_proposals;"); ASSERT_EQ(pcount.step(), true); EXPECT_EQ(pcount.column_int64(0), 0);
    auto n = db.prepare("SELECT COUNT(*) FROM npc_registry;"); ASSERT_EQ(n.step(), true); EXPECT_EQ(n.column_int64(0), 0);
}
