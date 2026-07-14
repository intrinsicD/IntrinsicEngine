---
id: UI-017
theme: F
depends_on: [UI-016, UI-015, RUNTIME-113, RUNTIME-114]
maturity_target: CPUContracted
---
# UI-017 — Bound render state inspector

## Completion
- Retired on 2026-06-17 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: the sandbox editor model now includes bound-state rows for selected
  mesh, graph, point-cloud, and composition entities, correlating render hints,
  progressive presentation slots, property-catalog entries, defaults/textures,
  readiness diagnostics, and derived-job progress.
- Evidence: focused runtime/UI coverage passed in the combined
  `SandboxEditor|EditorCommandHistory|Uv|TextureBake|SelectedMeshTextureBake|MeshAttributeTextureBake|DerivedJob|Progressive|PropertyCatalog`
  CTest run.

## Slice plan
- **Slice A. Bound-state DTOs.** Add rows for render lanes, presentation keys, slot semantics, source kind, current property/default/texture, readiness, diagnostics, and command availability.
- **Slice B. Snapshot builders.** Populate bound-state rows from render hints, progressive presentation extraction, property-catalog descriptors, visualization config, and derived-job snapshots for mesh, graph, point-cloud, and composition entities.
- **Slice C. ImGui visibility.** Add compact domain/inspector rendering for bound-state rows and disabled command surfaces without introducing renderer or asset ownership into UI state.
- **Slice D. Tests/docs/retirement.** Add headless tests for default/property/texture/pending/failed/retained states, update docs, refresh inventories, and retire at `CPUContracted`.

## Goal
- Add sandbox editor bound-state inspection for selected mesh, graph, point-cloud, and composition entities so users can see render lanes, presentation slots, bound properties/textures/defaults, readiness, diagnostics, and bake/job progress in one coherent UI model.

## Non-goals
- No new render backend behavior or Vulkan proof.
- No UI-owned texture baking, UV generation, asset upload, worker scheduling, or graphics resource ownership.
- No graph or point-cloud texture baking.
- No inherited bulk edit table for composition children.
- No implicit mutation of bindings from inspection-only rows.

## Context
- Owning subsystem/layer: `runtime` editor UI consumes data-only snapshots from runtime render extraction, progressive presentation, derived-job, and property-catalog seams.
- `UI-015` exposes the first progressive render-data inspector; this task reorganizes and broadens bound-state visibility into per-domain usability comparable to the framework24 material/property panels.
- `UI-016` supplies the all-properties catalog and compatible binding chooser that this inspector should link from.
- `RUNTIME-113` supplies descriptor extraction and readiness diagnostics. `RUNTIME-114` supplies progressive import enrichment jobs and generated output state.
- The inspector should make the current state obvious: which render lanes exist, which slots are default/property/authored-texture/generated-texture backed, which properties are bound, which jobs are baking or waiting, and why a slot is not ready.

## Required changes
- [x] Add bound-state sections to mesh, graph, and point-cloud domain UI models for render lanes, presentation keys, active slots, source kind, current property/texture/default, readiness, and diagnostics.
- [x] Surface render-hint bindings such as point size and edge width alongside progressive presentation slots.
- [x] Show material/visualization binding rows for selected mesh surface, mesh edges/points, graph edges/points, and point-cloud points with domain-appropriate enabling.
- [x] Link each property-backed row to the `UI-016` property catalog entry or expose enough descriptor data for the UI to correlate them.
- [x] Add composition aggregate rows that summarize child lane readiness, failed slots, and derived-job status without editing all children as one inherited table.
- [x] Add global and selected-entity job/bake progress rows with status, progress, dependencies, output semantic, elapsed time, and diagnostic text.
- [x] Keep unavailable command surfaces visible but disabled with the owning task or reason, rather than hiding partially supported workflows.
- [x] Ensure bound-state rows remain data-only and do not store renderer handles, raw property pointers, or live asset-service references.

## Tests
- [x] Add headless UI model tests proving selected mesh, graph, and point-cloud bound-state rows include render lanes, active slots, source kind, readiness, and diagnostics.
- [x] Add tests proving bound property rows correlate with property catalog descriptors from `UI-016`.
- [x] Add tests for default-backed, property-buffer-backed, authored-texture-backed, generated-texture-backed, pending, failed, and retained-previous-output states.
- [x] Add composition aggregate tests proving child readiness/job failures summarize without bulk child mutation.
- [x] Add derived-job/bake progress table tests for queued, running, applying, complete, failed, cancelled, and stale states.
- [x] Add tests proving disabled command surfaces remain visible with deterministic reasons.

## Docs
- [x] Update `src/runtime/Editor/README.md` or `src/runtime/README.md` with the bound-state inspector ownership and snapshot sources.
- [x] Update `tasks/backlog/ui/README.md` if follow-up controls are opened.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Users can see what is currently bound for each selected entity render lane and presentation slot.
- [x] Users can distinguish uniform defaults, property buffers, authored textures, generated textures, pending jobs, failed jobs, and retained previous outputs.
- [x] Users can see bake/derived-job progress and diagnostics without UI owning worker or asset state.
- [x] Composition entities summarize child readiness and failures without pretending to own one shared material table.
- [x] The default CPU-supported CTest gate verifies the bound-state inspector model.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|BoundState|Progressive|DerivedJob|PropertyCatalog' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not mutate geometry, asset, renderer, or worker state from inspection-only rows.
- Do not allocate or store Vulkan/RHI/renderer handles in UI state.
- Do not hide unsupported or unavailable bindings; show disabled rows with reasons.
- Do not add graph or point-cloud texture baking.
- Do not bypass runtime command surfaces for user-visible binding changes.

## Maturity
- Target: `CPUContracted`.
- This UI task closes the bound-state inspector data-model contract; no `Operational` follow-up is owed.
