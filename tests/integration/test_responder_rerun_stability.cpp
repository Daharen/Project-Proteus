#include "gtest/gtest.h"

#include <sstream>

#include "proteus/responders/responder_manager.h"

namespace {

TEST(ResponderIntegration, RerunProducesStableHashAndSummary) {
    proteus::responders::ResponderManager manager;

    const proteus::responders::ResponderContext context{
        .query_id = 101,
        .stable_player_id = "stable-player",
        .raw_query_text = "Where should we go next?",
        .query_hash64 = 12345,
        .stable_run_seed64 = 12345,
        .build_version_tag = 1,
    };

    const auto first = manager.ProduceValidatedEnvelope(context, proteus::responders::ResponderKind::DeterministicStub);
    const auto second = manager.ProduceValidatedEnvelope(context, proteus::responders::ResponderKind::DeterministicStub);

    EXPECT_EQ(first.response_hash64, second.response_hash64);

    std::ostringstream first_summary;
    first_summary << "responder_summary query_id=" << first.query_id
                  << " player=" << first.stable_player_id
                  << " hash64=" << first.response_hash64
                  << " canonical=" << proteus::responders::CanonicalizeEnvelopeForHash(first);

    std::ostringstream second_summary;
    second_summary << "responder_summary query_id=" << second.query_id
                   << " player=" << second.stable_player_id
                   << " hash64=" << second.response_hash64
                   << " canonical=" << proteus::responders::CanonicalizeEnvelopeForHash(second);

    EXPECT_EQ(first_summary.str(), second_summary.str());
}

}  // namespace
