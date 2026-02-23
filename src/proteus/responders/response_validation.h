#pragma once

#include <cstdint>
#include <string>

#include "proteus/responders/response_envelope.h"

namespace proteus::responders {

enum class EnvelopeValidationCode : std::uint32_t {
    Ok = 0,
    MissingQueryId,
    MissingPlayerId,
    TooManyBlocks,
    BlockTextTooLong,
    TooManyOptions,
    OptionTooLong,
    TooManyMechanicalIntents,
    MechanicalIntentInvalidToken,
    ProvenanceTooLong,
};

struct EnvelopeValidationResult {
    EnvelopeValidationCode code = EnvelopeValidationCode::Ok;
    std::uint32_t failing_index = 0;
    std::string detail;
};

EnvelopeValidationResult ValidateEnvelopeDeterministic(const ResponseEnvelope& envelope);

}  // namespace proteus::responders
