# Project Proteus

Project Proteus is a systemic RPG/MMO framework focused on identity depth, deterministic inference, and edge-only AI expansion.

## Skeleton Construction Phase

This repository now contains a baseline C++ architecture scaffold aligned with the v1 checkpoint:

- **Layer 1: Simulation Engine** (`include/proteus/simulation`)
- **Layer 2: Deterministic Inference Engine** (`include/proteus/inference`)
- **Layer 3: Content Graph Contracts** (`include/proteus/content`)
- **Layer 4: Experimentation Contracts** (`include/proteus/bandits`)
- **Layer 5: AI Builder Pipeline Contracts** (`include/proteus/ai`)

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```


## PlayerContext v1

`PlayerContext` now carries the stable identity snapshot used downstream by bandits and content selection:

- `identity_axes` (8 floats in `[-1, 1]`).
- `identity_confidence` (top posterior).
- `identity_entropy` (normalized entropy in `[0, 1]`).
- `questions_answered` and `idk_rate` for inference depth/ambiguity.
- `session_id` (optional, currently default `0`) and `niche_id` placeholder (default `0`).

## Identity Axes v1 (seeded in-memory)

The in-memory content graph now seeds a canonical `identity` domain with:

- 8 axes (`AgencyStyle`, `ChallengeAppetite`, `ExplorationDrive`, `SystemAppetite`, `RiskPosture`, `SocialPosture`, `NarrativePreference`, `MoralOrientation`).
- 12 identity archetypes (`identity:planner`, `identity:wanderer`, etc.) represented as 8-float axis vectors.
- 15 behavioral questions with 6 substantive options + `Unknown`.
- Per-answer likelihood tables over all archetypes plus validation checks for near-zero/near-uniform/duplicate/non-differentiating entries.


## Likelihood Semantics + Authoring Rules v1

Canonical semantics for `ContentGraph::get_likelihoods(question_id, answer, targets)`:

- Returns one value per target `t`: `L[t] = P(answer=a | target=t, question=Q)`.
- Questions are fixed to 7 options, with IDK at index `6`.
- Likelihood tables are normalized **per-target across answers** for each question:
  - `sum_a P(answer=a | target=t, question=Q) = 1`.
- Values must be finite and strictly positive (epsilon-clamped by authored data + runtime safeguards).

Validator checks (hard violations vs warnings):

- **Hard**: wrong vector length, NaN/Inf, non-positive likelihood, per-target normalization failure.
- **Warnings**: near-epsilon values, weak substantive differentiation ratio, near-duplicate answers, high-impact IDK (KL vs neutral prior), low expected information gain.
- Validator also reports per-question expected information gain (bits) under a neutral prior for debug visibility.

Authoring recipe (v1):

1. For each substantive answer (0..5), pick 2–4 archetypes that are strongest matches.
2. Assign rough weights (example): strong `0.18`, medium `0.10`, weak `0.06`, others `0.03`.
3. Add a small epsilon floor to every cell.
4. Keep IDK near-uniform and low-impact.
5. Normalize per-target across all 7 answers.

## Immediate Next Steps

1. Tune validator thresholds with live trace data and authoring feedback.
2. Harden novelty thresholds and fallback policy using live play traces.
3. Expand reinforcement math with volatility/switching penalties and coherence bonuses.
4. Define offline hybrid composer grammar and discriminator flow.
5. Attach SQLite prototype adapters to content graph interfaces.

## Inference Contract (Locked in code)

- Exactly **7 total options** per question.
- `Option1`..`Option6` are substantive choices.
- `Unknown` is the 7th choice (`idk_index = 6`).
- Belief updates consume likelihoods for the selected answer only.
- Degenerate likelihood mass triggers prior reset for graceful novelty handling.


## Demo

Run the in-memory end-to-end 20Q loop:

```bash
./build/proteus_20q_demo
```
