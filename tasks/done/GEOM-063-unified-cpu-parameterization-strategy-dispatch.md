---
id: GEOM-063
theme: I
depends_on: [GEOM-018, GEOM-019]
maturity_target: CPUContracted
completed_on: 2026-07-15
---
# GEOM-063 — Unified CPU parameterization strategy dispatch

## Goal
- Consolidate the existing LSCM and Harmonic/Tutte disk-parameterization
  solvers behind one geometry-owned `ParameterizeMesh(mesh, strategy)` CPU
  entry point with normalized UV output, explicit status, and shared
  `ParameterizationDiagnostics`.

## Non-goals
- No backend selector or GPU-fallback telemetry. `METHOD-025`/`METHOD-026` own
  any runtime-visible selection when a real second implementation lands.
- No reserved SCP/ARAP/SLIM/BFF tokens or `NotImplemented` branches. Each
  method task adds its concrete strategy payload when its implementation lands.
- No mega parameter record, generic seed, or generic iteration fields.
- No changes to successful-solve equations or UV numerics in `ComputeLSCM` or
  `ComputeHarmonic`. Closure validation may reject inputs that violate the
  documented disk/numeric contract and must fail closed on unusable diagnostics.
- No optimization kernels (`GEOM-064`), UV-atlas migration, runtime/UI
  integration, or renderer/assets/ECS dependencies.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only. The task extends the
  existing `Geometry.Parameterization` module and implementation unit.
- `Geometry.Parameterization::ComputeLSCM` and
  `Geometry.Parameterization::ComputeHarmonic` are the two real solver APIs.
  Tutte is the harmonic solver with `HarmonicWeightType::Uniform`, not a third
  implementation.
- The existing parameter records are materially different, so the smallest
  useful family surface is a `std::variant<ParameterizationParams,
  HarmonicParams>` rather than a speculative enum plus one catch-all record.
- Right-sizing review removed the proposed global `Backend { CPU, GPU }`,
  fallback telemetry, reserved method tokens, and generic iterative fields.
  There is one real backend today; BFF and SCP are CPU-only plans, while the
  future optimized/GPU tasks apply only to selected iterative strategies.
- Reintroduction triggers are explicit: each `METHOD-021..024` task adds its
  implemented params type to the variant; `METHOD-025`/`METHOD-026`
  introduce real optimized/GPU execution and selection when a second
  implementation exists. `RUNTIME-176` owns CPU engine integration and stable
  strategy-token conversion only.
- The design follows the current-implementations-only rule in
  [`algorithm-variant-dispatch.md`](../../docs/architecture/algorithm-variant-dispatch.md)
  and realizes the family-surface step of the
  [parameterization/mapping roadmap](../../docs/architecture/parameterization-mapping-roadmap.md).

## Status
- Completed on branch `codex/arch-006-completion`; owner: Codex.
- Dependencies GEOM-018 and GEOM-019 are retired.
- Promotion/right-sizing commit: `667364f4`. Implementation commit:
  `7ebd681f`. The public dispatch/result contract, documentation refresh,
  downstream-plan correction, focused tests, and full CPU gate are complete.
  Final review also aligned LSCM with the shared connected-manifold disk
  preflight, rejected non-finite solver inputs, and made dispatch refuse UV
  payloads when shared diagnostics contain no usable evaluated faces.

## Control surfaces
- No config/UI/agent surface lands here. `RUNTIME-176` will serialize explicit
  stable strategy tokens and convert them to typed variant payloads; it must
  never persist `std::variant::index()`.

## Backends
- CPU reference only. `RUNTIME-176` owns CPU engine integration;
  `METHOD-025`/`METHOD-026` own real optimized/GPU execution and selection for
  the strategies they support.

## Required changes
- [x] Add `ParameterizationStrategy =
      std::variant<ParameterizationParams, HarmonicParams>` to the existing
      `Geometry.Parameterization` public module.
- [x] Add `ParameterizationStatus { Success, InvalidInput, SolverFailed }`.
- [x] Add `ParameterizeResult` containing status, UVs, full shared diagnostics,
      and a `Succeeded()` query.
- [x] Add `ParameterizeMesh(mesh, strategy)` dispatching to the existing direct
      solvers and normalizing their two legacy failure conventions into one
      status channel.
- [x] Add full `ParameterizationDiagnostics` to the existing LSCM result and
      populate it from the evaluator already run by `ComputeLSCM`.
- [x] Keep both direct solvers reachable and preserve valid successful-solve UV
      behavior.
- [x] Keep the implementation in the existing module implementation unit; no
      new production target or file.

## Tests
- [x] Add `tests/unit/geometry/Test.ParameterizationDispatch.cpp`
      (`unit;geometry`).
- [x] Assert bitwise UV parity for direct versus dispatched LSCM,
      Harmonic-cotangent, and Harmonic-uniform/Tutte calls.
- [x] Assert every successful dispatch contains successful shared diagnostics.
- [x] Assert invalid input and solver failure return empty UVs through the
      explicit status channel.
- [x] Assert a one-boundary-loop punctured torus, non-finite LSCM pins, and a
      solver success with `NoEvaluatedFaces` all fail closed without UVs.
- [x] Assert two identical dispatches produce bitwise-identical UVs.

## Docs
- [x] Document the unified typed CPU surface and failure mapping in the module
      interface and `docs/architecture/geometry.md`.
- [x] Refresh `docs/architecture/parameterization-mapping-roadmap.md` and
      `docs/architecture/algorithm-variant-dispatch.md` to remove speculative
      global backend/reserved-token claims and name the reintroduction triggers.
- [x] Refresh `METHOD-021..026`, `RUNTIME-176`, `UI-036`, and geometry backlog
      wording so future work adds concrete variant alternatives and owns backend
      policy where it executes.
- [x] Regenerate `docs/api/generated/module_inventory.md` and
      `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] Every currently implemented solver is selectable through one typed
      strategy variant.
- [x] Successful output is bitwise-identical to its direct solver.
- [x] Every successful result carries full normalized diagnostics.
- [x] Invalid input and solver failure fail closed with no UV payload.
- [x] No reserved strategy or backend token is exposed.
- [x] Public surface exposes only `std`/`glm`/geometry-owned records and
      preserves `geometry -> core` layering.

## Verification

The Clang-23 ASan/UBSan `ci` configure and serialized `IntrinsicTests` plus
`IntrinsicBenchmarkSmoke` build completed. The single geometry binary's exact
task-family selection passed 31/31, the dispatch-only selection passed 6/6,
and the repository CPU-supported gate passed 9250/9250 in 977.67 seconds.
Module inventory generation remained at 390 modules.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
build/ci/bin/IntrinsicGeometryTests --gtest_filter='*Parameterization*:*Harmonic*:*Lscm*'
build/ci/bin/IntrinsicGeometryTests --gtest_filter='ParameterizationDispatch.*'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
```

## Forbidden changes
- No new parameterization algorithm or successful-solve numerical change to the
  wrapped direct solvers.
- No RHI/Eigen types on the public geometry surface; no
  runtime/graphics/assets/ECS/platform/app dependencies.
- No reserved method/backend values, arbitrary projection fallback, or
  serialization of variant alternative indices.
- No migration of `Geometry.UvAtlas` onto this surface.

## Maturity
- Reached: `CPUContracted`. Operational engine integration is owned by
  `RUNTIME-176`; real optimized/GPU selection is owned by
  `METHOD-025`/`METHOD-026` when those implementations land.

Completed: 2026-07-15. Promotion/right-sizing commit: `667364f4`;
implementation commit: `7ebd681f`; final fail-closed review corrections and
retirement metadata are recorded by this GEOM-063 change set.
