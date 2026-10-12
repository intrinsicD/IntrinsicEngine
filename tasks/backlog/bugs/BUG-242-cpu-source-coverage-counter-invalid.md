---
id: BUG-242
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: existing main CI failure observed during METHOD-083; no CI repair in this slice.
contract_schema: 1
contracts: []
contract_review: Existing coverage collection failure; no method or architecture change.
---
# BUG-242 — CPU coverage rejects an invalid LLVM execution counter

## Goal
Restore source coverage collection on main without weakening its validity checks.

## Evidence
[Main run 38200249201](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38200249201),
commit `bbdf1af795ba064099689d6d591b1ee310f46b74`, job `114651839727`,
2026-10-12 04:20:04 UTC: `CPU source coverage: error: src/graphics/renderer/Graphics.Renderer.cpp:
segment: saturated or negative LLVM execution counter; coverage builds require atomic profile updates`.
Collection exits 1; timing aggregation also reports failure. Retrieved via the job logs API.
No PR exists for the current local METHOD-083 branch.

## Acceptance criteria
- [ ] Diagnose profile collection/counter invalidity from this job's raw artifacts.
- [ ] Re-run collection successfully without suppressing invalid counters.

## Verification
Reproduce the failing workflow command with its saved profile artifacts, then run the
owning coverage-tool tests and strict task policy checks.
