# GRAPHICS-052 — Deltaful GPU-resident scene (planning)

## Goal
Lock down the contract for replacing the current per-frame full re-extraction of the render world with an incremental, *delta*-based extraction in which `RuntimeRenderSnapshotBatch` carries only added/removed/changed records, the GPU scene buffer is updated in place, and visibility/culling reads a persistent buffer that is not re-uploaded each frame. Planning only — no extraction-side code changes here.

## Non-goals
- No changes to the snapshot-extraction *boundary* (`AGENTS.md` §4); only its *shape* (full vs delta).
- No removal of the existing full-extraction path; it remains the unconditional default until Impl-D ships.
- No changes to `ImmutableRenderWorld` field shapes.
- No live ECS access from graphics.
- No CPU-side delta computation in graphics; runtime computes deltas.

## Context
- Owner layer: `runtime/` (delta computation), `graphics/renderer` (persistent GPU scene buffer + apply-deltas pass), `graphics/rhi` (existing buffer surface).
- Today extraction re-fills `RuntimeRenderSnapshotBatch` from scratch each frame, then uploads. This is fine for low-density scenes but does not scale. Modern engines (Bevy delta extraction, UE5 GPU scene, Unity DOTS Hybrid Renderer V2) ship deltas.
- The existing backlog item `GRAPHICS-004Q` already flagged this; this task is the planning slice that makes it actionable in the modernization roadmap.
- Cross-links: `GRAPHICS-002` (snapshot contract), `GRAPHICS-004` family (GPU world allocation), `GRAPHICS-016` (extraction handoff), `GRAPHICS-028` (residency bridge), `GRAPHICS-036` (pipelined frames; the persistent buffer interacts with double-buffered render world).

## Design decisions to record
1. **Delta record shape.** Per change kind: `InstanceAdded(InstanceId, GpuInstanceStatic, GpuInstanceDynamic)`, `InstanceRemoved(InstanceId)`, `InstanceUpdated(InstanceId, FieldMask, payload)`, similarly for materials, lights, geometries. Record the canonical layout.
2. **Field mask.** Per-instance-update field mask (transform, render flags, visibility mask, material slot, etc.). Updates carry only changed fields. Record the mask encoding.
3. **Persistent GPU scene buffer.** Owned by `graphics/renderer`, retained across frames, sized for max-instances policy. Record the resize rule.
4. **Apply-deltas pass.** A compute pass at frame start that reads the delta batch and writes the persistent buffer in place. Record the dispatch shape and atomic-write rules.
5. **Compaction.** Removed instances leave holes; periodic compaction reclaims them. Record the compaction trigger heuristic and the relocation reporting (per `GRAPHICS-005`).
6. **Pipelined-frame interaction.** With double-buffered render world (`GRAPHICS-036`), each buffer carries the delta queue *since* the previous reclaimed snapshot. Record the synchronization rule.
7. **Full-extract fallback.** A boolean recipe flag forces full re-extraction (for tests, benchmarks, scene reset). Record the rule.
8. **Determinism.** Apply-deltas produces a buffer state identical to a from-scratch extract. Record the equivalence test fixture.
9. **Diagnostics.** `DeltaRecordsPerFrameHistogram[Add/Remove/Update]`, `SceneCompactionFrames`, `FullExtractFallbackCount`. Counters atomic.
10. **Test split.** `contract;runtime` for delta-record correctness; `contract;graphics` for apply-deltas pass under null RHI; `integration` for end-to-end equivalence with full-extract baseline.
11. **Layering.** Runtime computes deltas. Graphics applies them. No live ECS access. Snapshot-extraction boundary is preserved.
12. **Performance bound.** Delta-apply CPU cost is bounded by O(changed records); GPU dispatch cost is bounded by O(changed records). Recorded in benchmarks.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-052-Impl-A** — Delta record shape + field-mask encoding + `contract;runtime` shape tests.
- **GRAPHICS-052-Impl-B** — Persistent GPU scene buffer + apply-deltas pass + null-RHI shape tests.
- **GRAPHICS-052-Impl-C** — Compaction + relocation reporting integration with `GRAPHICS-005`.
- **GRAPHICS-052-Impl-D** — End-to-end full-vs-delta equivalence integration tests + recipe selection.
- **GRAPHICS-052-Impl-E** — Pipelined-frame interaction wiring (gated by `GRAPHICS-036`).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/graphics.md` snapshot-extraction section to record the delta shape.
- [ ] Update `src/runtime/README.md` extraction section.
- [ ] Update `src/graphics/renderer/README.md` scene-buffer section.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Snapshot-extraction boundary is preserved.
- [ ] Full-extract path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No live ECS access from graphics.
- No removal of full-extract path.
- No bypass of snapshot-extraction boundary.
- No mixing of mechanical file moves with semantic refactors.
