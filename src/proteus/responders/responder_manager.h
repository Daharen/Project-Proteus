#pragma once

#include <memory>
#include <unordered_map>

#include "proteus/responders/deterministic_stub_responder.h"
#include "proteus/responders/response_validation.h"

namespace proteus::responders {

class ResponderManager {
public:
    ResponderManager();

    ResponseEnvelope ProduceValidatedEnvelope(const ResponderContext& context, ResponderKind requested_kind);

private:
    std::unordered_map<ResponderKind, std::unique_ptr<IResponder>> responders_;
    DeterministicStubResponder fallback_stub_;
};

}  // namespace proteus::responders
