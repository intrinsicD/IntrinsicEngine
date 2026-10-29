---
id: RUNTIME-351
theme: F
depends_on: [ARCH-021, RUNTIME-349]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, CI and the gate measurement
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: kernel density and point spacing get their agent row, section registration and panel generated from their definitions, their entry point and section forwards become generic, and the phase 1 gate of ADR 0031 is measured; behaviour unchanged; no catalog contract covers the editor's generated exposure
---
# RUNTIME-351 — Generated exposure and the phase 1 gate (ADR 0031 phase 1, slice 5)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (3: start phase 0 and then the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (1: tests that pin the per-operation structure may be
    rewritten, keeping every behavioural assertion)

## Goal
- ADR 0031 Decision 7 (exposure: `OperationRow<Def>`, `RegisterOperationSection<Def>`, a default
  `DrawOperation<Def>` panel) and the phase 1 gate: each pilot operation fits "adding an operation"
  (at most 9 files, 3 its own) with at most 150 operation-specific lines; build time and memory
  within the ADR's limits; existing behaviour unchanged.
- **Starting point (measured 2026-10-29, after `RUNTIME-349`):** own files: kernel density 382 lines
  (interface 101, implementation 281), point spacing 367 (103, 264); plus their panels (about 130
  each in `Sandbox.MeshProcessingPanels.cpp`), agent rows and registrations in shared files.
- **Removal budget for kernel density** (to be refined by the plan):

  | Part | Lines now | Can it go? |
  |---|---|---|
  | entry point declarations and forwards (9 functions) | about 75 | to generic templates on the operation tag; a few lines remain |
  | section functions and their declarations (serialize, validate, get, set, specs, registration, ToString) | about 50 | to generic functions of the section declaration; a few lines remain |
  | `IsDerivable` witnesses | about 22 | to a test |
  | config wrappers | about 18 | to the runtime |
  | panel (shared file) | about 130 | to `DrawOperation<Def>` plus a one-line listing |
  | record, enum, result struct | about 45 | stays |
  | rows and section declaration | about 63 | stays |
  | definition (names, compute, GPU parameters, statistics fold) | about 55 | stays |
  | module boilerplate and includes | about 50 | partly |

  **Early warning:** what stays adds up to about 210-230 lines per operation before any shared-file
  lines, above the gate's 150. Unless the plan finds deeper cuts (a generic result header instead
  of each result's common fields, fewer includes), the gate will be missed; per ADR 0031 that is an
  implementation failure to fix within the phase, and the operator decides how to proceed.

## Plan
- To be written and reviewed before implementation (generated exposure first, then the generic
  forwards, then the measurement).

## Acceptance criteria
- [ ] Generated agent row, section registration and default panel for both operations.
- [ ] Generic entry point and section forwards.
- [ ] Phase 1 gate measured and reported (files, lines, build time, memory), met or not.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-29: Created from `RUNTIME-349`'s code review (O1), with the measured starting point and the
  removal budget.
