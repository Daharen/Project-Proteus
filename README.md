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
