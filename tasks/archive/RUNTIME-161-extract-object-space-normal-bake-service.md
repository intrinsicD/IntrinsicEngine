---
id: RUNTIME-161
theme: F
depends_on: [RUNTIME-160]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-161 — Extract object-space normal bake service out of Engine

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.ObjectSpaceNormalBakeService` owns object-space normal
  bake GPU-queue lifetime, dependency setup, ready-frame callback construction,
  JobService participant registration, diagnostics access, pending-count access,
  and dependency clearing.
- `Runtime.Engine` keeps lifecycle ordering and queue handoff to import,
  scene-document, direct-mesh, and selected-mesh callers while delegating the
  bake queue policy through the service.
- Verification passed: focused object-space-normal / JobService /
  Engine-layering CPU coverage, `RuntimeEngineLayering` integration coverage,
  strict task/docs/layering/test-layout checks, `IntrinsicTests`, and the
  default CPU-supported CTest gate at 3646/3646.
- Warning-mode root/task-state findings remain pre-existing and unchanged:
  retired `ARCH-007`..`ARCH-013` index links, root `ara/`, and root
  `imgui.ini`.
- PR/commit: pending.

## Goal
- Move object-space normal bake GPU-queue ownership, dependency setup, JobService participant registration, and test diagnostics access out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving the existing request queue handoff to asset import, scene document cleanup, direct-mesh post-import processors, and selected-mesh texture bake callers.

## Non-goals
- Completing `RUNTIME-129`'s remaining production Vulkan geometry-buffer / pipeline / dilation plan-provider wiring.
- Claiming `RUNTIME-129` `Operational` status or running a new `gpu;vulkan` object-space normal bake smoke.
- Changing `RuntimeObjectSpaceNormalBakeQueue`, `RuntimeObjectSpaceNormalBakeGpuQueue`, stale-key semantics, generated `AssetId` selection, material binding, or no-CPU-fallback behavior.
- Changing asset import, direct-mesh post-import, selected-mesh texture bake, scene reset, renderer command recording, or JobService participant ordering behavior.

## Context
- Owner: `runtime`; this is composition glue around the existing object-space normal bake queue substrate.
- `RUNTIME-129` remains open for production Vulkan plan-provider wiring. Its CPU-contracted queue substrate already exists and is composed by Engine today.
- `Runtime.Engine.cppm` currently imports both `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` and `Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue`, stores `RuntimeObjectSpaceNormalBakeGpuQueue`, and exposes queue diagnostics through Engine test accessors.
- `Runtime.Engine.cpp` currently sets GPU queue dependencies, captures `RHI::IDevice::GetGlobalFrameNumber()` in the ready-frame callback, registers the queue participant with `JobService`, passes the raw queue into asset import/document dependencies, and clears queue dependencies during shutdown.
- This follows the `RUNTIME-146` through `RUNTIME-160` decomposition pattern: `Engine` remains the concrete composition root, while subsystem-local ownership and policy move behind runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.ObjectSpaceNormalBakeService` owning the `RuntimeObjectSpaceNormalBakeGpuQueue`.
- [x] Move dependency setup, ready-frame callback construction, JobService GPU-queue participant registration, queue diagnostics access, pending-count access, and dependency clearing behind the service.
- [x] Update `Runtime.Engine.cppm` to store the service instead of `RuntimeObjectSpaceNormalBakeGpuQueue` and stop directly importing object-space normal bake queue modules.
- [x] Update `Runtime.Engine.cpp` so initialization, shutdown, and test accessors delegate through the service while preserving existing queue handoff to dependent runtime subsystems.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving object-space normal bake GPU-queue ownership and participant-registration helper code no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve object-space normal bake queue/submission/binding/GPU-queue CPU-contract coverage and Engine layering coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.ObjectSpaceNormalBakeService` and revise the Engine/object-space-normal current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer imports or stores `RuntimeObjectSpaceNormalBakeGpuQueue`.
- [x] `Runtime.Engine.cpp` no longer directly calls `RuntimeObjectSpaceNormalBakeGpuQueue::SetDependencies(...)`, `MakeGpuQueueParticipantDesc()`, or captures the object-space normal bake ready-frame callback.
- [x] Existing behavior remains unchanged: imported/direct/selected-mesh generated-normal callers receive the same request queue, non-operational backends still fail closed without CPU fallback, ready-frame stamping still uses the Engine device global frame number plus one, and shutdown still clears queue dependencies after GPU participants drain.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalBake|RuntimeEngineLayering|RuntimeJobService|JobServiceGpuQueueBridge' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing object-space normal bake request/stale-key/material-binding semantics.
- Adding or removing a CPU fallback for non-operational graphics backends.
- Adding production Vulkan plan-provider wiring or claiming `RUNTIME-129` closure.
- Changing JobService participant ordering, renderer frame-hook behavior, frame phase ordering, or shutdown ordering.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational` for this cleanup slice.
- This slice closes at `Operational` when live Engine initialization/shutdown delegates object-space normal bake service ownership to the new runtime module and focused object-space-normal/layering tests plus the default CPU gate pass. `RUNTIME-129` remains the owner of production Vulkan plan-provider and `gpu;vulkan` smoke closure.
