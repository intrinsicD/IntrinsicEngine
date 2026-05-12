# GEOM-010 — Point-cloud algorithm pack roadmap

## Goal
- Break the point-cloud gaps from the `src/geometry` review into an implementation-ready roadmap for filtering, descriptors, robust registration, and reconstruction algorithms.

## Non-goals
- No point-cloud algorithm implementation in this roadmap task.
- No GPU backend.
- No asset/runtime/graphics integration.
- No benchmark performance claims.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry` where paper-specific work applies.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Existing point-cloud support includes `Geometry.PointCloud`, point-cloud IO, utilities, normal estimation, PCA, K-means, and registration, but lacks a structured roadmap for common modern point-cloud papers.
- Method-backed work must follow `docs/agent/method-workflow.md`: CPU reference first, correctness tests, benchmark harness, optimized CPU, GPU only after reference parity.

## Required changes
- [ ] Create a roadmap document under `docs/architecture/` or `docs/methods/` that groups point-cloud algorithms into task-sized packs.
- [ ] Define candidate task packs for voxel/downsampling filters, statistical/radius outlier removal, MLS/RIMLS smoothing, bilateral normal filtering, keypoints/descriptors, robust primitive fitting, robust/global registration, multiway registration, and reconstruction alternatives.
- [ ] For each pack, record required data structures, dependencies on GEOM-005 through GEOM-009, correctness fixtures, diagnostics, and benchmark manifests.
- [ ] Identify which items belong in generic `src/geometry` APIs vs paper-specific `methods/geometry` packages.
- [ ] Add follow-up task files for the first two implementation packs selected by priority.
- [ ] Document reproducibility requirements for randomized sampling, RANSAC, clustering, and registration initialization.

## Tests
- [ ] Run docs link validation.
- [ ] Run task policy validation after adding follow-up tasks.
- [ ] No C++ tests are required unless this roadmap task also adds code examples or fixtures.

## Docs
- [ ] Update `tasks/backlog/geometry/README.md` to link the roadmap or follow-up implementation tasks.
- [ ] Cross-link relevant method workflow docs.

## Acceptance criteria
- [ ] Point-cloud algorithm work is split into reviewable, method-compliant task packs.
- [ ] Each proposed implementation pack identifies CPU reference, tests, benchmarks, diagnostics, and forbidden shortcuts.
- [ ] The roadmap preserves `geometry -> core` layering and keeps method-specific work under `methods/geometry` where appropriate.
- [ ] Documentation and task links validate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement algorithms in this roadmap task.
- Do not introduce new dependencies without a dedicated implementation task.
- Do not bypass the method workflow for paper-backed algorithms.

