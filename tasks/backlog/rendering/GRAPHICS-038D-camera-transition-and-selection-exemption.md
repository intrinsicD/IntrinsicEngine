# GRAPHICS-038D — Camera-transition skip heuristic + selection-bucket exemption

## Goal
- Add the first-frame-after-teleport phase-1 skip heuristic (`GRAPHICS-038` decision 7)
  and the hard selection-bucket occlusion exemption (decision 9), driven by extracted
  camera-snapshot fields, with `contract;graphics` + integration tests.

## Non-goals
- No HZB resource/build/cull changes beyond consuming the camera-transition signal.
- No new RHI surface.

## Context
- Owner layer: `graphics/renderer` (heuristic + bucket routing), reading extracted
  camera-snapshot fields (decision 13 — no live ECS access).
- Depends on `GRAPHICS-038C` (two-phase cull).
- Decision 7: the first frame after a hard camera teleport skips phase-1 occlusion
  (stale HZB) and treats every instance as phase-1 visible; trigger on **both** a
  camera position/orientation delta threshold AND an explicit runtime teleport/scene-
  change flag on the extracted camera snapshot (either fires the skip); count
  `HzbStaleSkipCount`. Decision 9: the three selection buckets are never HZB-occlusion-
  culled (frustum-only, single phase-1 set, no phase-2 split) — a hard rule for stable picking.

## Required changes
- [ ] Add a teleport/scene-change flag (+ delta threshold) to the extracted camera snapshot.
- [ ] Skip phase-1 occlusion on the first frame after a transition; count `HzbStaleSkipCount`.
- [ ] Exempt the three selection buckets from occlusion culling (frustum-only, phase-1 only).
- [ ] `contract;graphics` + integration tests for the skip heuristic and selection exemption.

## Tests
- [ ] `contract;graphics` — delta-threshold and explicit-flag skip both fire; selection
      buckets never occlusion-culled; `HzbStaleSkipCount` accounting.
- [ ] `integration` — teleport frame treats all instances as phase-1 visible.
- [ ] CPU gate green.

## Docs
- [ ] Document the camera-transition heuristic + selection exemption in
      `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] Both skip triggers fire; selection picking stays stable under occlusion.
- [ ] Camera transitions arrive through the snapshot, not live ECS.
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Occlusion-culling the selection buckets (breaks picking determinism).
- Direct ECS observation of camera transitions from graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the heuristic + exemption contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
