# GRAPHICS-038 — HZB and two-phase occlusion culling extension to CullingPass (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for extending the existing GPU-driven `CullingPass` (`Extrinsic.Graphics.Pass.Culling`) with a hierarchical Z-buffer (HZB) and two-phase occlusion culling: phase 1 culls against the previous-frame HZB, renders the visible set, builds the new HZB from this frame's depth; phase 2 re-tests phase-1 rejects against the new HZB and renders the disocclusion set. Planning only — no shader bodies or new render-graph passes land here.

## Non-goals
- No mesh-shader path (covered by `GRAPHICS-053`).
- No meshlet representation changes (covered by `GRAPHICS-044`).
- No virtualized geometry / Nanite-style cluster DAG (`GRAPHICS-056`).
- No new visibility lanes; the 8-bucket contract is preserved.
- No CPU-side occlusion logic; the entire HZB lifecycle is GPU-resident.

## Context
- Owner layer: `graphics/renderer` (compute pipelines + frame-graph wiring), `graphics/framegraph` (HZB resource lifetime), `graphics/rhi` (existing storage-image / sampled-image surfaces).
- The current `instance_cull_multigeo.comp` performs frustum-only culling and outputs eight indirect-draw command buffers + visibility remaps. This is sufficient for low-density scenes but limits high-density GPU-driven rendering at scale.
- Two-phase culling is the canonical Frostbite/UE/id Tech pattern: render-then-rebuild-then-recull. It avoids the false-rejection class while keeping CPU work flat.
- Cross-links: `GRAPHICS-007` (culling and draw buckets), `GRAPHICS-022` (rendergraph diagnostics), `GRAPHICS-044` (meshlet representation that benefits from HZB), `GRAPHICS-046` (GI sampling can reuse HZB).

## Recorded decisions
1. **HZB resource shape.** A single frame-graph-owned resource, `R32_SFLOAT`, sized to the next power of two of the render extent and halved per mip down to 1×1, mip count `floor(log2(max(w,h))) + 1`. It stores per-tile conservative **maximum** depth (built with a max reduction). Under the engine's standard-Z `Less` convention (near = small depth, GRAPHICS-031 `DepthCompareOp = Less`), the farthest drawn surface bounds occlusion, so an instance is culled only when its screen-bounded **nearest** depth exceeds the sampled HZB max-depth — the no-false-rejection invariant. Rationale: `R32_SFLOAT` matches the `D32_FLOAT` source with no precision loss; pow2 sizing keeps the reduction chain exact; `R16_UNORM` was rejected because it can quantize the conservative bound the wrong way and wrongly cull visible geometry.
2. **HZB build pass.** Compute shader `hzb_build.comp`; one workgroup per output tile using `subgroupMax` where the subgroup capability is present, else a shared-memory tree max-reduction. Build is a **single-pass mip-chain** down-sampler (SPD-style, last-workgroup mip stitching via global atomics) where that capability is available, with a **per-mip dispatch fallback** otherwise. Rationale: single-pass minimizes barriers and dispatch count; the per-mip fallback keeps the chain correct on minimal hosts.
3. **Previous-frame HZB lifetime.** Choose **(a)** — a retained graphics-owned HZB carried across frames via the GRAPHICS-015Q retire-deadline pattern, ping-ponged so phase 1 reads the previous-frame HZB and phase 2 writes the current-frame HZB (also next frame's source). Reject (b) imported-previous-depth + on-the-fly downsample because it repeats the reduction every frame and complicates determinism. Trade-off: the retained HZB costs ≈1.33× one `R32` mip-chain per view (bounded) but yields a deterministic single-build-per-frame pipeline; resize/format changes reallocate it through the retire-deadline window.
4. **Phase-1 cull shader.** Extends `instance_cull_multigeo.comp`: project each instance's bounding sphere to clip space, derive its screen rect + nearest depth, pick the HZB mip whose texel ≥ the rect size, sample the conservative max-depth, and compare per decision 1. Output per bucket: a **visible set** (drawn in phase 1) and a **rejected set** (deferred to phase 2), tested against the previous-frame HZB. Rationale: reuses the existing bounding-sphere/frustum machinery, adding only the HZB lookup and reject-list emission.
5. **Phase-2 cull shader.** Reads the phase-1 rejected set, retests it against the freshly built **current-frame** HZB, and emits a second per-bucket indirect-draw set for disocclusion survivors. Phase-2 reissues are **tagged separately** (`Phase2RescuedCount`) so the disocclusion rate is observable. Rationale: recovers instances wrongly rejected by the stale previous-frame HZB with no CPU round-trip.
6. **8-bucket contract preservation.** Each of the existing 8 buckets gains a **phase-1** and **phase-2** indirect command buffer; bucket count and shader-side semantics are unchanged. Naming convention: `<bucket>.Indirect.Phase1` / `<bucket>.Indirect.Phase2`. Rationale: preserves the GRAPHICS-007 lane contract (no renumbering); only the indirect buffers double.
7. **Camera transitions.** The first frame after a hard camera teleport skips phase-1 occlusion (stale HZB) and treats every instance as phase-1 visible (phase 2 is a no-op that frame). Heuristic: **both** a camera position/orientation delta threshold AND an explicit runtime teleport/scene-change flag carried on the extracted camera snapshot — either one triggers the skip. Diagnostic: `HzbStaleSkipCount`. Rationale: the delta threshold catches large cuts automatically while the explicit flag lets runtime force-skip on teleport/scene-load without trusting a heuristic.
8. **Shadow-cascade interaction.** Each cascade that uses occlusion culling gets its **own** per-view HZB built from that cascade's depth — not the main view's HZB. Sharing the main view's HZB with a conservative bias is rejected because main-view depth does not bound shadow-view occlusion. Trade-off: per-cascade HZB costs one mip-chain per active cascade; to bound first-implementation cost, cascades may opt out of occlusion culling entirely (frustum-only), with the per-cascade HZB as the recorded target.
9. **Selection-bucket interaction.** The three selection buckets are **never** HZB-occlusion-culled (frustum-only) and emit a single phase-1 set with no phase-2 split, so cursor-driven picking hits stay stable regardless of occlusion. Hard rule. Rationale: occlusion-culling selection geometry would drop pickable-but-occluded entities and break selection determinism.
10. **Diagnostics.** Per-bucket atomic counters `Phase1VisibleCount`, `Phase1RejectedCount`, `Phase2RescuedCount`, plus the per-view `HzbStaleSkipCount`, surfaced through the GRAPHICS-022 diagnostics surface. Atomic increments only; no per-frame strings.
11. **Determinism.** Given identical inputs (camera snapshot, instance set, previous-frame HZB) the visible/rejected/rescued partition is deterministic. Correctness does **not** depend on atomic-counter ordering — counters are commutative sums, and each per-instance visibility decision is a pure function of its bounds plus the sampled HZB texel, independent of dispatch order. Rationale: keeps results reproducible for `contract;graphics` assertions.
12. **Test split.** `contract;graphics` (null RHI) for HZB resource shape/mip count, phase-1/phase-2 per-bucket buffer wiring, 8-bucket preservation, the camera-transition skip heuristic, and the selection-bucket exemption; opt-in `gpu;vulkan` smoke (`GRAPHICS-038-Impl-E`) validating HZB conservatism — no over-rejection of a known-visible probe instance.
13. **Layering audit.** No live ECS access; camera transitions arrive through extracted camera-snapshot fields (decision 7), not direct ECS observation. HZB lifetime is frame-graph/graphics-owned, the cull shaders live in `graphics/renderer`, and no new RHI surface beyond the existing storage-image/sampled-image is added. No AGENTS.md §2 edge is crossed.

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
- [x] Two-phase culling shape + HZB lifetime rule for `docs/architecture/rendering-three-pass.md` are deferred to the implementation children (`GRAPHICS-038-Impl-A/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] `Pass.Culling` section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

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

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All thirteen HZB / two-phase occlusion-culling decisions are recorded with explicit answers and trade-off rationales: the conservative max-depth `R32_SFLOAT` HZB shape (standard-Z no-false-rejection invariant), single-pass/per-mip build, retained ping-pong previous-frame lifetime, phase-1/phase-2 cull shaders, the preserved 8-bucket contract with `.Phase1`/`.Phase2` indirect buffers, the dual camera-transition skip heuristic, per-cascade shadow HZB, the selection-bucket exemption, the four diagnostic counters, order-independent determinism, the test split, and the layering audit. Implementation children `GRAPHICS-038-Impl-A..E` are identified but not opened; the 8-bucket contract is preserved and no code lands. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No bucket renumbering or removal.
- No CPU-side occlusion path.
- No HZB-driven culling of selection buckets.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation children.
