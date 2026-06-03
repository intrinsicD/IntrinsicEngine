# GRAPHICS-039D — Cluster build/assignment async-compute affinity

## Goal
- Tag the cluster build and light-assignment passes with `QueueAffinity::AsyncCompute`
  (`GRAPHICS-039` decision 9) so they overlap the current frame's raster, defaulting to
  the graphics queue until `GRAPHICS-037` lands, with null-RHI tagging tests.

## Non-goals
- No correctness dependence on `GRAPHICS-037` — the passes must remain correct on the
  graphics queue when async compute is unavailable.

## Context
- Owner layer: `graphics/renderer` (pass affinity tagging).
- Depends on `GRAPHICS-039C` (clustered lighting operational) and `GRAPHICS-037A`
  (`QueueAffinity` surface). Gated by `GRAPHICS-037` for real async execution.
- Decision 9: cluster build + assignment are tagged `AsyncCompute` once `GRAPHICS-037`
  lands; default until then is the graphics queue. The work is independent of the
  current frame's raster and overlaps naturally on async compute.

## Required changes
- [ ] Tag the cluster build + assignment passes `QueueAffinity::AsyncCompute`.
- [ ] Confirm capability-absent demotion to the graphics queue keeps the schedule correct
      (via the `GRAPHICS-037A` demotion helper).
- [ ] `contract;graphics` null-RHI tests asserting the affinity tag + demotion fallback.

## Tests
- [ ] `contract;graphics` — affinity tagging; demotion to graphics queue when async
      compute is absent; correctness preserved on the single-queue path.
- [ ] CPU gate green.

## Docs
- [ ] Note the async-compute affinity in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] The cluster passes carry the async-compute affinity and demote correctly.
- [ ] Correctness holds on the graphics queue (no dependence on async execution).
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Making cluster correctness depend on async-compute availability.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the affinity-tagging + demotion contract.
- `Operational` owned by `GRAPHICS-037D` (real async-compute multi-queue submission smoke).
