#include "gtest/gtest.h"

#include "proteus/responders/response_envelope.h"

namespace {

using proteus::responders::ComputeEnvelopeHash64;
using proteus::responders::CanonicalizeEnvelopeForHash;
using proteus::responders::ContentBlock;
using proteus::responders::ContentBlockKind;
using proteus::responders::ResponderKind;
using proteus::responders::ResponseEnvelope;

TEST(ResponseEnvelopeDeterminism, CanonicalizationAndHashAreStable) {
    ResponseEnvelope envelope;
    envelope.query_id = 42;
    envelope.stable_player_id = "player-1";
    envelope.responder_kind = ResponderKind::DeterministicStub;
    envelope.content_blocks = {
        ContentBlock{.kind = ContentBlockKind::Text, .text = "hello", .options = {}, .ordinal = 0},
        ContentBlock{.kind = ContentBlockKind::Choice, .text = "choose", .options = {"a", "b"}, .ordinal = 1},
    };
    envelope.mechanical_intents = {"intent:z", "intent:a"};
    envelope.provenance_tag = "deterministic_stub:v1";
    envelope.schema_version_tag = 1;

    const std::string canonical_first = CanonicalizeEnvelopeForHash(envelope);
    const std::string canonical_second = CanonicalizeEnvelopeForHash(envelope);
    const std::uint64_t hash_first = ComputeEnvelopeHash64(envelope);
    const std::uint64_t hash_second = ComputeEnvelopeHash64(envelope);

    EXPECT_EQ(canonical_first, canonical_second);
    EXPECT_EQ(hash_first, hash_second);
}

TEST(ResponseEnvelopeDeterminism, CanonicalizationEscapesDelimiters) {
    ResponseEnvelope envelope;
    envelope.query_id = 7;
    envelope.stable_player_id = "player\x1E\x1F";
    envelope.content_blocks = {
        ContentBlock{.kind = ContentBlockKind::Text, .text = std::string("alpha") + std::string(1, static_cast<char>(0x1E)) + "beta" + std::string(1, static_cast<char>(0x1F)) + "gamma", .options = {}, .ordinal = 0},
    };
    envelope.provenance_tag = "deterministic_stub:v1";
    envelope.schema_version_tag = 1;

    const std::string canonical = CanonicalizeEnvelopeForHash(envelope);
    EXPECT_EQ(canonical.find("\\x1E") != std::string::npos, true);
    EXPECT_EQ(canonical.find("\\x1F") != std::string::npos, true);
}

TEST(ResponseEnvelopeDeterminism, MechanicalIntentsAreSortedInCanonicalization) {
    ResponseEnvelope envelope;
    envelope.query_id = 99;
    envelope.stable_player_id = "player-sorted";
    envelope.content_blocks = {
        ContentBlock{.kind = ContentBlockKind::Text, .text = "ok", .options = {}, .ordinal = 0},
    };
    envelope.mechanical_intents = {"zeta", "alpha", "beta"};
    envelope.provenance_tag = "deterministic_stub:v1";
    envelope.schema_version_tag = 1;

    const std::string canonical = CanonicalizeEnvelopeForHash(envelope);
    const std::size_t alpha = canonical.find("alpha");
    const std::size_t beta = canonical.find("beta");
    const std::size_t zeta = canonical.find("zeta");

    EXPECT_EQ(alpha != std::string::npos, true);
    EXPECT_EQ(beta != std::string::npos, true);
    EXPECT_EQ(zeta != std::string::npos, true);
    EXPECT_GT(beta, alpha);
    EXPECT_GT(zeta, beta);
}

}  // namespace
