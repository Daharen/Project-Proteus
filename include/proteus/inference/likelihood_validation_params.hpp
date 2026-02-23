#pragma once

namespace proteus::inference {

struct LikelihoodValidationParams {
    double epsilon = 1e-9;
    double near_epsilon_ratio = 2.0;
    double substantive_min_ratio = 2.0;
    double answer_cosine_dup = 0.995;
    double idk_uniform_ratio = 1.25;
    double idk_kl_max_bits = 0.02;
    double ig_min_bits = 0.02;
    double normalization_tol = 1e-6;
};

}  // namespace proteus::inference
