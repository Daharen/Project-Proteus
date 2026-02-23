#include "proteus/math/deterministic_math.h"

#include <algorithm>
#include <cmath>

namespace proteus::math {

double RoundFixed(double v, int decimals) {
    const int clamped_decimals = std::max(0, std::min(decimals, 12));
    double scale = 1.0;
    for (int i = 0; i < clamped_decimals; ++i) {
        scale *= 10.0;
    }
    return std::llround(v * scale) / scale;
}

double SafeLog2(double p) {
    if (p <= 0.0) {
        return 0.0;
    }
    return std::log2(p);
}

double ShannonEntropy(const std::vector<double>& probs, int decimals) {
    double entropy = 0.0;
    for (const double p : probs) {
        if (p <= 0.0) {
            continue;
        }
        entropy -= p * SafeLog2(p);
    }
    return RoundFixed(entropy, decimals);
}

}  // namespace proteus::math
