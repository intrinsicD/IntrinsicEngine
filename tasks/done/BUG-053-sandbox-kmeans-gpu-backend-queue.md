---
id: BUG-053
theme: F
depends_on: []
maturity_target: CPUContracted
---
# BUG-053 — Sandbox K-Means GPU backend queue

## Goal
- Selecting the Sandbox K-Means Vulkan backend submits work to a runtime-owned GPU job queue instead of falling through the synchronous CPU fallback path.

## Non-goals
- Do not change `Geometry.KMeans` CPU reference semantics.
- Do not make `Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)` block on GPU completion.
- Do not add a compute-only RHI submission API in this slice.

## Context
- Owner layer: `runtime`.
- `Extrinsic.Runtime.KMeansGpuBackend` already owns the explicit command-recording and async-readback primitives.
- The bug is in Sandbox integration: `ApplySandboxEditorKMeansCommand(...)` routes GPU requests through the synchronous convenience overload, which correctly falls back because it lacks command context, persistent cache, and deferred readback ownership.

## Completion
- Completed: 2026-07-02. Commit/PR: this local fix commit.
- Root cause: the Sandbox command path used the synchronous `Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)` convenience overload for Vulkan requests. That overload correctly cannot execute GPU work because it owns no command context, pipeline handles, persistent buffers, transfer queue, or deferred readback state.
- Follow-up root cause found during live Sandbox use: the queued GPU path created
  `kmeans_assign.comp.spv`, whose previous shader source required
  `GL_EXT_shader_atomic_float` / `GL_EXT_shader_atomic_int64` and emitted
  `OpAtomicFAddEXT` plus 64-bit atomic capability requirements. Vulkan validation
  rejected shader-module creation on devices where those optional features were
  not enabled.
- Fix summary: runtime now owns `Extrinsic.Runtime.KMeansGpuJobQueue`, a single-flight queue installed through the renderer runtime frame-command hook. Sandbox Vulkan K-Means requests submit to that queue, report `Pending` while in flight, and publish completed GPU labels/colors through the same ECS property publication path as CPU K-Means. The synchronous overload remains a truthful nonblocking CPU fallback seam. The assign/update shaders now use a portable assignment plus per-cluster scan path, avoiding optional float/int64 atomic shader features; GRAPHICS-111 owns the future fast segmented-reduction replacement.

## Control surfaces
- UI: Sandbox processing K-Means backend selector.
- Agent/CLI: `ApplySandboxEditorKMeansCommand(...)`.

## Backends
- Backend axis: `Geometry::KMeans::Backend::CPU` vs `Geometry::KMeans::Backend::GPU`.
- CPU/null behavior remains a truthful fallback when no operational GPU queue is available.

## Required changes
- [x] Add a runtime-owned K-Means GPU job queue that owns pipeline handles, persistent K-Means GPU resources, and async readbacks.
- [x] Wire `Engine` to record queued K-Means GPU phases inside the renderer frame command context and drain completions during maintenance.
- [x] Route Sandbox K-Means GPU requests through the job queue and publish completed GPU results back to ECS on a later frame.
- [x] Preserve CPU reference behavior and device-unavailable fallback telemetry.

## Tests
- [x] Add/update runtime contract coverage proving the Sandbox GPU request uses the queued path when the queue accepts it.
- [x] Add runtime contract coverage guarding that the KMeans assign shader does not require optional float/int64 atomic shader features.
- [x] Keep existing CPU/null K-Means fallback coverage passing.

## Docs
- [x] Update runtime module documentation for the new K-Means GPU queue owner.
- [x] Update the KMeans GPU migration note to describe the current portable shader path and the GRAPHICS-111 optimization follow-up.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring the task.

## Acceptance criteria
- [x] Sandbox K-Means GPU selection no longer reports CPU fallback merely because the synchronous command lacks GPU command/readback resources.
- [x] GPU work completion is asynchronous and publishes the same K-Means label/color properties as the CPU path.
- [x] The synchronous backend overload remains a nonblocking CPU fallback seam.
- [x] KMeans GPU shader-module creation no longer requires optional float atomic or int64 atomic Vulkan features.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle.*RuntimeFrameCommandHook|SandboxEditorUi.*KMeans|RuntimeKMeansBackend|RuntimeKMeansGpuBackend' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 8/8 tests.

cmake --build --preset ci --target IntrinsicRuntimeKMeansGpuSmokeTests_Shaders
spirv-val --target-env vulkan1.3 build/ci/bin/shaders/kmeans_assign.comp.spv
spirv-val --target-env vulkan1.3 build/ci/bin/shaders/kmeans_update.comp.spv
spirv-val --target-env vulkan1.3 build/ci/bin/shaders/kmeans_reset.comp.spv
# Passed.

git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
# Passed.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reverting the synchronous K-Means backend to a blocking GPU implementation.

## Maturity
- Target: `CPUContracted` for Sandbox integration in the default gate; the raw GPU recorder/readback path remains `Operational` under the existing opt-in `gpu;vulkan` K-Means smoke.
