---
id: RUNTIME-170
theme: B
depends_on:
  - CI-003
  - RUNTIME-161
maturity_target: CPUContracted
---
# RUNTIME-170 — Privatize the object-space normal GPU queue surface

## Status

- Completed on 2026-07-16 at `CPUContracted`.
- Implementation commit: `ca17b9b9`.
- Verification: the focused selection passed 164/164, the full CPU-supported
  gate passed 3,780/3,780 in 398.27 seconds, and the strict
  structural/review bundle is green.

## Goal
- Keep object-space normal bake GPU queue ownership inside
  `ObjectSpaceNormalBakeService` by replacing
  `Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue` with service-owned private
  implementation state.

## Non-goals
- No production Vulkan bake plan-provider wiring; that remains owned by
  `RUNTIME-129`.
- No object-space normal bake behavior, diagnostics, or queue policy changes.
- No graphics/RHI ownership movement into ECS, assets, or app.

## Context
- Owner/layer: `runtime`; this queue bridges runtime job scheduling to graphics
  command recording through existing lower-layer contracts.
- Local 2026-07-10 triage measured the GPU queue interface at up to 31.541s,
  with one production consumer (`ObjectSpaceNormalBakeService`) and one focused
  test importer.
- `RUNTIME-161` extracted the service; this task makes the queue a service
  implementation detail while keeping the public CPU/null queue contract.

## Right-sizing

- Measured current surface: the queue interface is 118 lines with 9 imports,
  one production importer (`ObjectSpaceNormalBakeService`), and one direct
  focused-test importer. A no-ccache exact interface-object rebuild took
  26.73 s on this host; the service interface rebuild took 6.15 s.
- Simpler alternative: keep one public service module and one service-owned
  private state implementation. Preserve behavioral coverage through the
  service boundary or one explicit service test hook; do not replace the
  retired module with a registry, interface, factory, or private module
  framework.
- Blast radius: the queue/service sources, their focused contract and layering
  assertions, runtime CMake/README, and generated module inventory.
- Reintroduction trigger: only a present second production owner requiring an
  independently composed queue lifetime would justify a standalone surface.

## Implementation evidence

- The former service-plus-queue public surface measured 175 lines and 15
  direct import declarations (57/6 for the service and 118/9 for the queue).
  The resulting single service interface is 96 lines with 5 direct imports;
  the generated inventory therefore drops from 392 to 391 modules.
- The implementation keeps the request queue public but moves the GPU
  participant, plan-provider adaptation, retained submissions, and full
  diagnostics into one service-owned `Impl`. The focused contracts reach
  record/drain behavior only through `ObjectSpaceNormalBakeService` and
  `JobService`; two explicitly test-named free functions configure deterministic
  failure cases and copy the counters those contracts assert.
- The participant debug label intentionally remains
  `Runtime.ObjectSpaceNormalBakeGpuQueue` to preserve diagnostic continuity;
  it is not a module name, import, CMake entry, or exported type.
- A post-change no-ccache exact interface-object command completed in 36.73 s
  on this host, versus the 32.88 s sum of the separately measured pre-change
  queue and service targets. The post-change command also regenerated CMake and
  rescanned the module graph after the file-set change, so this is recorded as
  a single-host diagnostic and not a compile-time improvement claim. The
  demonstrated result is the structural reduction of one module, 79 interface
  lines, and 10 direct import declarations.

## Required changes
- [x] Inventory declarations in `Runtime.ObjectSpaceNormalBakeGpuQueue.cppm`
      into public service API needs and private queue implementation details.
- [x] Move queue class, plan-provider, and diagnostics internals behind
      `ObjectSpaceNormalBakeService` private source/header glue where possible.
- [x] Preserve test access through service-level APIs or a narrow explicit test
      seam; do not keep a public module only for tests.
- [x] Remove the `.cppm` from module file sets if the public surface is retired.
- [x] Record before/after interface/import/timing metrics.

## Tests
- [x] Run object-space normal bake queue/service tests, runtime job-service
      tests, and relevant sandbox/editor tests.
- [x] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [x] Update runtime/rendering docs and READMEs if they name the GPU queue module.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] `ObjectSpaceNormalBakeService` remains the public runtime owner of the GPU
      queue participant.
- [x] Queue diagnostics and fail-closed behavior remain covered by tests.
- [x] The standalone GPU queue module is removed or explicitly justified with
      before/after metrics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalBake|JobService|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Implementing the production Vulkan plan-provider path.
- Weakening fail-closed diagnostics on non-operational backends.
- Keeping an exported queue module solely for focused tests.

## Maturity
- Target: `CPUContracted`; the queue remains backend-neutral/fail-closed here.
  `Operational` owned by `RUNTIME-129`.
