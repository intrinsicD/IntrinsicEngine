---
id: GEOM-053
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-29
---
# GEOM-053 — Geometry reuse and deterministic sampling cleanup

## Goal
- Reduce duplicated geometry helper code and make sphere random sampling deterministic by reusing narrow geometry-owned utility surfaces that match `docs/architecture/geometry-api-style.md`.

## Non-goals
- No broad `Geometry` umbrella export expansion.
- No public migration of legacy mesh conversion helpers in this slice.
- No behavior changes to `Geometry.DomainViews` borrow contracts.
- No wholesale rewrite of normal-estimation, conversion, or mesh utility modules.

## Context
- Owner/layer: `src/geometry`; the layer contract remains `geometry -> core` only.
- Status: done; retired on 2026-06-29 by Codex. Implementation landed in commit
  `a96e5bd4` (`Add deterministic geometry sampling reuse`); retirement
  bookkeeping is this commit.
- PR/commit: this retirement commit (`Retire completed active task records`);
  implementation commit `a96e5bd4`.
- The knowledge-graph MCP list/graph calls timed out during this slice, so the concrete changes below are grounded in a source audit of the same geometry reuse target classes: `Geometry.Properties`, `Geometry.DomainViews`, `Geometry.Mesh.Conversion`, `Geometry.PointCloud.Conversion`, `Geometry.RobustPredicates`, `Geometry.Linalg`, `Geometry.Sparse`, and deterministic sampling helpers.
- The first reviewable slice should address high-confidence duplication without mixing in compatibility migration work.
- `docs/architecture/geometry-api-style.md` requires deterministic random algorithms, explicit diagnostics, narrow imports, and least-structured domain reuse.
- Deferred reuse opportunities:
  - Migrating `Geometry.HalfedgeMesh.Utils::BuildHalfedgeMeshFromIndexedTriangles` to `Geometry.Mesh.Conversion` needs a dedicated compatibility slice because welding behavior, diagnostics, and importer call sites must be preserved deliberately.
  - Centralizing normal-estimation helpers across graph, point-cloud, and mesh code needs an algorithm-specific slice because each domain has different neighborhood semantics despite sharing PCA-style linear algebra.
  - Broader `Geometry.DomainViews` adoption should land through individual algorithm migrations, not by reshaping the borrow contracts in this cleanup.

## Required changes
- [x] Add a narrow geometry helper module for deterministic sampling/noise primitives shared by graph, point-cloud, and sphere sampling code.
- [x] Refactor `Geometry.Graph.Utils` and `Geometry.PointCloud.Utils` to reuse the shared deterministic Gaussian displacement helper instead of duplicate local seed-mixing implementations.
- [x] Refactor `Geometry.Sphere.Sampling` random APIs to accept explicit seed parameters while preserving existing overloads as deterministic defaults.
- [x] Reuse `Geometry.RobustPredicates` in graph edge-crossing orientation tests where it preserves existing graph-layout semantics.
- [x] Record any deferred reuse opportunities that are too broad for this slice.

## Tests
- [x] Add or update focused unit tests proving deterministic sphere random sampling.
- [x] Add or update focused unit tests proving graph and point-cloud Gaussian noise reuse remains deterministic.
- [x] Preserve existing graph edge-crossing behavior through existing tests or a focused regression.

## Docs
- [x] Update `docs/architecture/geometry-api-style.md` only if the public policy changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if a module surface is added or changed.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task.

## Acceptance criteria
- [x] No duplicated `MixSeed`/`GaussianDisplacement` implementation remains in graph and point-cloud utility implementation files.
- [x] Sphere random sampling no longer uses `std::random_device` in public geometry algorithms.
- [x] Public random sampling surfaces have deterministic seed-bearing overloads.
- [x] New reuse module is narrow and not re-exported through the broad `Geometry` umbrella.
- [x] Layering, task policy, and focused geometry tests pass.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Full CPU gate note: the broad gate completed with two unrelated `ProgressivePoissonReferenceDim`
CTest timeout-property failures; both failed parameters passed when run directly with a larger shell
timeout.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Expanding the geometry umbrella module for convenience.
- Replacing legacy mesh conversion APIs without a dedicated compatibility task.

## Maturity
- Target: `CPUContracted`; this is a CPU geometry utility cleanup with focused tests.
- No `Operational` follow-up is owed.
