#include "proteus/responders/response_validation.h"

namespace proteus::responders {
namespace {

bool is_valid_intent_char(const char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == ':';
}

bool is_valid_intent_token(const std::string& token) {
    if (token.empty() || token.size() > 48) {
        return false;
    }
    for (const char c : token) {
        if (!is_valid_intent_char(c)) {
            return false;
        }
    }
    return true;
}

}  // namespace

EnvelopeValidationResult ValidateEnvelopeDeterministic(const ResponseEnvelope& envelope) {
    if (envelope.query_id <= 0) {
        return {EnvelopeValidationCode::MissingQueryId, 0, "query_id must be > 0"};
    }

    if (envelope.stable_player_id.empty() || envelope.stable_player_id.size() > 96) {
        return {EnvelopeValidationCode::MissingPlayerId, 0, "stable_player_id length must be 1..96"};
    }

    if (envelope.content_blocks.size() > 8) {
        return {EnvelopeValidationCode::TooManyBlocks, static_cast<std::uint32_t>(envelope.content_blocks.size()), "content_blocks must be <= 8"};
    }

    for (std::size_t i = 0; i < envelope.content_blocks.size(); ++i) {
        const auto& block = envelope.content_blocks[i];
        if (block.ordinal != i) {
            return {EnvelopeValidationCode::TooManyBlocks, static_cast<std::uint32_t>(i), "content_blocks ordinals must be strictly increasing from 0"};
        }
        if (block.text.size() > 512) {
            return {EnvelopeValidationCode::BlockTextTooLong, static_cast<std::uint32_t>(i), "content block text must be <= 512"};
        }
        if (block.options.size() > 8) {
            return {EnvelopeValidationCode::TooManyOptions, static_cast<std::uint32_t>(i), "content block options must be <= 8"};
        }
        for (std::size_t j = 0; j < block.options.size(); ++j) {
            if (block.options[j].size() > 96) {
                return {EnvelopeValidationCode::OptionTooLong, static_cast<std::uint32_t>(j), "option text must be <= 96"};
            }
        }
    }

    if (envelope.mechanical_intents.size() > 16) {
        return {EnvelopeValidationCode::TooManyMechanicalIntents, static_cast<std::uint32_t>(envelope.mechanical_intents.size()), "mechanical_intents must be <= 16"};
    }

    for (std::size_t i = 0; i < envelope.mechanical_intents.size(); ++i) {
        if (!is_valid_intent_token(envelope.mechanical_intents[i])) {
            return {EnvelopeValidationCode::MechanicalIntentInvalidToken, static_cast<std::uint32_t>(i), "invalid mechanical intent token"};
        }
    }

    if (envelope.provenance_tag.size() > 64) {
        return {EnvelopeValidationCode::ProvenanceTooLong, 0, "provenance_tag must be <= 64"};
    }

    return {};
}

}  // namespace proteus::responders
