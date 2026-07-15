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

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- Next gate: replace the standalone exported queue module with service-owned
  implementation state, then build and run the focused queue/service contract.

## Goal
- Keep object-space normal bake GPU queue ownership inside
  `ObjectSpaceNormalBakeService` by replacing
  `Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue` with a private
  implementation/header seam.

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

## Required changes
- [ ] Inventory declarations in `Runtime.ObjectSpaceNormalBakeGpuQueue.cppm`
      into public service API needs and private queue implementation details.
- [ ] Move queue class, plan-provider, and diagnostics internals behind
      `ObjectSpaceNormalBakeService` private source/header glue where possible.
- [ ] Preserve test access through service-level APIs or a narrow explicit test
      seam; do not keep a public module only for tests.
- [ ] Remove the `.cppm` from module file sets if the public surface is retired.
- [ ] Record before/after interface/import/timing metrics.

## Tests
- [ ] Run object-space normal bake queue/service tests, runtime job-service
      tests, and relevant sandbox/editor tests.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update runtime/rendering docs and READMEs if they name the GPU queue module.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] `ObjectSpaceNormalBakeService` remains the public runtime owner of the GPU
      queue participant.
- [ ] Queue diagnostics and fail-closed behavior remain covered by tests.
- [ ] The standalone GPU queue module is removed or explicitly justified with
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
