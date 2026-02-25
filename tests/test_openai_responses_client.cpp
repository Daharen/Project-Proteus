#include "proteus/llm/openai/openai_responses_client.hpp"

#include <gtest/gtest.h>

namespace {

proteus::llm::LlmRequest bootstrap_req(
    proteus::bootstrap::DimensionKind kind,
    const std::string& prompt
) {
    return proteus::llm::LlmRequest{
        .request_kind = proteus::llm::LlmRequestKind::BootstrapFunnel,
        .dimension_kind = kind,
        .provider = "openai",
        .model = "gpt-4.1-mini",
        .schema_name = "attempted_override_name",
        .prompt_text = prompt,
    };
}

}  // namespace

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

TEST(OpenAiResponsesClientTest, PromptTextMutationDoesNotAffectBootstrapSchemaOrName) {
    const auto request_a = bootstrap_req(proteus::bootstrap::DimensionKind::Class, "Dimension: dialogue should not matter");
    const auto request_b = bootstrap_req(proteus::bootstrap::DimensionKind::Class, "Totally different prompt text");

    const auto payload_a = proteus::llm::openai::build_openai_responses_payload(request_a);
    const auto payload_b = proteus::llm::openai::build_openai_responses_payload(request_b);

    EXPECT_EQ(payload_a.at("text").at("format").at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(payload_b.at("text").at("format").at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(payload_a.at("text").at("format").at("schema").dump(), payload_b.at("text").at("format").at("schema").dump());
}

TEST(OpenAiResponsesClientTest, BootstrapNamingIsInvariantAcrossDimensions) {
    const auto class_payload = proteus::llm::openai::build_openai_responses_payload(bootstrap_req(proteus::bootstrap::DimensionKind::Class, "class text"));
    const auto skill_payload = proteus::llm::openai::build_openai_responses_payload(bootstrap_req(proteus::bootstrap::DimensionKind::Skill, "skill text"));
    const auto dialogue_payload = proteus::llm::openai::build_openai_responses_payload(bootstrap_req(proteus::bootstrap::DimensionKind::Dialogue, "dialogue text"));

    EXPECT_EQ(class_payload.at("text").at("format").at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(skill_payload.at("text").at("format").at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");
    EXPECT_EQ(dialogue_payload.at("text").at("format").at("name").get<std::string>(), "proteus_funnel_bootstrap_v1");

    EXPECT_EQ(class_payload.at("text").at("format").at("schema").dump() == dialogue_payload.at("text").at("format").at("schema").dump(), false);
}

TEST(OpenAiResponsesClientTest, BootstrapSchemaBodyDoesNotContainTopLevelName) {
    const auto payload = proteus::llm::openai::build_openai_responses_payload(
        bootstrap_req(proteus::bootstrap::DimensionKind::Class, "class text")
    );

    EXPECT_EQ(payload.at("text").at("format").at("schema").contains("name"), false);
}

TEST(OpenAiResponsesClientTest, BootstrapWithoutDimensionFailsDeterministically) {
    const proteus::llm::LlmRequest request{
        .request_kind = proteus::llm::LlmRequestKind::BootstrapFunnel,
        .dimension_kind = static_cast<proteus::bootstrap::DimensionKind>(-1),
        .provider = "openai",
        .model = "gpt-4.1-mini",
        .schema_name = "ignored",
        .prompt_text = "any"
    };

    bool threw = false;
    try {
        (void)proteus::llm::openai::build_openai_responses_payload(request);
    } catch (const std::runtime_error& ex) {
        threw = true;
        EXPECT_EQ(std::string(ex.what()), "OPENAI_BOOTSTRAP_DIMENSION_KIND_INVALID");
    }
    EXPECT_EQ(threw, true);
}
