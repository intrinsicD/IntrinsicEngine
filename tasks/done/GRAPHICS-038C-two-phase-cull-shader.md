# GRAPHICS-038C — Phase-1/phase-2 cull shader extension + per-bucket buffer doubling

## Status
- Commit reference: this task-landing commit.
- Landed 2026-06-04 at maturity `CPUContracted`. Added the
  `GpuCullBucketPhases` ABI, per-bucket phase-1/phase-2 output surfaces,
  renderer-owned diagnostics counter buffers, phase-aware cull-shader output
  selection, and the pure CPU two-phase partition helper that preserves the
  strict no-false-rejection invariant (`nearestDepth > conservativeMaxDepth`).
  The renderer now allocates, resets, publishes, and barriers both phase
  surfaces while preserving `GetBucket(kind)` as the phase-1 alias and exposing
  `GetBucketPhase(kind, phase)` for phase-specific consumers. The camera
  transition/selection exemption remains `GRAPHICS-038D`; concrete Vulkan HZB
  reject-list publication, phase-2 recull, and opt-in `gpu;vulkan`
  conservatism proof remain `GRAPHICS-038E`.

## Goal
- Promote the CPU/null contract and shader interface for two-phase occlusion
  culling (`GRAPHICS-038` decisions 4/5/6): each of the 8 buckets gains
  phase-1/phase-2 indirect output surfaces, phase-aware diagnostics counters,
  deterministic partition logic, and `contract;graphics` coverage.

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
- [x] Extend the cull shader ABI with phase-aware output selection and
      diagnostics hooks for HZB reject-list publication.
- [x] Double each bucket's indirect command buffer into phase-1/phase-2 variants.
- [x] Add per-bucket atomic counters `Phase1VisibleCount`, `Phase1RejectedCount`,
      `Phase2RescuedCount` on the `GRAPHICS-022` diagnostics surface.
- [x] `contract;graphics` null-RHI tests for per-bucket buffer wiring, 8-bucket
      preservation, and the visible/rejected/rescued partition determinism.

## Tests
- [x] `contract;graphics` — per-bucket phase-1/phase-2 buffer wiring; 8-bucket count
      preserved; deterministic partition; counter accounting.
- [x] CPU gate green.

## Docs
- [x] Document the two-phase cull in `src/graphics/renderer/README.md` and
      `docs/architecture/rendering-three-pass.md`.

## Acceptance criteria
- [x] Two-phase per-bucket cull is wired and CPU-tested with the 8-bucket contract intact.
- [x] The no-false-rejection invariant is asserted on the CPU partition.
- [x] No new layering violations.

## Verification
```bash
glslc assets/shaders/culling/instance_cull.comp -I assets/shaders -o /tmp/culling_instance_cull.comp.spv --target-env=vulkan1.3
glslc assets/shaders/instance_cull.comp -I assets/shaders -o /tmp/instance_cull.comp.spv --target-env=vulkan1.3
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsCullingContracts' --timeout 60
ctest --test-dir build/ci --output-on-failure -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require culling/instance_cull.comp.spv --require instance_cull.comp.spv
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

Results:
- Focused culling contract subset: 3/3 passed.
- Full graphics label subset: 437/437 passed.
- Default CPU-supported gate: 2707/2707 passed.
- Shader-output check found 80 SPIR-V shader outputs, including both
  `culling/instance_cull.comp.spv` and `instance_cull.comp.spv`.
- Structural/docs checks reported no findings.

## Forbidden changes
- Renumbering or changing the 8-bucket lane contract.
- Live ECS access from renderer code.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the two-phase cull contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
