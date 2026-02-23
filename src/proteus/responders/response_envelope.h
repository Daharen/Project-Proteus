#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::responders {

enum class ResponderKind : std::uint32_t {
    DeterministicStub = 0,
    ReservedRetrievalAssist = 1,
    ReservedPolicyReasoner = 2,
};

enum class ContentBlockKind : std::uint32_t {
    Text = 0,
    Choice = 1,
    Hint = 2,
};

struct ContentBlock {
    ContentBlockKind kind = ContentBlockKind::Text;
    std::string text;
    std::vector<std::string> options;
    std::uint32_t ordinal = 0;
};

struct ResponseEnvelope {
    std::uint64_t response_hash64 = 0;
    std::int64_t query_id = 0;
    std::string stable_player_id;
    ResponderKind responder_kind = ResponderKind::DeterministicStub;
    std::vector<ContentBlock> content_blocks;
    std::vector<std::string> mechanical_intents;
    std::string provenance_tag;
    std::uint32_t schema_version_tag = 1;
    std::string debug_note;
};

std::string CanonicalizeEnvelopeForHash(const ResponseEnvelope& envelope);
std::uint64_t ComputeEnvelopeHash64(const ResponseEnvelope& envelope);

}  // namespace proteus::responders
