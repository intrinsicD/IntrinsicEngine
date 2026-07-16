---
id: RUNTIME-173
theme: F
depends_on:
  - CI-003
  - RUNTIME-137
maturity_target: CPUContracted
---
# RUNTIME-173 — Privatize the K-Means GPU job queue surface

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- Next gate: move the public queue DTO contract into
  `Extrinsic.Runtime.SandboxEditorFacades`, make the queue class private to
  that module, and build the focused Sandbox/JobService coverage.

## Goal
- Remove `Extrinsic.Runtime.KMeansGpuJobQueue` as a broadly importable runtime
  module by moving the queue behind the Sandbox editor/module integration seam
  or a private runtime header, preserving current CPU/null and GPU-queue
  contract behavior.

## Non-goals
- No K-Means algorithm, method, command, or UI behavior changes.
- No new Vulkan operational proof.
- No reintroduction of K-Means ownership into `Runtime.Engine`.

## Context
- Owner/layer: `runtime` for current editor/job integration; algorithm
  semantics remain in geometry/method-owned code.
- Local 2026-07-10 triage measured `Runtime.KMeansGpuJobQueue.cppm` at up to
  21.473s with mostly editor/test consumers.
- `RUNTIME-137` moved the queue path onto the `JobService` `GpuQueue` registry;
  this task narrows the compile surface after that ownership split.
- Pre-change inventory: nine sources import the named module: the Sandbox
  facade interface plus two facade implementation units and six split Sandbox
  contract sources. Queue-class use is entirely private to
  `SandboxEditorSession`; only the request/submission/result/status DTOs form a
  genuine public command injection contract, used directly by one clustering
  test. The other five test imports are unused, and the exported debug-name
  function has no callers.
- Right-sized shape: preserve the four DTO/status definitions and inline
  predicates under their existing names in the public Sandbox facade; move the
  unchanged queue declaration to one directive-free private header included by
  that facade; and attach the unchanged implementation unit to
  `Extrinsic.Runtime.SandboxEditorFacades`. Do not add a replacement module,
  partition, wrapper, compatibility facade, or queue test API.
- Lifetime constraints: retain the session-owned `unique_ptr`, borrowed device/
  buffer-manager/transfer-queue references, queue-before-participant member
  order, JobService registration/unregistration, device-idle shutdown callback,
  attachment-epoch guards, frame-command recording, maintenance drain, and
  completion-consumption ordering exactly.

## Required changes
- [ ] Inventory queue consumers and decide whether the queue belongs behind the
      Sandbox editor content migration (`ARCH-006`) or a private runtime helper.
- [ ] Replace direct module imports from editor/tests with public command/job
      seams or a narrow explicit test seam.
- [ ] Keep queue diagnostics, duplicate-submit suppression, and completion drain
      behavior unchanged.
- [ ] Remove the module file-set entry if the public surface is retired.
- [ ] Record before/after compile metrics and consumer count.

## Tests
- [ ] Run K-Means GPU queue, Sandbox editor, JobService, and clustering module
      tests.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update runtime/editor docs if the queue moves behind app/editor-private
      composition.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] Runtime.Engine remains free of K-Means-specific imports and ownership.
- [ ] Existing K-Means command/job behavior is unchanged.
- [ ] The queue is no longer a public module unless the consumer inventory
      proves that it must be.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'KMeans|ClusteringModule|JobService|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Moving K-Means execution back into `Runtime.Engine`.
- Changing algorithm outputs, diagnostics, or command semantics.
- Claiming Vulkan operational coverage from CPU/null tests.

## Maturity
- Target: `CPUContracted`; no new `Operational` proof is owed here because this
  task only changes compile-surface shape for the existing queue contract.
