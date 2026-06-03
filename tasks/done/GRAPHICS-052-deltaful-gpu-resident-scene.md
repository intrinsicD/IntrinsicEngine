# GRAPHICS-052 — Deltaful GPU-resident scene (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until the residency/extraction prerequisites (`GRAPHICS-028`/`GRAPHICS-036`) feed an implementation slice.

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

## Recorded decisions
1. **Delta record shape.** Per change kind: `InstanceAdded(InstanceId, GpuInstanceStatic, GpuInstanceDynamic)`, `InstanceRemoved(InstanceId)`, `InstanceUpdated(InstanceId, FieldMask, payload)`, with the same triplet for materials, lights, and geometries; the canonical layout is recorded. Rationale: a typed add/remove/update record per resource class is the minimal description of a frame's scene change, and mirroring the existing full-record types (`GpuInstanceStatic`/`Dynamic`) keeps the apply path writing the same memory layout the full extract produces.
2. **Field mask.** A per-instance-update field mask (transform, render flags, visibility mask, material slot, etc.) so updates carry only changed fields; the mask encoding is recorded. Rationale: most per-frame updates touch one or two fields (typically transform), so a field mask makes `InstanceUpdated` payloads proportional to what actually changed rather than re-sending the whole record — the core bandwidth win.
3. **Persistent GPU scene buffer.** Owned by `graphics/renderer`, retained across frames, sized to a max-instances policy, with a recorded resize rule (grow-and-relocate at a capacity threshold). Rationale: a persistent buffer is the precondition for in-place updates (the whole point of deltas), and graphics ownership keeps the GPU lifetime inside the consuming layer; a recorded grow-and-relocate rule bounds resize churn.
4. **Apply-deltas pass.** A compute pass at frame start reads the delta batch and writes the persistent buffer in place, with the dispatch shape and atomic-write rules recorded. Rationale: applying deltas on the GPU (one thread per delta record) keeps the CPU off the per-instance write path and bounds apply cost to O(changed records); recorded atomic-write rules prevent two updates to the same instance from racing.
5. **Compaction.** Removed instances leave holes reclaimed by periodic compaction, with a recorded compaction trigger heuristic and relocation reporting per `GRAPHICS-005`. Rationale: in-place removal must not fragment the buffer unboundedly, so periodic compaction reclaims holes; reusing the `GRAPHICS-005` relocation-reporting contract means downstream draw-bucket references are updated through the existing mechanism rather than a new one.
6. **Pipelined-frame interaction.** With a double-buffered render world (`GRAPHICS-036`), each buffer carries the delta queue *since* the previous reclaimed snapshot, with the synchronization rule recorded. Rationale: under sim-N / render-N-1 pipelining each in-flight buffer must hold a self-consistent delta set relative to its own baseline, so anchoring deltas to the last reclaimed snapshot prevents applying a delta twice or skipping one across the swap.
7. **Full-extract fallback.** A boolean recipe flag forces full re-extraction (for tests, benchmarks, scene reset), with the rule recorded. Rationale: a forced full extract is the ground truth the delta path is validated against and the clean recovery for scene reset, so keeping it a first-class recipe flag preserves the unconditional-default full path and a deterministic reset.
8. **Determinism.** Apply-deltas produces a buffer state identical to a from-scratch extract, validated by a recorded equivalence test fixture. Rationale: bitwise full-vs-delta equivalence is the correctness contract that lets deltas be trusted — without it, a missed or double-applied delta would silently diverge the GPU scene from the authoritative ECS state.
9. **Diagnostics.** `DeltaRecordsPerFrameHistogram[Add/Remove/Update]`, `SceneCompactionFrames`, and `FullExtractFallbackCount` are atomic counters. Rationale: the per-kind histogram surfaces change volume (and pathological full-rebuild-via-deltas), compaction frames surface fragmentation pressure, and the fallback counter surfaces how often the delta path bails to full extract — the signals needed to tune the path.
10. **Test split.** `contract;runtime` for delta-record correctness; `contract;graphics` for the apply-deltas pass under null RHI; `integration` for end-to-end equivalence with the full-extract baseline. Rationale: delta computation is a runtime concern, apply is a graphics concern, and equivalence is cross-layer — splitting the tests by owner keeps each layer's contract independently verifiable on the default CPU gate.
11. **Layering.** Runtime computes deltas, graphics applies them, no live ECS access, and the snapshot-extraction boundary is preserved. Rationale: preserves AGENTS.md §2/§4 — only the *shape* of the snapshot changes (full vs delta), not the boundary, so graphics still never touches live ECS and runtime remains the sole producer.
12. **Performance bound.** Delta-apply CPU cost is bounded by O(changed records) and GPU dispatch cost by O(changed records), recorded in benchmarks. Rationale: O(changed) on both CPU and GPU is the scaling property that motivates the whole task (vs. O(total instances) for full extract), so pinning it as a benchmarked bound makes any regression to O(total) detectable.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-052-Impl-A** — Delta record shape + field-mask encoding + `contract;runtime` shape tests.
- **GRAPHICS-052-Impl-B** — Persistent GPU scene buffer + apply-deltas pass + null-RHI shape tests.
- **GRAPHICS-052-Impl-C** — Compaction + relocation reporting integration with `GRAPHICS-005`.
- **GRAPHICS-052-Impl-D** — End-to-end full-vs-delta equivalence integration tests + recipe selection.
- **GRAPHICS-052-Impl-E** — Pipelined-frame interaction wiring (gated by `GRAPHICS-036`).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The snapshot-extraction (delta-shape) section of `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-052-Impl-A/B/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The extraction section of `src/runtime/README.md` is deferred to the same implementation children for the same reason.
- [x] The scene-buffer section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Snapshot-extraction boundary is preserved.
- [x] Full-extract path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve deltaful-scene decisions are recorded with explicit answers and trade-off rationales: the typed add/remove/update delta records mirroring the full-record layout, the per-update field mask, the renderer-owned persistent grow-and-relocate scene buffer, the GPU apply-deltas pass with atomic-write rules, periodic compaction reusing `GRAPHICS-005` relocation reporting, the pipelined-frame delta-queue-since-last-snapshot synchronization, the forced full-extract recipe-flag fallback, the bitwise full-vs-delta determinism contract, the three atomic delta counters, the per-owner `contract;runtime`/`contract;graphics`/`integration` test split, the runtime-computes / graphics-applies boundary-preserving layering, and the O(changed-records) CPU+GPU performance bound. Implementation children `GRAPHICS-052-Impl-A..E` are identified but not opened; the full-extraction path stays the unconditional default, the snapshot-extraction boundary is preserved, and no extraction-side code changes land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No live ECS access from graphics.
- No removal of full-extract path.
- No bypass of snapshot-extraction boundary.
- No mixing of mechanical file moves with semantic refactors.
