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

- Completed on 2026-07-16 at `CPUContracted`.
- Implementation commit: `e8033e11`.
- Verification: focused queue/facade/lifetime coverage passed `31/31`, and the
  final default CPU-supported gate passed `3785/3785` after a successful
  `IntrinsicTests` build. Strict structural/review gates passed.

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
  unchanged queue declaration to one module/import-directive-free private
  header included by
  that facade; and attach the unchanged implementation unit to
  `Extrinsic.Runtime.SandboxEditorFacades`. Do not add a replacement module,
  partition, wrapper, compatibility facade, or queue test API.
- Lifetime constraints: retain the session-owned `unique_ptr`, borrowed device/
  buffer-manager/transfer-queue references, queue-before-participant member
  order, JobService registration/unregistration, device-idle shutdown callback,
  attachment-epoch guards, frame-command recording, maintenance drain, and
  completion-consumption ordering exactly.

## Required changes
- [x] Inventory queue consumers and decide whether the queue belongs behind the
      Sandbox editor content migration (`ARCH-006`) or a private runtime helper.
- [x] Replace direct module imports from editor/tests with public command/job
      seams or a narrow explicit test seam.
- [x] Keep queue diagnostics, duplicate-submit suppression, and completion drain
      behavior unchanged.
- [x] Remove the module file-set entry if the public surface is retired.
- [x] Record before/after compile metrics and consumer count.

## Tests
- [x] Run K-Means GPU queue, Sandbox editor, JobService, and clustering module
      tests.
- [x] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [x] Update runtime/editor docs if the queue moves behind app/editor-private
      composition.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] Runtime.Engine remains free of K-Means-specific imports and ownership.
- [x] Existing K-Means command/job behavior is unchanged.
- [x] The queue is no longer a public module unless the consumer inventory
      proves that it must be.

## Evidence

- Consumer inventory: the nine pre-change named-module importers were the
  Sandbox facade interface, two facade implementation units, and six split
  Sandbox contract sources. Queue-class ownership was private to
  `SandboxEditorSession`; only the request/submission/result/status records
  were a genuine public facade contract. Named-module importers are now zero,
  and the module/import-directive-free queue header has exactly one include
  owner: `Runtime.SandboxEditorFacades.cppm`.
- Surface metrics: runtime modules `80 -> 79`, repository modules `387 -> 386`,
  named-module importers `9 -> 0`, and the combined public interface surface
  fell from 3,224 lines / 59 imports to 3,179 lines / 57 imports. Including the
  40-line private declaration header, the declaration surface is 3,219 lines.
  Across the production slice, imports fell `237 -> 233`; export-imports were
  unchanged.
- Mechanical equivalence: the 513-line queue implementation body is unchanged
  apart from its module attachment; the DTO definitions and inline predicates
  are text-equivalent after moving to the public Sandbox facade. Queue-before-
  participant member order, unregister/idle-wait/reset teardown order, frame
  recording, maintenance drain, and completion consumption are unchanged.
- Compile diagnostics: the task baseline records the retired BMI at up to
  21.473 seconds; this session observed it at 19.312 seconds before the change.
  The facade BMI measured 164.684 seconds before and 135.992 seconds after;
  the attached implementation edge measured 11.598 seconds. These single-host
  diagnostics are not an aggregate build-speed claim.
- Focused K-Means/backend/queue/Sandbox/lifetime/private-glue coverage passed
  `31/31` after final review cleanup. A full `IntrinsicTests` build completed,
  and the default CPU-supported gate passed `3785/3785` in 399.36 seconds.
- Strict layering, allowlist quality, task policy/maturity, doc links, test
  layout, root hygiene, PR contract, skill-mirror sync, module-inventory
  freshness, diff checks, and the clean-workshop automated bundle passed.
  Independent design, lifetime, mechanical-equivalence, and right-sizing
  reviews found no remaining blocker. No new Vulkan operational claim is made.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEnginePrivateGlue.*KMeansGpuJobQueue|SandboxEditorUi\.KMeans|SandboxEditorSession\.StaleCopiedSurfacesFailAfterDetachAndReattach|SandboxEditorPresentation\.RuntimeFacadesCompileSeparatelyFromEditorShell|ClusteringModule|RuntimeJobService\.(GpuQueueParticipantRecordsDrainsAndUnregisters|GpuQueueShutdownRunsParticipantsInReverseOrder)|KMeansGpuBackend' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
