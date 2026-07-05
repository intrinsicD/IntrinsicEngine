---
id: GEOM-055
theme: none
depends_on: [GEOM-054]
completed: 2026-07-05
---
# GEOM-055 — Registration: optional per-iteration observer (zero cost when off)

## Status
- Retired on 2026-07-05 at `Operational`.
- Implementation commit: `811a1677` (`Add optional per-iteration observer to Geometry.Registration (GEOM-055)`).
- PR/commit for task retirement: this retirement commit.
- `Geometry.Registration` now exports `IterationTrace` and
  `IterationObserver`, and `AlignICP` accepts a trailing null-default observer
  argument. `RunIcpLoop` emits one trace per completed iteration after the
  cumulative transform/result fields are updated and before convergence
  termination.
- The observer remains separate from `RegistrationParams`, so the serializable
  config stays a pure value. The no-observer path pays one branch per
  iteration, never per point.

## Goal
- Add an optional, null-by-default `IterationObserver` seam to
  `Geometry::Registration::AlignICP` so callers can watch the intermediate
  solution each iteration (e.g. render the shape under the current estimate),
  with **zero overhead when no observer is supplied**. This is the observability
  slice ("0-obs") of the
  [geometry-pipeline-modularity](../../docs/architecture/geometry-pipeline-modularity.md)
  roadmap (§3.4).

## Non-goals
- No new algorithm variants, correspondence/transform/rejector axes, global
  stage, non-rigid path, or schedule (those are other slices).
- No change to the numerical result of registration: an observed run and an
  unobserved run must produce identical `RegistrationResult`.
- No runtime/UI wiring in this task (the geometry seam only; the editor observer
  that forwards traces to the visualization overlay is a follow-up UI slice).
- Do not put the observer in `RegistrationParams` — config stays a serializable
  value; the observer is a separate argument.

## Context
- Owner layer: `geometry` (`geometry -> core` only). `IterationTrace` is pure
  geometry/core data (glm + scalars); the runtime supplies the concrete
  visualizer-forwarding observer in a later slice.
- Builds directly on `GEOM-054`, which extracted the ICP loop into the named
  `RunIcpLoop` driver — the observer is called once per completed iteration from
  that driver, after the transform update. All observable state (cumulative
  transform, per-iteration RMSE, inlier count) is already computed by the loop,
  so emitting a trace is read-only and allocation-free.
- Zero-cost rationale: the guard is one branch per iteration (O(iterations)), not
  per point; it does not touch the per-point hot loops.
- Observer contract is read-only (must not mutate solver state) to preserve
  determinism and headless replayability (geometry-api-style deterministic
  diagnostics policy).

## Control surfaces
- Config: none (the observer is intentionally NOT a `RegistrationParams` field).
- UI: deferred — a later UI slice adds an editor observer that pushes each
  trace's `Transform` into the visualization overlay.
- Agent/CLI: unaffected; the serializable config is unchanged.

## Backends
- Backend axis: not applicable to this slice (CPU reference path only).

## Required changes
- [x] `src/geometry/Geometry.Registration.cppm`: export an `IterationTrace`
      record (`Iteration`, `Transform` = cumulative estimate after the iteration,
      `RMSE`, `InlierCount`) and an `IterationObserver =
      std::function<void(const IterationTrace&)>` alias; add `<functional>` to the
      module's global module fragment.
- [x] Add a trailing `const IterationObserver& observer = {}` parameter to
      `AlignICP` (source-compatible: all existing calls omit it).
- [x] `src/geometry/Geometry.Registration.cpp`: thread the observer into
      `RunIcpLoop` and invoke it once per completed iteration under an
      `if (observer)` guard, after the cumulative transform and result fields are
      updated and before the convergence break. Add `<functional>` to the GMF.
- [x] Regenerate `docs/api/generated/module_inventory.md` (public surface
      changed).

## Tests
- [x] `tests/unit/geometry/Test_Registration.cpp` — `ObserverDoesNotChangeResult`:
      run the same rigid-recovery case with and without a recording observer;
      assert `Transform`, `FinalRMSE`, `IterationsPerformed`, `Converged`, and
      `RMSEHistory` are identical.
- [x] `ObserverReceivesPerIterationTraces`: with a recording observer, assert the
      trace count equals `IterationsPerformed`, iteration indices are `0..n-1` in
      order, each trace `RMSE == RMSEHistory[Iteration]`, and the last trace
      `Transform` equals `result.Transform`.
- [x] Run the default CPU correctness gate for the touched scope.

## Docs
- [x] `docs/architecture/geometry-pipeline-modularity.md` §3.4 documents the seam
      (added with this roadmap; keep consistent with the shipped API).
- [x] Refresh `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] Observed and unobserved runs produce identical `RegistrationResult`.
- [x] With no observer, the ICP inner loops are unchanged (guard is one branch
      per iteration; no per-point cost, no allocation).
- [x] Trace invariants hold (count == `IterationsPerformed`; ordered indices;
      `RMSE == RMSEHistory[Iteration]`; final `Transform` == `result.Transform`).
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes
      (`IterationTrace` introduces no dependency edge beyond `geometry -> core`).
- [x] Module inventory regenerated and committed.
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` and
      `python3 tools/agents/check_task_policy.py --root . --strict` pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'Registration_ICP' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --exit-code docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Forbidden changes
- Placing the observer in `RegistrationParams` or otherwise making it part of the
  serializable config.
- Any change that alters the numerical registration result when an observer is
  present.
- Introducing per-point observer overhead or per-iteration allocation on the
  no-observer path.
- Runtime/UI/ECS edits (the editor-facing observer is a separate slice).
- Introducing any dependency that violates `geometry -> core`.

## Maturity
- Target: `Operational` — additive seam on an operational CPU path, exercised by
  the new CPU tests. The editor visualization wiring that consumes the seam is a
  follow-up UI slice tracked in the design doc §8 roadmap.
