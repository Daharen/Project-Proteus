#include "proteus/playable/governor.hpp"

namespace proteus::playable {

GovernorAdjustments compute_governor_adjustments(const GovernorInputs& inputs) {
    (void)inputs;
    return GovernorAdjustments{.dampening_factor = 1.0, .reason = "noop"};
}

}  // namespace proteus::playable
