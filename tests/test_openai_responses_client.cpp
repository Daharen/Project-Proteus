#include "proteus/llm/openai/openai_responses_client.hpp"

#include <gtest/gtest.h>

TEST(OpenAiResponsesClientTest, ExtractsOutputTextFromOutputContentArray) {
    const auto payload = nlohmann::json::parse(R"({
        "id":"resp_123",
        "output":[
            {"content":[{"type":"output_text","text":"Hello"}]}
        ]
    })");

    EXPECT_EQ(proteus::llm::openai::extract_output_text_from_responses_json(payload), "Hello");
}

TEST(OpenAiResponsesClientTest, FallsBackToTopLevelOutputText) {
    const auto payload = nlohmann::json::parse(R"({"output_text":"Fallback"})");

    EXPECT_EQ(proteus::llm::openai::extract_output_text_from_responses_json(payload), "Fallback");
}

TEST(OpenAiResponsesClientTest, BuildsCandidateSetSchemaWhenPromptDimensionIsClass) {
    const proteus::llm::LlmRequest request{
        .provider = "openai",
        .model = "gpt-4.1-mini",
        .schema_name = "proteus_funnel_bootstrap_v1",
        .prompt_text = "Dimension: class. Return strict JSON using proposal_json.mode=candidate_set"
    };

    const auto payload = proteus::llm::openai::build_openai_responses_payload(request);

    const auto dump = payload.dump();
    EXPECT_EQ(dump.find("proteus_funnel_bootstrap_v1") != std::string::npos, true);
    EXPECT_EQ(dump.find("candidate_set") != std::string::npos, true);
}

TEST(OpenAiResponsesClientTest, BuildsDialogueSchemaWhenPromptDimensionIsDialogue) {
    const proteus::llm::LlmRequest request{
        .provider = "openai",
        .model = "gpt-4.1-mini",
        .schema_name = "proteus_funnel_bootstrap_v1",
        .prompt_text = "Dimension: dialogue. Return strict JSON using proposal_json.mode=dialogue_options"
    };

    const auto payload = proteus::llm::openai::build_openai_responses_payload(request);

    const auto dump = payload.dump();
    EXPECT_EQ(dump.find("dialogue_options") != std::string::npos, true);
    EXPECT_EQ(dump.find("intent_tag") != std::string::npos, true);
}
