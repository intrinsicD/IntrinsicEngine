---
id: RUNTIME-122
theme: B
depends_on: [RUNTIME-120]
maturity_target: CPUContracted
---
# RUNTIME-122 — Declarative vertex layout descriptor and packer unification

## Goal
- Replace the three hard-coded AoS vertex structs (mesh / graph / point cloud)
  with one declarative `VertexLayout` descriptor that drives both AoS interleave
  and the SoA BDA offsets the surface/line/point shaders consume, and unify the
  per-kind packers on it.

## Non-goals
- No new visible channels beyond those already packed plus the RUNTIME-121 color
  channel.
- No partial / dirty-range upload (owned by RUNTIME-124).
- No shader algorithm changes beyond pointer/stride plumbing.

## Context
- Owning subsystem/layer: `src/runtime` packers and `src/graphics/renderer`
  geometry record; shaders read SoA via BDA (`PtrPositions`/`PtrNormals`/`PtrAux`)
  while packers emit interleaved AoS — this task makes the AoS<->SoA mapping a
  declared contract instead of three implicit per-struct conventions.
- Property naming is inconsistent: mesh/point cloud use `v:position`, graph uses
  `v:point`. This task unifies on `v:position` and documents the alias migration.
- Builds on the `VertexAttributeBinding` resolver from RUNTIME-120.

## Required changes
- [ ] Add a `VertexLayout` descriptor (channels, source types, offsets, stride,
      SoA vs interleaved policy) and a layout-driven packer core.
- [ ] Migrate mesh, graph, and point-cloud packers onto the shared core via
      per-kind layouts and bindings.
- [ ] Unify `v:point` -> `v:position` with a documented temporary alias and a
      removal follow-up.

## Tests
- [ ] Per-kind packer contract tests prove byte-identical output to the current
      packers for shared fixtures.
- [ ] A layout round-trip test (binding -> offsets -> SoA pointer math).

## Docs
- [ ] Update `src/runtime/README.md` and `src/graphics/renderer/README.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] All three packers share one layout-driven core; output is byte-identical
      for existing fixtures.
- [ ] Property naming is unified with a documented alias + removal task.
- [ ] Default-gate contract tests and layering checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GeometryPacker|VertexLayout' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors in a single commit.
- Introducing unrelated feature work.
- Changing visible vertex output bytes for existing fixtures without test proof.

## Maturity
- Target: `CPUContracted`; the layout core is CPU-testable. Any `Operational`
  GPU stride/pointer proof rides on RUNTIME-121's smoke; no separate
  `Operational` follow-up is owed by this slice.
