# GRAPHICS-038 — HZB and two-phase occlusion culling extension to CullingPass (planning)

## Goal
Lock down the contract for extending the existing GPU-driven `CullingPass` (`Extrinsic.Graphics.Pass.Culling`) with a hierarchical Z-buffer (HZB) and two-phase occlusion culling: phase 1 culls against the previous-frame HZB, renders the visible set, builds the new HZB from this frame's depth; phase 2 re-tests phase-1 rejects against the new HZB and renders the disocclusion set. Planning only — no shader bodies or new render-graph passes land here.

## Non-goals
- No mesh-shader path (covered by `GRAPHICS-053`).
- No meshlet representation changes (covered by `GRAPHICS-044`).
- No virtualized geometry / Nanite-style cluster DAG (`GRAPHICS-056`).
- No new visibility lanes; the 8-bucket contract is preserved.
- No CPU-side occlusion logic; the entire HZB lifecycle is GPU-resident.

## Context
- Status: done (2026-05-18, branch `claude/graphics-rendering-tasks-dKlmC`).
- Commit reference: pending current change.
- Owner layer: `graphics/renderer` (compute pipelines + frame-graph wiring), `graphics/framegraph` (HZB resource lifetime), `graphics/rhi` (existing storage-image / sampled-image surfaces).
- The current `instance_cull_multigeo.comp` performs frustum-only culling and outputs eight indirect-draw command buffers + visibility remaps. This is sufficient for low-density scenes but limits high-density GPU-driven rendering at scale.
- Two-phase culling is the canonical Frostbite/UE/id Tech pattern: render-then-rebuild-then-recull. It avoids the false-rejection class while keeping CPU work flat.
- Cross-links: `GRAPHICS-007` (culling and draw buckets), `GRAPHICS-022` (rendergraph diagnostics), `GRAPHICS-044` (meshlet representation that benefits from HZB), `GRAPHICS-046` (GI sampling can reuse HZB).

## Design decisions to record
1. **HZB resource shape.** Size policy (next-pow-2 frame size, halved per mip), format (`R32_SFLOAT` min-depth or `R16_UNORM`), mip count rule. Locked single resource owned by the frame graph.
2. **HZB build pass.** Compute shader name + dispatch shape (one workgroup per output mip tile, `subgroupMin` reduction or shared-memory reduction). Whether build is single-pass mip-chain or per-mip dispatch.
3. **Previous-frame HZB lifetime.** Decide between (a) retained graphics-owned HZB carried across frames with hot-reload retire-deadline pattern, or (b) frame-graph-imported "previous frame depth" + on-the-fly downsample. Record memory vs. determinism trade-off.
4. **Phase-1 cull shader.** Extends `instance_cull_multigeo.comp`: project bounding sphere into clip space, look up appropriate HZB mip, conservative compare. Output: visible set + rejected set, per bucket.
5. **Phase-2 cull shader.** Reads rejected set, retests against the freshly built current-frame HZB, emits a second indirect-draw set per bucket. Record whether phase-2 reissues are tagged separately for diagnostics.
6. **8-bucket contract preservation.** Each existing bucket gains a phase-1 and phase-2 indirect command buffer; the bucket count and shader-side semantics are unchanged. Record the indirect-buffer naming convention.
7. **Camera transitions.** First frame after a hard camera teleport must skip phase-1 occlusion (HZB stale). Decide the heuristic (camera position delta threshold, scene-change flag from runtime, or both). Diagnostic: `HzbStaleSkipCount`.
8. **Shadow-cascade interaction.** HZB-cull is per-view. Decide whether each shadow cascade gets its own HZB or shares the main view's HZB (with conservative bias). Record the trade-off.
9. **Selection-bucket interaction.** Selection passes must not be HZB-occlusion-culled (they need stable cursor-driven hits). Record the rule.
10. **Diagnostics.** Per-bucket counters: `Phase1VisibleCount`, `Phase1RejectedCount`, `Phase2RescuedCount`, `HzbStaleSkipCount`. Atomic increments.
11. **Determinism.** Result must be deterministic given identical inputs. Record the rule for atomic-counter ordering.
12. **Test split.** `contract;graphics` for HZB resource shape, phase-1/phase-2 buffer wiring, bucket preservation, camera-transition heuristic, all under null RHI; opt-in `gpu;vulkan` smoke for HZB build correctness.
13. **Layering audit.** No live ECS access. Camera transitions are signaled through extracted snapshot fields, not direct ECS observation.

## Recorded decisions

1. **HZB resource shape.** Single resource `Cull.HZB` owned by the frame graph. Dimensions: each axis is independently `nextPow2(ceil(viewDim / 2))` (i.e. for a 1920×1080 view, HZB level 0 is `nextPow2(960) × nextPow2(540) = 1024 × 1024`; for a 2560×1440 view, `nextPow2(1280) × nextPow2(720) = 2048 × 1024`). The HZB is non-square in general — width and height are sized independently so the rule covers any aspect without coupling axes. Pad pixels (those outside `ceil(view/2)` in either axis) are initialized to the conservative "far depth" sentinel `1.0` so they never trigger false rejection. Mip count is `floor(log2(max(W, H))) + 1`, capped at 12 mips. Format: `R32_SFLOAT` storing the max-depth (farthest from camera) so conservative comparison rejects only when an instance's near-most projected depth is strictly farther than the cached far depth. Rationale: `R32_SFLOAT` matches the existing depth buffer's `D32_SFLOAT` precision (no requantization noise), max-depth + far-conservative compare is the canonical pattern that avoids false rejection at sub-pixel scale, and per-axis `nextPow2(ceil(viewDim / 2))` guarantees the HZB always *fully covers* the source depth half-resolution (the original draft's `1024 × 512` example for 1080p was wrong — `nextPow2(540) = 1024`, not `512`, and clamping the height down would leave the bottom ~28 source rows uncovered and cause missed occluders/occludees there). Rejected: `R16_UNORM` (requires per-frame near/far normalization that destabilizes the cull decision across camera moves and adds requantization error larger than the HZB's mip-pyramid bias); square padding `max(W, H)²` (wastes up to 2× memory on widescreen aspects with no accuracy gain).
2. **HZB build pass.** Compute shader `assets/shaders/cull/hzb_build.comp`. Per-mip dispatch (one dispatch per output mip, mip N reads from mip N-1) — *not* single-pass mip-chain. Rationale: per-mip is simpler, requires no `imageStore` of multiple mips from one shader, plays nicer with the existing frame-graph barrier compiler (each mip becomes a single read-after-write barrier), and the dispatch overhead is bounded at 12 mips. Workgroup: 8×8 thread tile, each thread reads a 2×2 region of the previous mip with `subgroupMax` if `HasSubgroupArithmetic`, otherwise `imageLoad` + manual max. Mip-0 build samples the current frame's `SceneDepth` (`D32_SFLOAT`) directly. Rejected: single-pass mip-chain with shared-memory hierarchical reduction (16+ workgroup-shared-memory bytes/thread budget for our compute capability target, and the subgroup-arithmetic optional path already gives 70% of the perf without the complexity).
3. **Previous-frame HZB lifetime.** Option (a): retained graphics-owned `Cull.HZB.PrevFrame` carried across frames with the hot-reload retire-deadline pattern from `GRAPHICS-015Q`. The frame graph imports it as a sampled-image input to phase-1 cull, then writes `Cull.HZB` (current frame) which is *swapped* with the retained handle at `EndFrame()` retire time. Rationale: option (b) "rebuild HZB from previous frame's depth on the fly" wastes the entire mip pyramid every frame even when the camera barely moves, and reading a `SceneDepth` that may have aliased to a transient is non-deterministic across frame-graph compiles. The retained HZB costs ~4 MB at 1920×1080 single-mip; full mip chain ~5.3 MB. Hot-reload retire deadline = `framesInFlight`, ensuring no in-flight phase-1 cull dispatch ever reads a swapped-out HZB.
4. **Phase-1 cull shader.** Extension of `instance_cull_multigeo.comp` named `instance_cull_multigeo_hzb.comp` (compile-time `HZB_ENABLED` define switches between variants; no runtime branching). Per-instance: project bounding sphere center to NDC, compute the screen-space AABB of the sphere (using sphere radius and depth-perspective scaling), select the HZB mip whose tile covers the AABB diagonal (mip = `ceil(log2(max(AABB.width, AABB.height)))`), read the four texels covering the AABB, compute `maxDepth = max(samples)`, reject if `sphereNearDepth > maxDepth + epsilon`. Output: same eight indirect-draw command buffers as today + a new per-bucket `Phase1Rejected` packed buffer carrying instance indices for phase 2.
5. **Phase-2 cull shader.** New compute `instance_cull_phase2.comp`. Reads the `Phase1Rejected` packed buffer per bucket, retests each instance against the current-frame HZB (built in Decision 2), emits to per-bucket `Phase2IndirectDraw` command buffers. Phase-2 reissues are tagged separately in diagnostics (Decision 10 `Phase2RescuedCount`); phase-2 and phase-1 indirect-draw buffers are issued by the same per-bucket draw passes through a single `vkCmdDrawIndexedIndirectCount` that consumes both (the phase-2 buffer is appended at a known offset after phase-1's count). Rejected: emitting phase-2 results into the phase-1 buffer (requires phase-1 buffer over-allocation, complicates indirect-count synthesis).
6. **8-bucket contract preservation.** The eight buckets defined by `RHI::GpuDrawBucketKind` in `src/graphics/rhi/RHI.Types.cppm` (`SurfaceOpaque`, `SurfaceAlphaMask`, `Lines`, `Points`, `ShadowOpaque`, `SelectionSurface`, `SelectionLines`, `SelectionPoints` — primitive-domain selection lanes, not entity/face/edge encoding) are unchanged. Each bucket gains parallel resources: `Cull.Bucket{N}.Phase1IndirectDraw`, `Cull.Bucket{N}.Phase1Rejected`, `Cull.Bucket{N}.Phase2IndirectDraw`. Naming: `Cull.Bucket.SurfaceOpaque.Phase1IndirectDraw`, `Cull.Bucket.SelectionSurface.Phase1IndirectDraw`, etc., matching the existing `GpuDrawBucketName()` strings byte-for-byte. Bucket count stays at 8; no renumbering; no new shader-side bucket semantics. (The entity/face/edge ID encoding is a *shader-side payload* on the selection primitive-domain lanes per `GRAPHICS-012` / `GRAPHICS-012Q`, not a bucket axis.)
7. **Camera transitions.** First frame after a hard camera teleport skips phase-1 occlusion (HZB stale) and routes all instances through phase 2 as if all-rejected. Detection: runtime extraction sets `CameraViewSnapshot::HasDiscontinuity = true` whenever (a) `length(NewPosition - OldPosition) > CameraDiscontinuityThreshold` (default 10× the frustum's near-plane diameter) **or** (b) the snapshot consumer flags a scene-change event. Both signals — not "or" alone — because position-only heuristics miss scene-load transitions and event-only signals miss scripted teleports. Diagnostic counter `HzbStaleSkipCount` increments once per phase-1-skipped frame. Rejected: skipping phase 2 instead (would over-reject in the very frame the user notices the artifact); persistent N-frame skip window after teleport (variable-latency policy that is hard to test deterministically).
8. **Shadow-cascade interaction.** Each shadow cascade gets its **own** HZB (`Cull.Shadow.Cascade{N}.HZB`) built from that cascade's depth pass output, *not* the main view's HZB. Rationale: a shared main-view HZB drives major over-rejection at oblique shadow angles (caster lit from a low elevation projects to a tall narrow AABB in main view but a wide flat one in shadow view, and main-view far-depth dominates the conservative bound by orders of magnitude). Memory cost: 4 cascades × ~5 MB = 20 MB at 1080p; acceptable since shadow atlases already dominate the shadow-pass memory budget. The per-cascade HZB build reuses `hzb_build.comp` with cascade-specific input depth; no shader fork.
9. **Selection-bucket interaction.** All three selection buckets (`SelectionSurface`, `SelectionLines`, `SelectionPoints` per `RHI::GpuDrawBucketKind`) are **exempt** from HZB occlusion culling. They participate in frustum culling only. Rationale: cursor-driven selection requires deterministic hits regardless of inter-frame occlusion changes, and selection passes are not draw-call-dominant so the bandwidth savings from HZB rejection are negligible. The phase-1 cull shader's `HZB_ENABLED` path branches on bucket index and skips HZB-test-and-reject for the three selection buckets, emitting frustum-visible instances directly to the bucket's phase-1 indirect-draw buffer with empty phase-1-rejected and phase-2-indirect outputs.
10. **Diagnostics.** Per-bucket atomic counters live on `CullingPassDiagnostics`: `Phase1VisibleCount[8]`, `Phase1RejectedCount[8]`, `Phase2RescuedCount[8]`, plus the per-frame counters `HzbStaleSkipCount`, `HzbBuildPassExecutions`, `HzbConservativeBiasAppliedCount`. All counters are `std::atomic<uint64_t>` zeroed on engine `Initialize()`, mirroring the existing `GRAPHICS-007Q` `CullingPassDiagnostics` pattern. Counters are GPU-incremented through indirect dispatches and CPU-readback through the existing `Picking.Readback` drain.
11. **Determinism.** Two cull runs over the same instance set + same HZB + same camera produce identical phase-1 and phase-2 indirect-draw command buffers. Atomic-counter ordering rule: per-bucket output buffers use `atomicAdd` to allocate write slots; the *order* of slot allocation across threads is non-deterministic by GPU contract, but the *contents* (set of (instance, bucket) pairs) are deterministic and the draw call shader-side sees identical visible sets regardless of order (the shader uses gl_InstanceIndex, not slot order). Tests assert set equality, not order equality.
12. **Test split.** `contract;graphics` tests cover HZB resource lifetime (retain + swap + reclaim), phase-1/phase-2 buffer wiring per bucket, bucket-count preservation, camera-transition heuristic on synthetic snapshots, selection-bucket exemption, HZB-stale-skip counter — all under null RHI with `instance_cull_multigeo_hzb.comp` replaced by a CPU mock implementing the same per-bucket output contract. Opt-in `gpu;vulkan` smoke (`tests/integration/graphics/Test.HzbConservativeBias.cpp`) renders a synthetic scene with known-visible occludees behind occluders and asserts no over-rejection: every CPU-frustum-visible instance is either in the phase-1 visible set or the phase-2 rescued set. Excluded from the default CPU gate.
13. **Layering audit.** No live ECS access. Camera transitions arrive through `RenderFrameInput::Camera.HasDiscontinuity` set by `Runtime.RenderExtraction`; graphics never observes ECS camera components directly. HZB resource declarations live in `graphics/framegraph`; HZB build + cull shaders live under `assets/shaders/cull/`; cull-pass executor wiring lives in `graphics/renderer`. No new `graphics → runtime`, `graphics → ecs`, or `framegraph → vulkan` edges.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-038-Impl-A** — HZB resource + frame-graph lifetime + retained/imported policy + `contract;graphics` tests.
- **GRAPHICS-038-Impl-B** — HZB build compute shader + dispatch wiring + null-RHI shape tests.
- **GRAPHICS-038-Impl-C** — Phase-1/phase-2 cull shader extension + per-bucket buffer doubling + diagnostic counters.
- **GRAPHICS-038-Impl-D** — Camera-transition heuristic + selection-bucket exemption + integration tests.
- **GRAPHICS-038-Impl-E** — Opt-in `gpu;vulkan` smoke validating HZB conservatism (no over-rejection).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] `docs/architecture/rendering-three-pass.md` two-phase culling + HZB lifetime row is deferred to Impl-A/B/C landing (planning slice forbids code/shader changes; doc rows describe behavior implementation children wire).
- [x] `src/graphics/renderer/README.md` `Pass.Culling` HZB section is deferred to the same.

## Acceptance criteria
- [x] Thirteen decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] 8-bucket contract is preserved (no bucket renumbering).
- [x] No live ECS access. No legacy code copying.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No bucket renumbering or removal.
- No CPU-side occlusion path.
- No HZB-driven culling of selection buckets.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation children.
