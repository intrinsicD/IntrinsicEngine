# GEOM-011 — Parameterization and mapping roadmap

## Goal
- Break the parameterization, surface mapping, distortion, and atlas gaps from the `src/geometry` review into implementation-ready tasks for generic geometry APIs and paper-method packages.

## Non-goals
- No parameterization or mapping algorithm implementation in this roadmap task.
- No renderer/material/UV asset pipeline integration.
- No GPU backend.
- No benchmark performance claims.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry` where paper-specific work applies.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Existing support includes LSCM parameterization and some quality metrics, but modern geometry papers commonly need harmonic/Tutte embeddings, ABF/ABF++, ARAP, SLIM, distortion diagnostics, atlas segmentation, chart packing, and surface-to-surface map representations.
- Method-backed work must follow `docs/agent/method-workflow.md`: CPU reference first, correctness tests, benchmark harness, optimized CPU, GPU only after reference parity.

## Required changes
- [ ] Create a roadmap document under `docs/architecture/` or `docs/methods/` that groups parameterization and mapping work into task-sized packs.
- [ ] Define candidate task packs for harmonic/Tutte embedding, ARAP parameterization, SLIM, distortion metrics, atlas segmentation, seam generation, chart packing, barycentric map storage, functional maps, and map-quality metrics.
- [ ] For each pack, record dependencies on GEOM-005 through GEOM-009, especially linear algebra, optimization, robust predicates, mesh soup/conversion, diagnostics, and benchmark manifests.
- [ ] Identify which components are generic geometry infrastructure and which are paper-method implementations.
- [ ] Add follow-up task files for the first two implementation packs selected by priority.
- [ ] Document standard fixtures and correctness checks for disk topology, boundary conditions, flipped triangles, distortion metrics, and degenerate inputs.

## Tests
- [ ] Run docs link validation.
- [ ] Run task policy validation after adding follow-up tasks.
- [ ] No C++ tests are required unless this roadmap task also adds code examples or fixtures.

## Docs
- [ ] Update `tasks/backlog/geometry/README.md` to link the roadmap or follow-up implementation tasks.
- [ ] Cross-link relevant method workflow docs.

## Acceptance criteria
- [ ] Parameterization and mapping work is split into reviewable, method-compliant task packs.
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
- Do not introduce renderer/material/asset dependencies.
- Do not bypass the method workflow for paper-backed algorithms.

