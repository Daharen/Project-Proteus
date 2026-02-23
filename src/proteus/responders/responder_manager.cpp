#include "proteus/responders/responder_manager.h"

#include <algorithm>

namespace proteus::responders {
namespace {

std::string validation_code_string(const EnvelopeValidationCode code) {
    return std::to_string(static_cast<std::uint32_t>(code));
}

void normalize_envelope_for_hashing(ResponseEnvelope& envelope) {
    std::sort(envelope.mechanical_intents.begin(), envelope.mechanical_intents.end());
    std::stable_sort(envelope.content_blocks.begin(), envelope.content_blocks.end(), [](const ContentBlock& lhs, const ContentBlock& rhs) {
        return lhs.ordinal < rhs.ordinal;
    });
}

}  // namespace

ResponderManager::ResponderManager() {
    responders_.emplace(ResponderKind::DeterministicStub, std::make_unique<DeterministicStubResponder>());
}

ResponseEnvelope ResponderManager::ProduceValidatedEnvelope(const ResponderContext& context, const ResponderKind requested_kind) {
    ResponseEnvelope envelope;
    auto it = responders_.find(requested_kind);
    if (it != responders_.end()) {
        envelope = it->second->Propose(context);
    } else {
        envelope = fallback_stub_.Propose(context);
        envelope.debug_note = "fallback:missing_requested_responder";
    }

    normalize_envelope_for_hashing(envelope);
    envelope.response_hash64 = ComputeEnvelopeHash64(envelope);
    EnvelopeValidationResult validation = ValidateEnvelopeDeterministic(envelope);
    if (validation.code != EnvelopeValidationCode::Ok) {
        envelope = fallback_stub_.Propose(context);
        if (!envelope.debug_note.empty()) {
            envelope.debug_note += ";";
        }
        envelope.debug_note += "fallback:validation_code=" + validation_code_string(validation.code);
        normalize_envelope_for_hashing(envelope);
        envelope.response_hash64 = ComputeEnvelopeHash64(envelope);
    }

    return envelope;
}

}  // namespace proteus::responders
