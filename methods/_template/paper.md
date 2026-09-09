# Paper Intake Template

## Citation

- **Title:**
- **Authors:**
- **Venue / Year:**
- **DOI:**
- **URL:**

## Core claim

Summarize the main claim in one or two sentences.

## Mathematical formulation

Document the objective or governing equations and required invariants.

## Inputs and outputs

- Inputs:
- Outputs:

## Degenerate/edge cases

List expected behavior for degenerate geometry or numerical edge cases.

## Implementation notes

Record translation notes from paper pseudocode to engine constraints (SoA layout, parallelization strategy, deterministic validation).

For spatial queries, consult `docs/architecture/spatial-index-consumers.md`.
Record the query/metric/membership contract, shared versus private index owner,
update frequency, complete-neighborhood policy and any missing capabilities.
Keep index acceleration separate from mathematical approximations and report
build/reuse/query costs when benchmarking an optimized backend.
