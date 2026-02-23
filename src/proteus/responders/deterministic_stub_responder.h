#pragma once

#include "proteus/responders/i_responder.h"

namespace proteus::responders {

class DeterministicStubResponder : public IResponder {
public:
    ResponderKind Kind() const override;
    ResponseEnvelope Propose(const ResponderContext& context) override;
};

}  // namespace proteus::responders
