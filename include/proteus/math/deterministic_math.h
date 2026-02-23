#pragma once

#include <vector>

namespace proteus::math {

double RoundFixed(double v, int decimals);
double SafeLog2(double p);
double ShannonEntropy(const std::vector<double>& probs, int decimals);

}  // namespace proteus::math
