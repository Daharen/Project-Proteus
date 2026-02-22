#include "proteus/inference/belief_state.hpp"
#include "proteus/inference/novelty_hooks.hpp"

#include <gtest/gtest.h>

TEST(BeliefStateTest, AppliesLikelihoodsForSelectedAnswer) {
    using proteus::inference::BeliefState;
    using proteus::inference::BeliefUpdateStatus;
    using proteus::inference::TargetScore;

    BeliefState belief({
        TargetScore{"tank", 0.5},
        TargetScore{"dps", 0.3},
        TargetScore{"support", 0.2},
    });

    const auto status = belief.update({0.2, 0.5, 0.3});
    EXPECT_EQ(status, BeliefUpdateStatus::Normal);

    const auto top = belief.top_n(2);
    ASSERT_EQ(top.size(), 2);
    EXPECT_EQ(top[0].target_id, "dps");
    EXPECT_GT(top[0].posterior, top[1].posterior);
}

TEST(BeliefStateTest, RecoversFromDegenerateMassByResettingToPriors) {
    using proteus::inference::BeliefState;
    using proteus::inference::BeliefUpdateStatus;
    using proteus::inference::TargetScore;

    BeliefState belief({
        TargetScore{"tank", 0.6},
        TargetScore{"dps", 0.4},
    });

    const auto status = belief.update({0.0, 0.0});
    EXPECT_EQ(status, BeliefUpdateStatus::RecoveredFromDegenerateMass);

    const auto top = belief.top_n(2);
    ASSERT_EQ(top.size(), 2);
    EXPECT_EQ(top[0].target_id, "tank");
}

TEST(NoveltyDetectorTest, TriggersOnDegenerateRecoveries) {
    using proteus::inference::BeliefState;
    using proteus::inference::NoveltyDetector;
    using proteus::inference::TargetScore;

    BeliefState belief({
        TargetScore{"a", 0.5},
        TargetScore{"b", 0.5},
    });

    NoveltyDetector detector({
        .max_questions = 20,
        .min_top_posterior = 0.8,
        .max_normalized_entropy = 0.9,
        .max_idk_answers = 5,
        .max_degenerate_recoveries = 2,
    });

    const auto signal = detector.evaluate(belief, 1, 0, 2);
    EXPECT_EQ(signal.excessive_degenerate_recoveries, true);
    EXPECT_EQ(signal.triggered, true);
}
