---
id: RUNTIME-136
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-07-02
---
# RUNTIME-136 — Sandbox method backend selectors

## Goal
- Let Sandbox users choose the requested CPU or Vulkan-compute backend for every currently exposed method that has a GPU variant, while reporting the actual backend and CPU fallback reason when the requested backend cannot execute.

## Non-goals
- No new GPU backend implementation; GEOM-056 and METHOD-013 own executable GPU backend work.
- No synchronous UI-side command submission, pipeline allocation, or readback polling for K-Means.
- No change to METHOD-012 CPU reference semantics, GEOM-035 mesh surface sampling, or Geometry.KMeans clustering semantics.
- No Vulkan, renderer, or RHI ownership in the Sandbox UI layer.

## Context
- Status: completed. METHOD-013 exposes Progressive Poisson backend request/fallback telemetry, and GEOM-056 exposes K-Means GPU backend request/fallback telemetry plus explicit async GPU execution surfaces.
- Owning subsystem/layer: `runtime`/UI composition. Runtime consumes geometry/method backend seams; `app` continues to import runtime only, and the UI must not call Vulkan/RHI directly.
- Progressive Poisson carries backend request through `sandbox.progressive_poisson` config and command DTOs; the Sandbox controls now expose that selector.
- K-Means exposes `Geometry::KMeans::Backend` and `Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)`; the Sandbox command/panel now lets users request CPU reference or Vulkan compute and reports the actual backend.

## Control surfaces
- Config: existing `EngineConfig.sandbox.progressive_poisson.backend` for Progressive Poisson.
- UI: Sandbox K-Means execution controls plus `PointCloud > Processing > Progressive Poisson Sampling` and `Mesh > Processing > Progressive Poisson Sampling` backend selectors.
- Agent/CLI: `ApplySandboxEditorKMeansCommand(...)`, `ApplySandboxEditorProgressivePoissonCommand(...)`, and the existing config-control facade for Progressive Poisson.

## Backends
- K-Means backend axis: CPU reference vs Vulkan compute, translated into `Geometry::KMeans::KMeansParams::Compute`.
- Progressive Poisson backend axis: CPU reference vs Vulkan compute, translated from `sandbox.progressive_poisson.backend`.
- Actual backend and fallback diagnostics come from runtime/backend result contracts. The UI displays requested and actual backends separately.

## Required changes
- [x] Add K-Means command/result backend fields with requested backend, actual backend, ids/display names, and fallback reason.
- [x] Route K-Means Sandbox execution through the runtime K-Means backend adapter when an RHI device is present, and preserve truthful CPU fallback telemetry when it is not.
- [x] Add visible K-Means and Progressive Poisson backend selectors in the Sandbox processing controls.
- [x] Preserve existing CPU reference behavior and deterministic published properties when the requested or actual backend is CPU.

## Tests
- [x] Add headless runtime contract coverage for default K-Means CPU selection.
- [x] Add headless runtime contract coverage for K-Means Vulkan-compute requests that fall back to CPU with explicit requested-vs-actual telemetry.
- [x] Preserve Progressive Poisson CPU/config/fallback coverage in the default CPU gate.
- [x] Keep any real Vulkan UI/runtime smoke opt-in with `gpu;vulkan` labels.

## Docs
- [x] Update `src/runtime/README.md`, `docs/architecture/algorithm-variant-dispatch.md`, and `methods/geometry/progressive_poisson/README.md` with backend-selector behavior, requested-vs-actual semantics, and fallback display.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after promotion and retirement.

## Acceptance criteria
- [x] Users can request CPU reference or Vulkan compute from every currently exposed Sandbox method with a GPU variant: K-Means and Progressive Poisson.
- [x] The UI/result DTOs show requested backend, actual backend, and fallback reason when applicable.
- [x] Default CPU/headless tests cover CPU and fallback semantics; any Vulkan execution remains opt-in under `gpu;vulkan`.
- [x] Layering holds: app imports runtime only, UI does not own Vulkan/RHI calls, and algorithm semantics stay in geometry/method backends.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*(KMeans|ProgressivePoisson)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed 2026-07-02 at `CPUContracted`.
Commit reference: this commit.

2026-07-02 verification evidence:
- `cmake --preset ci` passed with Clang 23 and vcpkg manifest dependencies.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests -- -j$(nproc)` passed.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*(KMeans|ProgressivePoisson)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed: 8/8.
- `cmake --build --preset ci --target IntrinsicTests -- -j$(nproc)` passed.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` completed 3448/3449 with the unrelated pre-existing `SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms` registration failure (`FinalRMSE` 0.309120624 vs `< 1.0e-3`); the edited K-Means and Progressive Poisson selector coverage passed.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` passed.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Implementing new GPU backend execution in this task.
- Bypassing the config-control facade with ad-hoc UI-only backend state.
- Leaking `Vk*`, graphics backend handles, or RHI device ownership into public UI/app APIs.

## Maturity
- Target: `CPUContracted` UI/config contract over existing backend-selection seams.
- Reached: `CPUContracted`.
- K-Means `Operational` GPU execution remains owned by the explicit GEOM-056 runtime GPU surfaces and downstream async UI scheduling work; Progressive Poisson `Operational` GPU execution remains owned by METHOD-013. This task consumes backend telemetry and does not introduce a new GPU backend.
