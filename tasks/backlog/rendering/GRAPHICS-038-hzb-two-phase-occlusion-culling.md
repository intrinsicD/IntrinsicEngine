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

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-038-Impl-A** — HZB resource + frame-graph lifetime + retained/imported policy + `contract;graphics` tests.
- **GRAPHICS-038-Impl-B** — HZB build compute shader + dispatch wiring + null-RHI shape tests.
- **GRAPHICS-038-Impl-C** — Phase-1/phase-2 cull shader extension + per-bucket buffer doubling + diagnostic counters.
- **GRAPHICS-038-Impl-D** — Camera-transition heuristic + selection-bucket exemption + integration tests.
- **GRAPHICS-038-Impl-E** — Opt-in `gpu;vulkan` smoke validating HZB conservatism (no over-rejection).

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` with the two-phase culling shape and HZB lifetime rule.
- Update `src/graphics/renderer/README.md` `Pass.Culling` section.

## Acceptance criteria
- Thirteen decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- 8-bucket contract is preserved (no bucket renumbering).
- No live ECS access. No legacy code copying.

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
