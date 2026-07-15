---
name: intrinsicengine-results-audit
description: Adversarial referee pass ("scientist pass") over IntrinsicEngine's method, benchmark, backend-parity, capability-maturity, and quantitative claims. Use before a result or performance/capability claim enters README, method papers/reports, architecture docs, tasks, or ARA claims; after a benchmark or method evidence session; before closing work as Operational or ParityProven; when reviewing a results-bearing PR; or whenever reported numbers or backend behavior lack an independent check. Pair with intrinsicengine-method, intrinsicengine-benchmark, and intrinsicengine-review as routed by scope. Do not use for generating new research directions (that is intrinsicengine-research-ideation).
license: MIT
metadata:
  version: "1.0.0"
---

# Results Audit ("scientist pass")

> **Provenance.** Distilled 2026-07-15 from IntrinsicEngine's own review
> discipline: the maturity taxonomy that prevents a CPU/null contract from being
> called Operational; the UI-030 investigation that narrowed CPU-side frame
> diagnostics after the Vulkan operational gate failed; the GRAPHICS-114 report
> that separated observable copy counters from an unmeasured wall-clock speedup;
> and the recurring documented-but-not-tested audit finding. It incorporates the
> sibling-repository scientist-pass pattern for claims that survive green CI but
> fail independent evidence review.

## Stance

Act as a referee, not the author. Assume the producing session confused a seam
with a capability, compilation with execution, a smoke with a benchmark, or a
measurement with a comparative claim. Hunt through the method contract, selected
backend, raw result, accounting, tests, and wording. The deliverable is corrected
evidence and narrowed state, never reassurance.

This skill audits results. Load `intrinsicengine-method` for method contracts and
backend parity, `intrinsicengine-benchmark` for manifests/results/baselines, and
`intrinsicengine-review` before reporting completion. `AGENTS.md` remains binding.

## When to run

- Before quantitative, performance, parity, or capability text enters a README,
  architecture doc, `methods/**/paper.md`, method report, task, or ARA claim.
- After a method/benchmark run, GPU/Vulkan smoke, parity session, or evidence bundle.
- Before closing a task as `Operational` or `ParityProven`, changing a default,
  retiring a legacy path, or reviewing a results-bearing PR.
- On request: "audit", "referee pass", "scientist pass", "verify the claims".
- Periodically over previously closed tasks and reports, not only new claims.

## Procedure

### 1. Inventory the claims

Sweep changed `README.md` files, architecture/method/benchmark docs,
`methods/**/{method.yaml,paper.md,README.md,reports/}`, benchmark manifests and
results, task Status/Acceptance/Verification blocks, `ara/logic/claims.md`, and
`ara/evidence/`. Build:

| # | Claim (one sentence) | Maturity/kind | Evidence path + backend | Executed now? |
|---|---|---|---|---|

Kind is one of: correctness, performance, backend parity, capability/maturity,
or asserted. Name `Scaffolded`, `CPUContracted`, `Operational`, `ParityProven`,
or `Retired` when maturity is involved. An asserted row, missing backend, or
missing maturity boundary is already a finding.

### 2. Check the property independently

Recompute metrics and acceptance predicates from machine-readable result JSON,
not prose. For methods, exercise an analytic/simple case against the deterministic
CPU reference oracle and trace the claim through `method.yaml`, `paper.md`, code,
and tests. For task capability, verify the reached maturity criteria directly.
Knowledge-graph links are navigation only; the method contract, source, and
executed checks are evidence.

### 3. Bind every claim to executed evidence

Correctness claims bind to named CTest cases and current output. Method parity
binds to the reference result plus declared backend identity and parity delta.
Benchmark/performance claims bind to a stable `benchmark_id`, validated manifest,
validated result JSON, comparable baseline, and quality/error metric. Capability
claims bind to the backend-labeled or integration-labeled run that actually
exercised the path. Record commands run in this session and whether tests selected,
passed, skipped, or fell back.

### 4. Audit configuration and backend parity

Verify preset and complete Clang 20+ toolchain, current build tree, commit/diff,
dataset/version, manifest parameters, warmup/repetitions, baseline, backend, and
runtime config. Distinguish requested Vulkan from an operational device; confirm
both promotion flags, `IDevice::IsOperational()`, diagnostics, and readback path.
Null/CPU fallback, a recorded dispatch, or a skipped `gpu;vulkan` test is not an
executed GPU result.

### 5. Recompute units and accounting

Check time units, byte counts, error norms, tolerance direction, sample/repetition
counts, cold versus warm populations, and aggregate versus phase timings. Recompute
speedups from raw comparable measurements and include uncertainty/variance where
applicable. Counters can prove work avoided but not elapsed-time improvement.
Require equal scope and a quality delta alongside runtime; a faster wrong backend
is a regression.

### 6. Audit controls and robustness

Require analytic, degenerate, invalid-input, and deterministic controls appropriate
to the method. Optimized CPU and GPU compare to the CPU reference, never only to
each other. Confirm failure/fallback diagnostics and backend identity are observable.
Separate PR-fast smoke from performance evidence, CPU/null contract from runtime
integration, and a single fixture from canonical-dataset parity. Never tune a
tolerance until an implementation passes it.

### 7. State environment limits honestly

List what this machine configured, compiled, and executed. The default CPU CTest
gate proves CPU-supported contracts only. A green CI build does not prove Vulkan
execution; `ci-vulkan` configuration does not prove an operational device; an
environmental skip is not a pass. Name unavailable hardware, large/nightly datasets,
or unrun follow-ups and keep their claims pending.

### 8. Dispose of every claim in the same change

For each row: **confirm** (evidence and scope filled), **narrow** (rewrite to the
demonstrated maturity/backend/dataset), or **retire** (remove the claim and preserve
why). Update the method README/paper/report, benchmark result/baseline docs, task
Verification and maturity state, architecture/ARA evidence, and follow-up task as
applicable in the same commit. A scaffold may close only with its explicit non-goal
or next-maturity task; an unexecuted backend remains pending.

### 9. Report

End with the claim table, raw recalculations, commands and selected test names,
corrections made, skipped/fallback/unverified items, and the exact evidence needed
to promote each pending maturity or performance row.

## Anti-patterns (hard nos)

- Calling configure/compile, command recording, Null fallback, or a skipped smoke
  proof that a GPU/Vulkan path executed.
- Calling `CPUContracted` work Operational or an unmeasured counter delta a speedup.
- Reporting runtime without a comparable baseline and quality/error delta.
- Comparing optimized and GPU backends without the CPU reference oracle.
- Using a stale/non-preset build tree or stale `LastTestsFailed.log` as evidence.
- Tuning tolerances, deleting failing checks, or weakening labels/gates to pass.
- Reporting remembered PR/task numbers without reopening their raw result.

## Repository anchors

`AGENTS.md` §§6–8, 12 — method, testing, benchmarking, and review contracts ·
`docs/agent/task-maturity.md` — capability vocabulary ·
`tools/agents/skills/intrinsicengine-{method,benchmark,review}/` — governing
workflows · `methods/**/method.yaml` and reports — claim/backend contracts ·
`benchmarks/**/manifests/`, `benchmarks/baselines/`, `benchmarks/reports/` —
measurement lineage · `tools/benchmark/validate_benchmark_{manifests,results}.py`
— schema gates · `ara/logic/claims.md`, `ara/evidence/` — research evidence.
