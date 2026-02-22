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

## Immediate Next Steps

1. Finalize v1 identity axes and `PlayerContext` schema.
2. Implement strict 7-option (+ IDK) question catalog format.
3. Add reinforcement-depth math with configurable compounding controls.
4. Define novelty thresholds and offline hybrid composer grammar.
5. Attach SQLite prototype adapters to content graph interfaces.

## Inference Contract (Locked in code)

- Exactly **7 total options** per question.
- `Option1`..`Option6` are substantive choices.
- `Unknown` is the 7th choice (`idk_index = 6`).
- Belief updates consume likelihoods for the selected answer only.
- Degenerate likelihood mass triggers prior reset for graceful novelty handling.
