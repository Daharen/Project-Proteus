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

TEST(OpenAiResponsesClientTest, BuildsStructuredOutputPayloadWithSchemaOnTextFormat) {
    const proteus::llm::LlmRequest request{
        .provider = "openai",
        .model = "gpt-4.1-mini",
        .schema_name = "ignored_for_responses_format",
        .prompt_text = "hello"
    };

    const auto payload = proteus::llm::openai::build_openai_responses_payload(request);

    ASSERT_EQ(payload.contains("text"), true);
    ASSERT_EQ(payload.at("text").contains("format"), true);

    const auto& format = payload.at("text").at("format");
    EXPECT_EQ(format.at("type").get<std::string>(), "json_schema");
    EXPECT_EQ(format.at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(format.at("strict").get<bool>(), true);
    EXPECT_EQ(format.contains("schema"), true);
    EXPECT_EQ(format.contains("json_schema"), false);

    const auto& schema = format.at("schema");
    EXPECT_EQ(schema.at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(schema.at("type").get<std::string>(), "object");
    EXPECT_EQ(schema.contains("properties"), true);
    EXPECT_EQ(schema.at("properties").contains("normalized_query_text"), true);
    EXPECT_EQ(schema.contains("required"), true);
}
