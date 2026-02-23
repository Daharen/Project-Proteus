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
    EXPECT_EQ(report.issues.empty(), true);
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
