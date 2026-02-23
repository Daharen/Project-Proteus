#include "proteus/responders/deterministic_stub_responder.h"

#include <algorithm>

#include "proteus/query/query_identity.hpp"

namespace proteus::responders {
namespace {

std::string truncate_and_normalize(std::string_view text, std::size_t max_len) {
    std::string normalized = query::NormalizeQuery(text);
    if (normalized.size() > max_len) {
        normalized.resize(max_len);
    }
    return normalized;
}

}  // namespace

ResponderKind DeterministicStubResponder::Kind() const {
    return ResponderKind::DeterministicStub;
}

ResponseEnvelope DeterministicStubResponder::Propose(const ResponderContext& context) {
    ResponseEnvelope envelope;
    envelope.query_id = context.query_id;
    envelope.stable_player_id = context.stable_player_id;
    envelope.responder_kind = Kind();
    envelope.provenance_tag = "deterministic_stub:v1";
    envelope.schema_version_tag = 1;
    envelope.mechanical_intents.clear();

    const std::string summary = truncate_and_normalize(context.raw_query_text, 192);

    envelope.content_blocks.push_back(ContentBlock{
        .kind = ContentBlockKind::Text,
        .text = "Acknowledged. Deterministic responder scaffold active.",
        .options = {},
        .ordinal = 0,
    });
    envelope.content_blocks.push_back(ContentBlock{
        .kind = ContentBlockKind::Text,
        .text = "Query summary: " + summary,
        .options = {},
        .ordinal = 1,
    });

    return envelope;
}

}  // namespace proteus::responders
