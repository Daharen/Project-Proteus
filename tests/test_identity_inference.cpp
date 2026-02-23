#include "proteus/bandits/contextual_bandit.hpp"
#include "proteus/content/in_memory_graph.hpp"
#include "proteus/inference/identity.hpp"

#include <gtest/gtest.h>

#include <unordered_map>

TEST(IdentityInferenceTest, SeedsIdentityDomainAndValidatesLikelihoods) {
    proteus::content::InMemoryContentGraph graph;
    graph.seed_identity_v1_domain();

    const auto targets = graph.get_candidate_targets("identity");
    EXPECT_EQ(targets.size(), 12);

    const auto questions = graph.get_domain_questions("identity");
    EXPECT_EQ(questions.size(), 15);

    const auto report = graph.validate_likelihood_tables("identity");
    EXPECT_EQ(report.ok, true);
    EXPECT_EQ(report.hard_violations.empty(), true);
}

TEST(IdentityInferenceTest, BuildsDerivedAxisVectorFromPosterior) {
    proteus::content::InMemoryContentGraph graph;
    graph.seed_identity_v1_domain();

    std::unordered_map<std::string, proteus::inference::IdentityArchetype> archetypes;
    for (const auto& archetype : graph.get_identity_archetypes()) {
        archetypes.emplace(archetype.id, archetype);
    }

    const std::vector<proteus::inference::TargetScore> posterior = {
        {"identity:planner", 0.6},
        {"identity:wanderer", 0.3},
        {"identity:guardian", 0.1},
    };

    const auto result = proteus::inference::build_identity_result(
        posterior,
        archetypes,
        proteus::inference::NoveltySignal{.triggered = false},
        2
    );

    EXPECT_EQ(result.archetype_posterior.size(), 2);
    EXPECT_EQ(result.archetype_posterior.front().target_id, "identity:planner");

    const auto agency_idx = static_cast<std::size_t>(proteus::inference::IdentityAxis::AgencyStyle);
    const auto social_idx = static_cast<std::size_t>(proteus::inference::IdentityAxis::SocialPosture);
    EXPECT_GT(result.derived_axes[agency_idx], 0.4F);
    EXPECT_GT(0.0F, result.derived_axes[social_idx]);
    EXPECT_GT(result.confidence.top_posterior_strength, 0.59);
    EXPECT_GT(1.0001, result.confidence.normalized_entropy);
}


TEST(PlayerContextTest, CapturesIdentitySignalsInCompactSchema) {
    const proteus::inference::AxisVector axes = {0.5F, -0.1F, 0.3F, 0.7F, -0.2F, -0.8F, 0.4F, 0.9F};

    const proteus::bandits::PlayerContext context{
        .identity_axes = axes,
        .identity_confidence = 0.77F,
        .identity_entropy = 0.23F,
        .questions_answered = 9,
        .idk_rate = 2.0F / 9.0F,
        .session_id = 0,
        .niche_id = 0,
    };

    EXPECT_EQ(context.identity_axes.size(), proteus::inference::kIdentityAxisCount);
    EXPECT_GT(context.identity_confidence, 0.7F);
    EXPECT_GT(0.3F, context.identity_entropy);
    EXPECT_EQ(context.questions_answered, 9U);
    EXPECT_GT(context.idk_rate, 0.2F);
    EXPECT_EQ(context.session_id, 0U);
    EXPECT_EQ(context.niche_id, 0U);
}


TEST(IdentityInferenceTest, LikelihoodTablesArePerTargetNormalized) {
    proteus::content::InMemoryContentGraph graph;
    graph.seed_identity_v1_domain();

    const auto targets = graph.get_candidate_targets("identity");
    const auto questions = graph.get_domain_questions("identity");

    for (const auto& qid : questions) {
        for (std::size_t t = 0; t < targets.size(); ++t) {
            double sum = 0.0;
            for (std::size_t a = 0; a < proteus::inference::kTotalAnswerOptions; ++a) {
                const auto row = graph.get_likelihoods(qid, static_cast<proteus::inference::AnswerOption>(a), targets);
                sum += row[t];
            }
            EXPECT_GT(1.001, sum);
            EXPECT_GT(sum, 0.999);
        }
    }
}
