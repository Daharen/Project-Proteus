#include "gtest/gtest.h"

#include "proteus/responders/response_validation.h"

namespace {

using proteus::responders::ContentBlock;
using proteus::responders::ContentBlockKind;
using proteus::responders::EnvelopeValidationCode;
using proteus::responders::ResponseEnvelope;
using proteus::responders::ValidateEnvelopeDeterministic;

ResponseEnvelope valid_envelope() {
    ResponseEnvelope envelope;
    envelope.query_id = 1;
    envelope.stable_player_id = "player";
    envelope.content_blocks = {
        ContentBlock{.kind = ContentBlockKind::Text, .text = "hello", .options = {}, .ordinal = 0},
    };
    envelope.provenance_tag = "deterministic_stub:v1";
    envelope.schema_version_tag = 1;
    return envelope;
}

TEST(ResponseValidation, TooManyBlocksFails) {
    ResponseEnvelope envelope = valid_envelope();
    envelope.content_blocks.clear();
    for (std::uint32_t i = 0; i < 9; ++i) {
        envelope.content_blocks.push_back(ContentBlock{.kind = ContentBlockKind::Text, .text = "x", .options = {}, .ordinal = i});
    }

    const auto result = ValidateEnvelopeDeterministic(envelope);
    EXPECT_EQ(result.code, EnvelopeValidationCode::TooManyBlocks);
}

TEST(ResponseValidation, TextTooLongFails) {
    ResponseEnvelope envelope = valid_envelope();
    envelope.content_blocks[0].text = std::string(513, 'a');

    const auto result = ValidateEnvelopeDeterministic(envelope);
    EXPECT_EQ(result.code, EnvelopeValidationCode::BlockTextTooLong);
}

TEST(ResponseValidation, InvalidIntentTokenFails) {
    ResponseEnvelope envelope = valid_envelope();
    envelope.mechanical_intents = {"valid_token", "bad-token"};

    const auto result = ValidateEnvelopeDeterministic(envelope);
    EXPECT_EQ(result.code, EnvelopeValidationCode::MechanicalIntentInvalidToken);
}

}  // namespace
