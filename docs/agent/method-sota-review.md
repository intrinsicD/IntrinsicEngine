# Method SOTA Review — Recurring Process

## Purpose

Geometry-processing and rendering papers move forward on a quarterly cadence. New variants of methods that are already in the engine (signed heat, dual contouring, mesh booleans, parameterization, …) appear regularly. Without a deliberate review, the engine's defaults drift away from the current state of the art and accumulate "best at the time of integration" backends that nobody owns the task of replacing.

This document defines the recurring **Method SOTA Review** process used to keep the engine's default backends current and to retire superseded variants.

## Cadence

- One full pass every **calendar quarter** (4× per year).
- A targeted pass may be triggered any time a new SIGGRAPH / SIGGRAPH Asia / EUROGRAPHICS / SGP proceedings is published.
- Out-of-cycle review is also valid when an existing method-task in `tasks/backlog/` or `tasks/active/` is about to start implementation — re-confirm the variant choice before writing code.

## Scope

Each review pass covers:

1. Every paper-method package under `methods/geometry/`, `methods/rendering/`, `methods/physics/`.
2. Every backlog task whose ID is `METHOD-*` or which contains a "Variants and default selection" section.
3. Any module in `src/geometry/` that explicitly cites a paper in its top-of-file documentation.

## Inputs

- Latest SIGGRAPH / SGP / EUROGRAPHICS proceedings tables of contents.
- Latest arxiv `cs.GR` listings for the quarter.
- Open issues / discussions tagged with `method-sota`.
- Engine-side benchmark reports under `methods/*/<method>/reports/`.

## Output

Each pass produces:

1. A dated review note at `docs/reviews/<YYYY-MM-DD>-method-sota-review.md`. Use the format from `docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md` as the template.
2. For every method whose default variant is being challenged, **one of**:
   - A confirmation entry in the review note that the current default remains SOTA (cite the new candidates considered and why they were rejected).
   - A new follow-up implementation task under the appropriate `tasks/backlog/` directory that proposes the replacement variant with full justification, dependencies, and migration plan.
3. An updated standing task `METHOD-008` with the cycle's date appended to its `## Review log` section.

## Decision rubric for "replace the default?"

A variant should replace the current default only if **at least two** of the following are true and **none** of the disqualifiers apply:

**Promote if (≥ 2):**
- Demonstrates strict improvement on the existing benchmark manifest's quality metrics at equal or better wall-time.
- Reduces dependency surface (fewer external libraries, fewer prerequisite tasks).
- Simplifies the API or improves diagnostic granularity.
- Resolves a documented degeneracy or robustness failure of the current default.
- Is now implemented by a stable reference codebase that can serve as a parity oracle.

**Disqualifiers (any):**
- Requires a runtime ML stack (PyTorch, ONNX, JAX) before a non-neural reference exists.
- Has no published reference implementation and the paper omits enough algorithmic detail that re-implementation cost exceeds two engineer-weeks.
- Is licensed incompatibly (AGPL / commercial-only) for the engine's distribution model.
- Would break determinism guarantees the engine has committed to.

## Migration policy when a default is replaced

1. The new variant is added as an **opt-in** backend behind a `Params::Variant` enum value. Old default behaviour continues to run when the enum is left at its previous value.
2. A parity period of at least one cycle (one quarter) must elapse with both variants reachable, during which:
   - Quality metrics are recorded for both on the canonical benchmark manifest.
   - At least one downstream consumer test exercises the new variant.
3. Only after parity is demonstrated does the default value of `Params::Variant` flip in a separate, narrowly-scoped task. Existing callers are audited and updated explicitly.
4. The old variant remains reachable for one further cycle as `Variant::Legacy_X` before any retirement is even proposed.

## Anti-goals

- Never delete or rewrite a variant during the review pass itself; the review only proposes follow-up tasks.
- Never flip a default in the same PR that introduces the new variant.
- Never add a learned / neural variant as the default unless the engine first ships a non-neural reference for the same problem.
- Never make a "this paper is shinier" argument without a benchmark-manifest comparison.

## Where review artefacts live

- Process doc (this file): `docs/agent/method-sota-review.md`.
- Standing task: `tasks/backlog/methods/METHOD-008-recurring-method-sota-review.md`.
- Per-cycle review notes: `docs/reviews/<YYYY-MM-DD>-method-sota-review.md`.
- Per-method follow-up tasks: under the relevant `tasks/backlog/<domain>/` directory using the existing ID conventions.
