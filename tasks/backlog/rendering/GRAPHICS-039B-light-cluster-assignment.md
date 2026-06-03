# GRAPHICS-039B — Light-to-cluster assignment pass + overflow diagnostics

## Goal
- Land the light-to-cluster assignment compute pass (`GRAPHICS-039` decisions 3/4/5):
  test each extracted light's bounding volume against each cell AABB, emit per-cell
  light-index lists into a packed index buffer + offset/count header, with the
  256-lights/cell clamp and the decision-10 counters, tested under the null RHI.

## Non-goals
- No surface-shader integration (that is `GRAPHICS-039C`).
- No new extraction fields — consume the existing `LightSnapshot` (decision 8).

## Context
- Owner layer: `graphics/renderer` (assignment pass + light-system diagnostics).
- Depends on `GRAPHICS-039A` (cluster grid).
- Decision 3: per-cell light-index lists in a packed index buffer + per-cell
  `{ uint offset; uint count; }` header, written via an atomic bump-allocator.
- Decision 4: point=sphere vs AABB closest-point; spot=bounding-sphere prefilter then
  cone-vs-AABB SAT approximation (conservative over-inclusion, never drops a contributor);
  directional=skipped. Decision 5: hard 256 lights/cell, overflow clamps to the first 256
  and increments `LightClusterOverflowCount`.
- Decision 10: counters `LightClusterOverflowCount`, `LightsCulledCount`,
  `EmptyClusterCount` on the renderer light-system diagnostics.

## Required changes
- [ ] Add the packed light-index buffer + per-cell offset/count header resources.
- [ ] Add the assignment compute pass (sphere/cone inclusion, directional skip, atomic
      bump-allocator, 256 clamp).
- [ ] Add the three light-system diagnostic counters.
- [ ] `contract;graphics` null-RHI tests for inclusion shapes, directional skip, overflow
      clamp + counter, and empty-cell counting.

## Tests
- [ ] `contract;graphics` — sphere/cone inclusion correctness; directional lights skipped;
      overflow clamp at 256 + counter; empty-cell counting.
- [ ] CPU gate green.

## Docs
- [ ] Document the assignment pass + buffer layout in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] Assignment produces correct per-cell lists with conservative inclusion; overflow is
      fail-soft + counted.
- [ ] No new extraction fields; no new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding new runtime→graphics extraction fields for lights.
- Dropping a contributing light (inclusion must stay conservative).
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the assignment contract.
- `Operational` owned by `GRAPHICS-039C` (surface-shader integration + integration tests).
