# DOCS-001 — Reduce `docs/architecture/graphics.md` to contract + status

## Status

- Status: done.
- Completed: 2026-05-17.
- Retirement: this commit (moves the file from `tasks/active/` to `tasks/done/` and rewrites the `tasks/active/DOCS-001-...` references in all 15 new ADR files + `docs/architecture/index.md` to point at the `tasks/done/` path).
- Final `docs/architecture/graphics.md` line count: 118 (target was ≤ 250; reduced from 793 over slice 1 (table only) + slice 2 (15 ADR extractions) + slice 4 (final tightening + Pointers)).
- All slices landed:
  - Slice 1: Classification table commit `70e4612` on `claude/setup-agentic-workflow-6h6w4`.
  - Slice 2 (15 commits, one per ADR): ADR-0004 commit `42b966f` (prior branch); ADR-0005 commit `2415e13`; ADR-0006 commit `bfe0652`; ADR-0007 commit `d38cc45`; ADR-0008 commit `feeecae`; ADR-0009 commit `95edfd3`; ADR-0010 commit `d60adbc`; ADR-0011 commit `bdf82c9`; ADR-0012 commit `d674b08`; ADR-0013 commit `a569767`; ADR-0014 commit `0dc15cd`; ADR-0015 commit `f843152`; ADR-0016 commit `d17c2ab`; ADR-0017 commit `20c67f4`; ADR-0018 commit `23cd5b7`.
  - Slice 3: no-op (the only migration-inventory row in the Classification table was cross-linked to the existing `docs/migration/nonlegacy-parity-matrix.md` by ADR-0006 with no new migration doc).
  - Slice 4: final tightening + Pointers section commit `c88da85`.
- Owner/agent: Claude on `claude/finish-active-tasks-Tx4Br` (handed off from `claude/setup-agentic-workflow-6h6w4`).
- Branch: `claude/finish-active-tasks-Tx4Br`.
- Started: 2026-05-17.
- Slice 1 landed in commit `70e4612` (Classification subsection appended to Context).
- Slice 2 landed so far:
  - ADR-0004 (Vulkan backend bring-up + fail-closed fallback) — replaces the line-31 mega-paragraph of `docs/architecture/graphics.md` with a one-line pointer. Cross-links retired `GRAPHICS-018`/`018Q`/`018R`/`018T`/`026`.
  - ADR-0005 (Vulkan operational readiness gate and runtime reconciliation) — replaces the lines 33–65 `## Vulkan operational readiness and runtime fallback` section with a one-line pointer. Cross-links retired `GRAPHICS-033`/`033A`/`033B`/`033C`/`033E`/`033F` and the active `GRAPHICS-033D` smoke as the canonical end-to-end validator.
  - ADR-0006 (Camera, picking-request, and gizmo runtime handoff) — replaces the lines 85–149 GRAPHICS-017Q clarification paragraph embedded in the `Extrinsic.Graphics.CameraSnapshots` bullet with a one-line pointer. Cross-links retired `GRAPHICS-017`/`017Q` and the migration parity matrix (no new migration doc; the handoff inventory already lives there).
  - ADR-0007 (Picking, selection, and outline reporting seam) — replaces the lines 150–175 `Extrinsic.Graphics.SelectionSystem` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-012`/`012Q`. ADR-0006's forward note to ADR-0007 is upgraded to a real cross-link in this commit.
  - ADR-0008 (Spatial debug visualizer runtime adapters) — replaces the lines 176–200 `Extrinsic.Graphics.SpatialDebugVisualizers` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-011`/`011Q`.
  - ADR-0009 (Visualization packets, validation, and overlay upload) — replaces the lines 201–257 `Extrinsic.Graphics.VisualizationPackets` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-014`/`014Q`/`010Q`.
  - ADR-0010 (Postprocess chain backend policy) — replaces the lines 265–298 `Extrinsic.Graphics.PostProcessSystem` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-013A`/`013AQ`.
  - ADR-0011 (Debug-view inspection table and visualization mode mapping) — replaces the lines 299–336 `Extrinsic.Graphics.DebugViewSystem` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-013B`/`013BQ`.
  - ADR-0012 (ImGui overlay submission and `Pass.Present` finalization) — replaces the lines 337–393 `ImGuiOverlaySystem`/`Pass.Present` bullet with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-013C`/`013CQ`. ADR-0009's forward note to ADR-0012 is upgraded to a real cross-link in this commit.
  - ADR-0013 (ECS renderable residency bridge) — replaces the lines 406–465 `## ECS renderable residency bridge` section with a 2-sentence canonical summary plus a one-line pointer. Cross-links retired `GRAPHICS-028`/`023A`/`023B`/`023C`/`023D`.
  - ADR-0014 (Procedural-source residency bridge) — replaces the lines 467–534 `## Procedural-source residency bridge` section with a one-line pointer. Cross-links retired `GRAPHICS-030`/`030A`/`030B`/`030C`. ADR-0013's forward note to ADR-0014 is upgraded to a real cross-link in this commit.
  - ADR-0015 (Runtime reference scene bootstrap) — replaces the lines 536–594 `## Reference scene bootstrap` section with a one-line pointer. Cross-links retired `GRAPHICS-029`/`029A`/`029B` and the active `GRAPHICS-080` for the `Render.EnablePromotedVulkanDevice` flip.
  - ADR-0016 (Texture residency, fallback, and asset cache policy) — replaces the lines 619–701 GRAPHICS-015Q clarification paragraph inside the `## Graphics asset residency` section with a one-line pointer. Cross-links retired `GRAPHICS-015`/`015Q`.
  - ADR-0017 (Default debug surface material, slot 0) — replaces the lines 727–746 GRAPHICS-031 paragraph in the `## Material registry and slot contract` section with a one-line pointer + a short canonical summary of the slot-0 material identity. Cross-links retired `GRAPHICS-031`/`031A`.
  - ADR-0018 (Missing-material fallback substitution and diagnostics) — replaces the lines 747–764 GRAPHICS-031 substitution paragraph with a one-line pointer. Cross-links retired `GRAPHICS-031`/`031B`. Kept distinct from ADR-0017 per this task's `## Forbidden changes` rule (one decision per ADR).
- Slice 2 per-ADR extraction is complete (15 ADRs: 0004 through 0018, one per decision-record-classified Classification-table row). Slice 3 (migration-inventory extraction) is a no-op because the only migration-inventory row in the Classification table is row 85–149 which was already cross-linked to the existing `docs/migration/nonlegacy-parity-matrix.md` by ADR-0006 with no new migration doc required. Next verification step: prepare slice 4 final tightening — graphics.md is already well under the ≤250-line acceptance target; slice 4 adds the "Pointers" section at the bottom enumerating every extracted ADR + any migration docs and tightens any remaining bullets to one-sentence-per-bullet form. Each remaining commit runs the three static verification gates (`tools/agents/check_task_policy.py --strict`, `tools/docs/check_doc_links.py`, `tools/agents/validate_tasks.py --strict`). Build/CTest verification deferred to CI because the pinned `clang-20` toolchain is unavailable on this host.

## Goal
- [ ] Reshape [`docs/architecture/graphics.md`](../../docs/architecture/graphics.md) (currently ~793 lines, much of it multi-paragraph prose embedded in single bullet items) into a short canonical contract that a contributor can read in five minutes, plus a short pointer list to the deeper material it currently inlines.
- [ ] Move the inlined narrative (Vulkan operational gate decisions, GRAPHICS-017Q/018Q/032/033/etc. clarification prose, runtime/editor handoff inventories) out of the canonical architecture doc and into:
  - ADRs under [`docs/adr/`](../../docs/adr/) for irreversible architecture decisions; or
  - migration notes under [`docs/migration/`](../../docs/migration/) for time-bounded transition state; or
  - the originating tasks under `tasks/done/` (where the prose was authored as part of the slice).
- [ ] Leave `docs/architecture/graphics.md` factual about *current* state of canonical graphics layers, per `AGENTS.md` §9 doc sync policy.

## Non-goals
- [ ] Do not change `src/graphics/*` source.
- [ ] Do not introduce new architecture invariants. `AGENTS.md` remains authoritative.
- [ ] Do not delete the inlined prose; relocate it. Information preservation matters because parts of the prose record decisions that are not captured anywhere else.
- [ ] Do not reduce `docs/architecture/graphics.md` to a stub. The canonical contract content (sublayer split, dependency rules, frame lifecycle outline) must remain.
- [ ] Do not perform the same reduction on other architecture docs in this task. If `docs/architecture/runtime.md` (19 lines), `docs/architecture/geometry.md` (77 lines), or `docs/architecture/ecs.md` (14 lines) need work, that is a separate task.

## Context
- Owner/layer: docs only (`docs/architecture/`, `docs/adr/`, `docs/migration/`).
- Current state (2026-05-16):
  - `docs/architecture/graphics.md` is 793 lines.
  - Many of its bullet items are 30–80-line single paragraphs with embedded enumerations of "follow-up clarifications" (e.g. one bullet under `## GPU scene ownership` covers GRAPHICS-017Q camera-controller decisions, gizmo hit testing ownership, editor handoff, modifier-key behavior, transform application ownership, undo policy, and legacy `Graphics.TransformGizmo` parity in a single ~150-line paragraph).
  - The document mixes three kinds of content that should live in different places:
    1. **Canonical contract** (sublayer dependency rules, runtime/RHI frame lifecycle, render-graph ownership).
    2. **Decision records** (GRAPHICS-033 nine-step operational gate, validation-layer policy, fallback-reason taxonomy, sampler-anisotropy policy — each is effectively an ADR embedded in prose form).
    3. **Migration/handoff inventories** (GRAPHICS-017Q camera/gizmo handoff details, GRAPHICS-018Q/033/etc. clarification follow-ups, runtime/editor handoff matrices that duplicate what `docs/migration/nonlegacy-parity-matrix.md` already records).
- Symptoms of this drift:
  - The document is hard to read end-to-end and almost impossible to keep current.
  - Architectural contracts and ephemeral decisions are not visually distinguishable.
  - Cross-references from canonical sources point at run-on paragraphs that are likely to rot.
- Reference for target shape: [`docs/architecture/overview.md`](../../docs/architecture/overview.md) (22 lines) and [`docs/architecture/layering.md`](../../docs/architecture/layering.md) (~41 lines) demonstrate the intended concision for canonical architecture docs.
- ADR template: [`docs/adr/`](../../docs/adr/) already has 3 records (0001..0003). Each new extracted decision becomes an ADR (`0004`, `0005`, ...).
- Migration target: [`docs/migration/`](../../docs/migration/) already hosts the legacy retirement and parity matrix documents; extracted handoff inventories live alongside them.

### Classification

This subsection is the slice-1 deliverable. It records the end-to-end pass over [`docs/architecture/graphics.md`](../../docs/architecture/graphics.md) at the section/bullet granularity established by the document's top-level `##` headings and `^- ` bullet boundaries (see `grep -nE "^##|^- " docs/architecture/graphics.md` for the raw list). The intent is that slices 2–4 can mechanically consume this table without re-reading the source document: each `decision-record` row becomes one ADR commit (slice 2), each `migration-inventory` row becomes one migration-doc commit (slice 3), and the remaining `canonical-contract` rows are tightened in place (slice 4).

Conventions used in the table:

- **Line range** uses the file's current line numbers (snapshot 2026-05-17, file 793 lines). Long single bullets that mix canonical contract with embedded "Per `GRAPHICS-XXX-Q`, …" clarification prose are split into two rows: the canonical lead-in and the embedded clarification paragraph.
- **Classification** is one of `canonical-contract`, `decision-record`, `migration-inventory`, `obsolete`. No row is classified `obsolete`; everything currently in the doc is information-bearing.
- **Target destination** uses the formats specified in slice 1: `graphics.md (keep)` | `docs/adr/NNNN-*.md` | `docs/migration/<name>.md` | `tasks/done/<id>.md` cross-link. ADR numbering picks up at `0004` after the three existing records (`0001..0003`).
- Several embedded `GRAPHICS-XXX-Q` clarification paragraphs already have an authoritative home in `tasks/done/GRAPHICS-XXX-Q-*.md`. Slice 2's ADRs therefore cite those task files as the originating record and capture the same policy in ADR form; the table records both the new ADR and the cross-link so slice 2 can choose whether the ADR body is a fresh write-up or a curated copy of the retired task's "Required changes" section.
- Where a single section bullet sub-divides into multiple decisions that share one topical seam (for example `## Graphics asset residency` lines 619–701 is one bullet that covers cache capacity, streaming mips, fallback texture, bindless flush cadence, and runtime upload scheduling), the row consolidates into one ADR target rather than fragmenting into five. Slice 2 may further split if any single ADR exceeds ~150 lines, but the default is one bullet → one ADR.

| Line range | Section / bullet | Classification | Target destination |
| --- | --- | --- | --- |
| 1–9 | `# Graphics Architecture` title + sublayer intro (4 bullets at 5–8) | canonical-contract | `graphics.md` (keep) |
| 10–18 | `## Rules` (eight policy bullets at 12–18) | canonical-contract | `graphics.md` (keep) |
| 20–30 | `## Renderer/RHI frame lifecycle` (bullets at 22–30: `BeginFrame` / `ExecuteFrame` / culling soft-fail / `DepthPrepass` ordering / `RenderGraphFrameStats` split / `EndFrame` / `RebuildOperationalResources` seam / `RHI::SamplerDesc`) | canonical-contract | `graphics.md` (keep; tighten to one sentence per bullet in slice 4) |
| 31 | `## Renderer/RHI frame lifecycle` mega-paragraph (Vulkan backend bring-up: volk/instance/surface/swapchain/feature enablement, `IsOperational()` predicate, bootstrap/service/frame-lifecycle/pipeline diagnostics snapshots, fail-closed `BeginFrame`/`EndFrame`/`Present`/`Resize` taxonomy, rate-limited breadcrumbs, `GRAPHICS-018Q` texture-upload policy + sampler anisotropy + `FallbackPipelineReason` extensibility + per-call breadcrumb policy, `GRAPHICS-018T` multi-mip/multi-layer/cubemap batching) | decision-record | `docs/adr/0004-vulkan-backend-bringup-and-fallback.md`; cross-links `tasks/done/GRAPHICS-018-vulkan-renderer-integration.md`, `tasks/done/GRAPHICS-018Q-vulkan-integration-clarifications.md`, `tasks/done/GRAPHICS-018T-texture-upload-batching.md`, `tasks/done/GRAPHICS-026-*.md` (transfer queue seam). Replace bullet with a 1-sentence pointer in `graphics.md`. |
| 33–65 | `## Vulkan operational readiness and runtime fallback` (GRAPHICS-033 nine-step ordered gate + `EvaluateVulkanOperationalStatus(...)` evaluator surface + `VulkanOperationalStatusCode`/`Reason` enums + runtime reconciliation truth table + `VulkanRequestedButNotOperational` breadcrumb) | decision-record | `docs/adr/0005-vulkan-operational-readiness-gate.md`; cross-links `tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`, `tasks/done/GRAPHICS-033A..F-*.md`. Replace section with a 1-sentence pointer in `graphics.md`. |
| 67–70 | `## GPU scene ownership` opening (Runtime composes / Graphics receives) | canonical-contract | `graphics.md` (keep) |
| 71–78 | `## GPU scene ownership` — `RenderWorld` immutable span contract | canonical-contract | `graphics.md` (keep) |
| 79–84 | `## GPU scene ownership` — `Extrinsic.Graphics.CameraSnapshots` canonical contract (matrix validation, frustum extraction, pick-ray derivation, `Core::Extent2D` viewport carrier) | canonical-contract | `graphics.md` (keep) |
| 85–149 | `## GPU scene ownership` — CameraSnapshots GRAPHICS-017Q follow-ups paragraph (runtime camera controllers under `Extrinsic.Runtime.CameraControllers` umbrella, pick-request scheduling drain pattern, transform-gizmo hit testing under `Extrinsic.Runtime.GizmoInteraction`, interaction state storage, transform application + undo policy, legacy `Graphics.TransformGizmo`/`Graphics.Interaction` parity cross-reference) | decision-record + migration-inventory | `docs/adr/0006-camera-picking-and-gizmo-runtime-handoff.md` (decision body) + handoff matrix already lives in `docs/migration/nonlegacy-parity-matrix.md` (cross-link only, no new migration doc). Cross-link `tasks/done/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md`. Replace paragraph with a 1-sentence pointer in `graphics.md`. |
| 150–175 | `## GPU scene ownership` — `Extrinsic.Graphics.SelectionSystem` reporting-only pick seam + `EncodedSelectionId` packing + Picking.Readback drain + GRAPHICS-012Q runtime/backend ownership | decision-record | `docs/adr/0007-picking-selection-and-outline.md`; cross-links `tasks/done/GRAPHICS-012-picking-selection-outline.md`, `tasks/done/GRAPHICS-012Q-picking-backend-runtime-clarifications.md`. Keep the canonical 2–3-sentence summary of the reporting seam in `graphics.md`. |
| 176–200 | `## GPU scene ownership` — `Extrinsic.Graphics.SpatialDebugVisualizers` + GRAPHICS-011Q adapter ownership (`Extrinsic.Runtime.SpatialDebugAdapters` umbrella, pre-filter policy, diagnostics handoff, adapter test placement) | decision-record | `docs/adr/0008-spatial-debug-visualizer-adapters.md`; cross-links `tasks/done/GRAPHICS-011-spatial-debug-visualizers.md`, `tasks/done/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md`. Keep a 2-sentence canonical summary in `graphics.md`. |
| 201–257 | `## GPU scene ownership` — `Extrinsic.Graphics.VisualizationPackets` + GRAPHICS-014Q clarifications (runtime adapter umbrella `Extrinsic.Runtime.VisualizationAdapters`, `ValidateVisualizationPackets(...)` centralized validation, overlay upload through backend-local helper, UV vs Htex bake selection policy) + GRAPHICS-010Q two-pipeline-variant policy citation | decision-record | `docs/adr/0009-visualization-packets-and-overlay-upload.md`; cross-links `tasks/done/GRAPHICS-014-visualization-attributes-overlays.md`, `tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md`, `tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md`. Keep a 2-sentence canonical summary in `graphics.md`. |
| 258–264 | `## GPU scene ownership` — `Extrinsic.Graphics.ColormapSystem` LUT residency and bindless readiness | canonical-contract | `graphics.md` (keep) |
| 265–298 | `## GPU scene ownership` — `Extrinsic.Graphics.PostProcessSystem` HDR-to-LDR chain + GRAPHICS-013AQ backend clarifications (SMAA `AreaTex`/`SearchTex` retention, single-mip-chain bloom scratch with per-mip subviews, fixed 256-bin histogram + drain pattern, FXAA/SMAA mutual exclusion, quality-preset encoding) | decision-record | `docs/adr/0010-postprocess-chain-backend-policy.md`; cross-links `tasks/done/GRAPHICS-013A-postprocess-chain.md`, `tasks/done/GRAPHICS-013AQ-postprocess-backend-clarifications.md`. Keep a 2-sentence canonical summary in `graphics.md`. |
| 299–336 | `## GPU scene ownership` — `Extrinsic.Graphics.DebugViewSystem` + GRAPHICS-013BQ clarifications (visualization-mode derivation, `Pass.DebugView` descriptor binding, runtime/editor-owned UI-name dictionary, buffer-class non-previewability) | decision-record | `docs/adr/0011-debug-view-inspection-table.md`; cross-links `tasks/done/GRAPHICS-013B-debug-view-and-render-target-inspection.md`, `tasks/done/GRAPHICS-013BQ-debug-view-backend-clarifications.md`. Keep a 2-sentence canonical summary in `graphics.md`. |
| 337–393 | `## GPU scene ownership` — `Extrinsic.Graphics.ImGuiOverlaySystem` + `Pass.Present` + GRAPHICS-013CQ clarifications (`ImDrawData` → `ImGuiOverlayFrame` runtime translation, transient host-visible overlay buffers, graphics-owned retained font atlas, bindless user textures, fullscreen-triangle present finalization, platform/backend/runtime/graphics boundaries) | decision-record | `docs/adr/0012-imgui-overlay-and-present-finalization.md`; cross-links `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`. Keep a 3-sentence canonical summary in `graphics.md`. |
| 394 | `## GPU scene ownership` — canonical instance-slot space bullet | canonical-contract | `graphics.md` (keep) |
| 395–403 | `## GPU scene ownership` — `GpuWorld` managed vertex/index buffer ranges + opt-in `PlanManagedBufferCompaction()` / `ApplyManagedBufferCompaction()` + relocation table (GRAPHICS-004/005 contract) | canonical-contract | `graphics.md` (keep) |
| 404 | `## GPU scene ownership` — heavy CPU scene data ownership invariant | canonical-contract | `graphics.md` (keep) |
| 406–465 | `## ECS renderable residency bridge` (GRAPHICS-028 planning summary + GRAPHICS-023A/B/C/D observation/acknowledgment loop + static-vs-dynamic stream split + dirty-tag CPU-only invariant + hierarchy/primitive policy) | decision-record | `docs/adr/0013-ecs-renderable-residency-bridge.md`; cross-links `tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md`, `tasks/done/GRAPHICS-023A-*.md`, `tasks/done/GRAPHICS-023B-*.md`, `tasks/done/GRAPHICS-023C-*.md`, `tasks/done/GRAPHICS-023D-*.md`. Replace section with a 2-sentence pointer in `graphics.md`. |
| 467–534 | `## Procedural-source residency bridge` (GRAPHICS-030 planning summary: closed `ProceduralGeometryKind` enum, `ProceduralGeometryKey`, `Runtime::ProceduralGeometryCache`, packer placement, lifecycle ordering, failure modes + counters, extensibility) | decision-record | `docs/adr/0014-procedural-source-residency-bridge.md`; cross-links `tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`, `tasks/done/GRAPHICS-030A..C-*.md`. Replace section with a 1-sentence pointer in `graphics.md`. |
| 536–594 | `## Reference scene bootstrap` (GRAPHICS-029 planning summary: `Extrinsic.Runtime.ReferenceScene`, `IReferenceSceneProvider` + `ReferenceSceneRegistry`, `TriangleProvider`, camera authorship, `EngineConfig::ReferenceScene` defaults, GRAPHICS-080 cross-reference for promoted Vulkan flip) | decision-record | `docs/adr/0015-reference-scene-bootstrap.md`; cross-links `tasks/done/GRAPHICS-029-runtime-reference-scene-bootstrap.md`, `tasks/done/GRAPHICS-029A-*.md`, `tasks/done/GRAPHICS-029B-*.md`, `tasks/active/GRAPHICS-080-enable-promoted-vulkan-by-default.md` (still active). Replace section with a 1-sentence pointer in `graphics.md`. |
| 596–617 | `## Graphics asset residency` — `Graphics.GpuAssetCache` canonical contract (5 bullets: cache surface, `GpuTextureRequest`, fallback resolution, non-evicting policy, `GpuAssetCacheDiagnostics`) | canonical-contract | `graphics.md` (keep) |
| 619–701 | `## Graphics asset residency` — GRAPHICS-015Q clarification paragraph (cache capacity / future eviction policy, streaming mip reupload via `RHI::TextureManager::Reupload()`, single magenta-checker fallback + per-channel neutrality, bindless descriptor flush cadence + `BindlessDescriptorRewrites`, sampler manager dedup, runtime upload scheduling + `Extrinsic.Runtime.AssetBridges.Texture` umbrella) | decision-record | `docs/adr/0016-texture-residency-and-asset-cache-policy.md`; cross-links `tasks/done/GRAPHICS-015-gpu-assets-textures-residency.md`, `tasks/done/GRAPHICS-015Q-texture-residency-backend-clarifications.md`. Replace paragraph with a 1-sentence pointer in `graphics.md`. |
| 703–716 | `## Pipeline and shader registry contract` (3 bullets: `Extrinsic.RHI.PipelineRegistry`, shader reload invalidation, missing-shader diagnostics) | canonical-contract | `graphics.md` (keep) |
| 718–726 | `## Material registry and slot contract` — canonical opening (MaterialSystem ownership + slot 0 fallback immutability + stale-handle resolution) | canonical-contract | `graphics.md` (keep) |
| 727–746 | `## Material registry and slot contract` — GRAPHICS-031 default-debug-surface material details (slot 0 registration as `"Material.DefaultDebugSurface"`, shader pair location, vertex format, descriptor/push-constant reuse, pipeline state, dynamic state, cull bucket reuse) | decision-record | `docs/adr/0017-default-debug-surface-material.md`; cross-links `tasks/done/GRAPHICS-031-default-debug-surface-material.md` (parent planning) and `tasks/done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md` (shader pair + pipeline implementation). Replace paragraph with a 1-sentence pointer in `graphics.md`. ADR-0018 owns the substitution/diagnostics half of GRAPHICS-031 (lines 747–764) and is where `GRAPHICS-031B` is cross-linked. |
| 747–764 | `## Material registry and slot contract` — GRAPHICS-031 missing-material fallback substitution policy (three additive `MaterialSystemDiagnostics` counters at renderer span-copy step, no silent-skip, runtime-agnostic) | decision-record | `docs/adr/0018-missing-material-fallback-substitution.md`; cross-link `tasks/done/GRAPHICS-031-default-debug-surface-material.md`, `tasks/done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md`. Replace paragraph with a 1-sentence pointer in `graphics.md`. Kept distinct from ADR-0017 because the substitution policy (when/where slot 0 replaces a snapshot record's resolved slot, which counters increment, and that there are no silent-skip paths) is a separately traceable decision from the material definition itself (slot 0 registration, shader pair, pipeline state); per the task's `## Forbidden changes` rule, multiple decisions must not be folded into a single ADR. |
| 765–770 | `## Material registry and slot contract` — debug-variant family extensibility (`Material.DefaultDebug<Variant>` naming) | canonical-contract | `graphics.md` (keep one-sentence pointer to the naming family) |
| 771–775 | `## Material registry and slot contract` — canonical material SSBO layout (`kMaterialLayoutVersion == 1`, `GetCanonicalMaterialLayoutContract()`, 128-byte slot, 4 custom `vec4`, 4 texture refs) | canonical-contract | `graphics.md` (keep) |
| 776–783 | `## Material registry and slot contract` — texture references + `MaterialTextureAssetBindings` + `ResolveTextureAssetBindings` (canonical seam) | canonical-contract | `graphics.md` (keep) |
| 784–788 | `## Material registry and slot contract` — material type registration rejection + dirty material coalescing + CPU-testable diagnostics | canonical-contract | `graphics.md` (keep) |
| 790–793 | `## Related references` (frame-graph pointer + historical migration docs) | canonical-contract | `graphics.md` (keep) |

Row totals: 33 rows. By classification: `canonical-contract` = 18, `decision-record` = 14, `decision-record + migration-inventory` = 1, `migration-inventory` (standalone) = 0, `obsolete` = 0. ADR roster proposed by slice 1: `0004..0018` (15 ADRs, one per decision-record-classified row including the mixed row; no consolidation, per the task's `## Forbidden changes` rule that one decision-record row maps to one ADR).

Slice-budget sanity check: extracting 14 pure decision-record rows + 1 mixed row leaves 18 canonical-contract rows in `graphics.md`. Each row contributes between 1 and 9 lines of canonical prose (the largest are 20–30 `## Renderer/RHI frame lifecycle` and 596–617 `## Graphics asset residency`; both can compress to ≤ 10 lines under slice 4's one-sentence-per-bullet rule). Final shape is therefore inside the ≤ 250-line acceptance criterion with margin for the section headings, the new "Pointers" section at the bottom, and the front-matter intro.

## Required changes

### Slice 1 — inventory and classify (no edits to `graphics.md` yet)
- [x] Read `docs/architecture/graphics.md` end-to-end and classify each section/bullet as one of:
      `canonical-contract` | `decision-record` | `migration-inventory` | `obsolete`.
- [x] Record the classification as a table in this task file under a new "Classification" subsection appended to the Context section above. The table columns are: line-range, current heading, classification, target destination (`graphics.md (keep)` | `docs/adr/NNNN-*.md` | `docs/migration/<name>.md` | `tasks/done/<id>.md` cross-link).
- [x] Slice-1 commit is doc-only and changes only this task file. (Landed in commit `70e4612`.)

### Slice 2 — extract decision records to ADRs
- [x] For each row classified `decision-record`, author an ADR under `docs/adr/` (`0004-*`, `0005-*`, ...). Use the existing ADR pattern. The ADR captures the decision and its rationale; the original prose becomes the ADR's body.
- [x] Update `docs/adr/index.md` to list the new records.
- [x] Update `docs/architecture/graphics.md` to replace each extracted block with a one-line pointer (`See [ADR-NNNN](../adr/NNNN-*.md).`).
- [x] Slice-2 commit per ADR (each ADR + its `graphics.md` pointer update is a single commit).

Slice-2 progress (per Classification-table row, top-to-bottom):

- [x] ADR-0004 — Vulkan backend bring-up and fail-closed fallback (Classification row 31). Replaces graphics.md line 31 with a one-line pointer; index updated.
- [x] ADR-0005 — Vulkan operational readiness gate (Classification row 33–65). Replaces the `## Vulkan operational readiness and runtime fallback` section with a one-line pointer; index updated.
- [x] ADR-0006 — Camera, picking, and gizmo runtime handoff (Classification row 85–149). Replaces the GRAPHICS-017Q clarification paragraph embedded in the `Extrinsic.Graphics.CameraSnapshots` bullet with a one-line pointer; index updated. Migration-inventory portion stays cross-linked to `docs/migration/nonlegacy-parity-matrix.md` (no new migration doc).
- [x] ADR-0007 — Picking, selection, and outline (Classification row 150–175). Replaces the `Extrinsic.Graphics.SelectionSystem` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated. ADR-0006 cross-link upgraded from text-only to real link.
- [x] ADR-0008 — Spatial debug visualizer adapters (Classification row 176–200). Replaces the `Extrinsic.Graphics.SpatialDebugVisualizers` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated.
- [x] ADR-0009 — Visualization packets and overlay upload (Classification row 201–257). Replaces the `Extrinsic.Graphics.VisualizationPackets` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated.
- [x] ADR-0010 — Postprocess chain backend policy (Classification row 265–298). Replaces the `Extrinsic.Graphics.PostProcessSystem` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated.
- [x] ADR-0011 — Debug-view inspection table (Classification row 299–336). Replaces the `Extrinsic.Graphics.DebugViewSystem` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated.
- [x] ADR-0012 — ImGui overlay and Present finalization (Classification row 337–393). Replaces the `ImGuiOverlaySystem`/`Pass.Present` bullet with a 2-sentence canonical summary plus a one-line pointer; index updated. ADR-0009 cross-link upgraded from text-only to real link.
- [x] ADR-0013 — ECS renderable residency bridge (Classification row 406–465). Replaces the `## ECS renderable residency bridge` section with a 2-sentence canonical summary plus a one-line pointer; index updated.
- [x] ADR-0014 — Procedural-source residency bridge (Classification row 467–534). Replaces the `## Procedural-source residency bridge` section with a one-line pointer; index updated. ADR-0013 cross-link upgraded from text-only to real link.
- [x] ADR-0015 — Reference scene bootstrap (Classification row 536–594). Replaces the `## Reference scene bootstrap` section with a one-line pointer; index updated.
- [x] ADR-0016 — Texture residency and asset cache policy (Classification row 619–701). Replaces the GRAPHICS-015Q clarification paragraph in the `## Graphics asset residency` section with a one-line pointer; index updated.
- [x] ADR-0017 — Default debug surface material definition (Classification row 727–746). Replaces the GRAPHICS-031 slot-0 paragraph with a one-line pointer + short canonical summary; index updated.
- [x] ADR-0018 — Missing-material fallback substitution policy (Classification row 747–764). Replaces the GRAPHICS-031 substitution paragraph with a one-line pointer; index updated. Kept distinct from ADR-0017 per `## Forbidden changes` rule.

### Slice 3 — extract migration inventories
- [x] No migration-inventory rows in the Classification table require a new migration doc. The only `migration-inventory`-classified row (85–149, the GRAPHICS-017Q camera/gizmo handoff inventory) was authored in row 85–149 of the slice-1 Classification table to live in `docs/migration/nonlegacy-parity-matrix.md` via cross-link rather than a new file. ADR-0006 carries that cross-link; no new migration doc is added and `docs/migration/index.md` is unchanged.

### Slice 4 — final tightening
- [x] After slices 1–3, `docs/architecture/graphics.md` should fit on roughly two screens of reading. Tighten any remaining prose to one-sentence-per-bullet form. Keep the canonical sublayer split, the dependency rules, the frame lifecycle outline, the renderer/RHI seam, and the GPU scene ownership contract.
- [x] Add a "Pointers" section at the bottom listing every ADR and migration doc extracted in slices 2 and 3.
- [x] Slice-4 commit is the final `graphics.md` tightening.

## Completion metadata
- Completion date: 2026-05-17.
- Commit reference: pending current workspace/PR (retirement commit on branch `claude/finish-active-tasks-Tx4Br`; see Status section for the per-slice commit SHAs).
- Follow-up: none.

## Tests
- [x] No code is produced by this task. No automated tests.
- [x] Each slice must pass `python3 tools/docs/check_doc_links.py --root .` (no broken relative links).
- [x] Each slice must pass `python3 tools/agents/check_task_policy.py --root . --strict`.
- [x] Final `graphics.md` line count target: ≤ 250 lines. (Current: see Status; well under the target after slice 4 tightening.)

## Docs
- [x] [`docs/architecture/graphics.md`](../../docs/architecture/graphics.md) reduced to canonical contract + pointers.
- [x] [`docs/adr/index.md`](../../docs/adr/index.md) lists each new ADR (`0004..0018`).
- [x] [`docs/migration/index.md`](../../docs/migration/index.md) lists any new migration docs. (No new migration docs; the only migration-inventory row was cross-linked to the existing `nonlegacy-parity-matrix.md` by ADR-0006.)
- [x] [`docs/architecture/index.md`](../../docs/architecture/index.md) status note for `graphics.md` reflects the reduction (slice-4 commit).

## Acceptance criteria
- [x] Final `docs/architecture/graphics.md` is ≤ 250 lines.
- [x] No prose paragraph in the final `graphics.md` exceeds 5 lines (single-paragraph clarifications relocate to ADRs/migration docs).
- [x] Every relocated decision is captured in either an ADR (decision records), a migration doc (handoff inventories), or a `tasks/done/` cross-link (slice-specific clarifications). No content is lost.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes with no broken relative links.
- [x] Per `AGENTS.md` §5 ("keep patches small and scoped to one task when possible") each slice (1, 2-per-ADR, 3-per-migration-doc, 4) lands as its own commit/PR.
- [x] No `src/graphics/*` source or behavior changes in any slice.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Per-slice line-count check:
wc -l docs/architecture/graphics.md

# Confirm no source changes leaked in:
git diff --stat -- 'src/**'
```

## Forbidden changes
- Mixing mechanical relocation with semantic refactor of the architecture contract.
- Deleting decision content without relocating it to an ADR or migration doc.
- Folding multiple ADRs into a single ADR to keep the count low; one decision per ADR.
- Bundling multiple slices into a single commit.
- Changing `src/graphics/*` source under cover of this doc task.
- Reducing `graphics.md` past the canonical contract floor (sublayer split, dependency rules, frame lifecycle outline, renderer/RHI seam, GPU scene ownership contract must remain).
