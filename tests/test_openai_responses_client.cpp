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
