# PROJECT CHECKPOINT v1

## Core Vision

Project Proteus is a deterministic, systemic RPG/MMO framework that:

- Prevents meta collapse.
- Rewards identity depth over breadth.
- Separates optimizers from executors.
- Preserves offline operation with graceful degradation.
- Uses AI only as a database expansion pipeline at the edge.

## Architectural Layers

1. **Simulation Engine (C++)**
   - Entity/event/state/tag model.
   - Reinforcement math and identity axes.
2. **Inference Engine (Deterministic 20Q style)**
   - Bayesian updates.
   - Strict 7 options + "I don't know".
   - Returns primary and backup targets.
3. **Content Graph Database**
   - Question and target nodes.
   - Similarity and weight edges.
4. **Experimentation System**
   - Contextual bandits for niche-aware pack selection.
   - Guardrails against majority collapse.
5. **AI Builder Pipeline**
   - Staging -> Curate -> Test -> Promote lifecycle.
   - Triggered by novelty and sparse graph regions.

## Open Questions to Resolve in Skeleton-to-Alpha

- Identity axis count for v1.
- Full `PlayerContext` vector schema.
- Reward function definition and metric balance.
- Novelty thresholds (offline + online).
- Reinforcement compounding vs diminishing formulation.
- Launch pack counts per domain.
- Hybrid composer grammar.
- Safe and useful telemetry policy.
