# GEOM-010 — Point-cloud algorithm pack roadmap

## Goal
- Break the point-cloud gaps from the `src/geometry` review into an implementation-ready roadmap for filtering, descriptors, robust registration, and reconstruction algorithms.

## Non-goals
- No point-cloud algorithm implementation in this roadmap task.
- No GPU backend.
- No asset/runtime/graphics integration.
- No benchmark performance claims.

## Status
- Status: done.
- Completed: 2026-05-27.
- Commit: `98cc6b34` (landing commit for the retirement and follow-up pack promotion).
- PR: TBD.
- Owner/agent: GitHub Copilot on `main` working tree.
- Closure maturity: `Retired` for this roadmap-only task; implementation is
  explicitly owned by follow-up tasks and no point-cloud algorithm behavior ships
  in GEOM-010.
- Roadmap: [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Follow-up implementation packs: [`GEOM-016`](../done/GEOM-016-point-cloud-filtering-density-contracts.md) and [`GEOM-017`](../backlog/geometry/GEOM-017-point-cloud-descriptors-registration-seams.md).

## Context
- Owning subsystem/layer: `geometry` and `methods/geometry` where paper-specific work applies.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Existing point-cloud support includes `Geometry.PointCloud`, point-cloud IO, utilities, normal estimation, PCA, K-means, and registration, but lacks a structured roadmap for common modern point-cloud papers.
- Method-backed work must follow `docs/agent/method-workflow.md`: CPU reference first, correctness tests, benchmark harness, optimized CPU, GPU only after reference parity.

## Required changes
- [x] Create a roadmap document under `docs/architecture/` or `docs/methods/` that groups point-cloud algorithms into task-sized packs. ([`docs/architecture/point-cloud-algorithm-roadmap.md`](../../docs/architecture/point-cloud-algorithm-roadmap.md).)
- [x] Define candidate task packs for voxel/downsampling filters, statistical/radius outlier removal, MLS/RIMLS smoothing, bilateral normal filtering, keypoints/descriptors, robust primitive fitting, robust/global registration, multiway registration, and reconstruction alternatives.
- [x] For each pack, record required data structures, dependencies on GEOM-005 through GEOM-009, correctness fixtures, diagnostics, and benchmark manifests.
- [x] Identify which items belong in generic `src/geometry` APIs vs paper-specific `methods/geometry` packages.
- [x] Add follow-up task files for the first two implementation packs selected by priority. ([`GEOM-016`](../done/GEOM-016-point-cloud-filtering-density-contracts.md) and [`GEOM-017`](../backlog/geometry/GEOM-017-point-cloud-descriptors-registration-seams.md).)
- [x] Document reproducibility requirements for randomized sampling, RANSAC, clustering, and registration initialization.

## Tests
- [x] Run docs link validation. (See Verification.)
- [x] Run task policy validation after adding follow-up tasks. (See Verification.)
- [x] No C++ tests are required unless this roadmap task also adds code examples or fixtures. (No C++ examples or fixtures were added.)

## Docs
- [x] Update `tasks/backlog/geometry/README.md` to link the roadmap or follow-up implementation tasks.
- [x] Cross-link relevant method workflow docs.

## Acceptance criteria
- [x] Point-cloud algorithm work is split into reviewable, method-compliant task packs.
- [x] Each proposed implementation pack identifies CPU reference, tests, benchmarks, diagnostics, and forbidden shortcuts.
- [x] The roadmap preserves `geometry -> core` layering and keeps method-specific work under `methods/geometry` where appropriate.
- [x] Documentation and task links validate. (See Verification.)

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

### Commands run during the landing session

- `python3 tools/docs/check_doc_links.py --root .` — passed; checked 550 relative links with no broken relative links found.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed; validated 276 task files with 0 findings.

## Forbidden changes
- Do not implement algorithms in this roadmap task.
- Do not introduce new dependencies without a dedicated implementation task.
- Do not bypass the method workflow for paper-backed algorithms.




