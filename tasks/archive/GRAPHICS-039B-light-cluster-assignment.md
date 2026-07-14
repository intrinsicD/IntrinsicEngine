# GRAPHICS-039B — Light-to-cluster assignment pass + overflow diagnostics

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-05
- Completed: 2026-06-05
- Current slice: single-slice `CPUContracted` implementation. Landed
  clustered-light header/index buffer contracts, deterministic CPU assignment
  helper, `light_cluster_assign.comp`, shader-visible atomic counter clearing,
  conservative point/spot inclusion, directional skip, overflow/culled/empty
  diagnostics, frame-recipe assignment pass/resources, and docs sync.
- Next verification step: retired. Surface-shader consumption remains
  `GRAPHICS-039C`; async-compute affinity remains `GRAPHICS-039D`.

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
- [x] Add the packed light-index buffer + per-cell offset/count header resources,
      plus the shader-visible atomic allocation counter.
- [x] Add the assignment compute pass (sphere/cone inclusion, directional skip, atomic
      bump-allocator, 256 clamp).
- [x] Add the three light-system diagnostic counters.
- [x] `contract;graphics` null-RHI tests for inclusion shapes, directional skip, overflow
      clamp + diagnostics counter, shader counter clear, and empty-cell counting.

## Tests
- [x] `contract;graphics` — sphere/cone inclusion correctness; directional lights skipped;
      overflow clamp at 256 + diagnostics counter; shader counter clear; empty-cell counting.
- [x] CPU gate green.

## Docs
- [x] Document the assignment pass + buffer layout in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [x] Assignment produces correct per-cell lists with conservative inclusion; overflow is
      fail-soft + counted.
- [x] No new extraction fields; no new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsLightClusterGrid|FrameRecipeContract\.LightClusterAssignmentRequiresGridBuildAndImportedOutputs|FrameRecipeContract\.ClusterGridBuildRequiresDepthPrepassAndImportedTarget' --timeout 60
glslc assets/shaders/light_cluster_assign.comp -I assets/shaders -o /tmp/light_cluster_assign.comp.spv --target-env=vulkan1.3
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

## Forbidden changes
- Adding new runtime→graphics extraction fields for lights.
- Dropping a contributing light (inclusion must stay conservative).
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `CPUContracted` under the null RHI for the assignment contract.
- `Operational` owned by `GRAPHICS-039C` (surface-shader integration + integration tests).
