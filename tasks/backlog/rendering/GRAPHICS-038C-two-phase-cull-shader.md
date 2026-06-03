# GRAPHICS-038C — Phase-1/phase-2 cull shader extension + per-bucket buffer doubling

## Goal
- Extend `instance_cull_multigeo.comp` to a two-phase occlusion cull against the HZB
  (`GRAPHICS-038` decisions 4/5/6): phase 1 tests instances against the previous-frame
  HZB and emits visible + rejected sets; phase 2 retests rejects against the
  current-frame HZB; each of the 8 buckets gains phase-1/phase-2 indirect buffers, with
  the decision-10 diagnostic counters and `contract;graphics` tests.

## Non-goals
- No camera-transition heuristic / selection exemption (that is `GRAPHICS-038D`).
- No bucket renumbering — the 8-bucket lane contract is preserved (decision 6).

## Context
- Owner layer: `graphics/renderer` (cull shaders + indirect buffers).
- Depends on `GRAPHICS-038A` (HZB resource), `GRAPHICS-038B` (build pass), and
  `GRAPHICS-007` (8-bucket culling contract, done).
- Decision 4: project each instance's bounding sphere to clip space, derive screen rect
  + nearest depth, pick the HZB mip whose texel ≥ rect size, sample conservative
  max-depth, cull per the decision-1 invariant. Decision 5: phase 2 retests the rejected
  set against the freshly built current-frame HZB; rescues tagged `Phase2RescuedCount`.
- Decision 6: each bucket gains `<bucket>.Indirect.Phase1` / `<bucket>.Indirect.Phase2`;
  bucket count + shader semantics unchanged. Decision 11: the partition is deterministic
  and order-independent (counters are commutative sums).

## Required changes
- [ ] Extend the cull shader with HZB lookup + reject-list emission (phase 1) and
      reject-set retest (phase 2).
- [ ] Double each bucket's indirect command buffer into phase-1/phase-2 variants.
- [ ] Add per-bucket atomic counters `Phase1VisibleCount`, `Phase1RejectedCount`,
      `Phase2RescuedCount` on the `GRAPHICS-022` diagnostics surface.
- [ ] `contract;graphics` null-RHI tests for per-bucket buffer wiring, 8-bucket
      preservation, and the visible/rejected/rescued partition determinism.

## Tests
- [ ] `contract;graphics` — per-bucket phase-1/phase-2 buffer wiring; 8-bucket count
      preserved; deterministic partition; counter accounting.
- [ ] CPU gate green.

## Docs
- [ ] Document the two-phase cull in `src/graphics/renderer/README.md` and
      `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [ ] Two-phase per-bucket cull is wired and CPU-tested with the 8-bucket contract intact.
- [ ] The no-false-rejection invariant is asserted on the CPU partition.
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Renumbering or changing the 8-bucket lane contract.
- Live ECS access from renderer code.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the two-phase cull contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
