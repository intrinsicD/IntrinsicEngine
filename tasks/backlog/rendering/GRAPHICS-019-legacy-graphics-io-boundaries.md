# GRAPHICS-019 — Legacy graphics IO ownership boundaries
## Goal
- Split legacy graphics import/export/model-loader feature coverage into architecture-compliant assets and geometry ownership tasks instead of reintroducing file IO into `src/graphics`.
## Non-goals
- No importer/exporter implementation in this task.
- No renderer pass work.
- No migration of graphics-layer behavior into the wrong owner.
## Context
- Owner: planning across `assets`, `geometry`, and `graphics` boundaries.
- Legacy `src/legacy/Graphics/Importers`, `Exporters`, `ModelLoader`, `Model`, and IO registry modules are adjacent to rendering but do not belong as live file IO inside final graphics layers.
## Required changes
- Inventory OBJ, PLY, STL, OFF, PCD, TGF, XYZ, GLTF, texture, and model-loader behavior currently under legacy graphics.
- Create or update follow-up `GEOM-`/`assets` tasks for importer/exporter/model ownership as needed.
- Define the graphics-facing asset/geometry GPU-view handoff required by rendering tasks.
## Tests
- Run task policy and docs-link checks after adding ownership tasks.
- Future implementation tasks must add parser/exporter unit tests under the owning subsystem.
## Docs
- Update migration parity docs to state that legacy graphics IO is retired through assets/geometry ownership, not promoted graphics imports.
## Acceptance criteria
- Every legacy graphics IO feature has an owning subsystem decision.
- Rendering tasks depend only on asset IDs, geometry GPU views, or promoted graphics asset residency APIs.
- No final graphics task requires direct model file parsing ownership.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding direct importer/exporter ownership to `src/graphics`.
