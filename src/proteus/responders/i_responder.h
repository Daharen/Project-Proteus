#pragma once

#include <cstdint>
#include <string>

#include "proteus/responders/response_envelope.h"

namespace proteus::responders {

struct ResponderContext {
    std::int64_t query_id = 0;
    std::string stable_player_id;
    std::string raw_query_text;
    std::uint64_t query_hash64 = 0;
    std::uint64_t stable_run_seed64 = 0;
    std::uint32_t build_version_tag = 0;
};

class IResponder {
public:
    virtual ~IResponder() = default;
    virtual ResponderKind Kind() const = 0;
    virtual ResponseEnvelope Propose(const ResponderContext& context) = 0;
};

}  // namespace proteus::responders
