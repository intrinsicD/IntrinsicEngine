# GEOM-011 — Parameterization and mapping roadmap

## Goal
- Break the parameterization, surface mapping, distortion, and atlas gaps from the `src/geometry` review into implementation-ready tasks for generic geometry APIs and paper-method packages.

## Non-goals
- No parameterization or mapping algorithm implementation in this roadmap task.
- No renderer/material/UV asset pipeline integration.
- No GPU backend.
- No benchmark performance claims.

## Status
- Status: done.
- Completed: 2026-05-27.
- Commit: `98cc6b34` (landing commit for the retirement and follow-up pack promotion).
- PR: TBD.
- Owner/agent: GitHub Copilot on `main` working tree.
- Closure maturity: `Retired` for this roadmap-only task; implementation is
  explicitly owned by follow-up tasks and no parameterization/mapping algorithm
  behavior ships in GEOM-011.
- Roadmap: [`docs/architecture/parameterization-mapping-roadmap.md`](../../docs/architecture/parameterization-mapping-roadmap.md).
- Follow-up implementation packs: [`GEOM-018`](../backlog/geometry/GEOM-018-parameterization-distortion-map-quality-diagnostics.md) and [`GEOM-019`](../backlog/geometry/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md).

## Context
- Owning subsystem/layer: `geometry` and `methods/geometry` where paper-specific work applies.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Existing support includes LSCM parameterization and some quality metrics, but modern geometry papers commonly need harmonic/Tutte embeddings, ABF/ABF++, ARAP, SLIM, distortion diagnostics, atlas segmentation, chart packing, and surface-to-surface map representations.
- Method-backed work must follow `docs/agent/method-workflow.md`: CPU reference first, correctness tests, benchmark harness, optimized CPU, GPU only after reference parity.

## Required changes
- [x] Create a roadmap document under `docs/architecture/` or `docs/methods/` that groups parameterization and mapping work into task-sized packs. ([`docs/architecture/parameterization-mapping-roadmap.md`](../../docs/architecture/parameterization-mapping-roadmap.md).)
- [x] Define candidate task packs for harmonic/Tutte embedding, ARAP parameterization, SLIM, distortion metrics, atlas segmentation, seam generation, chart packing, barycentric map storage, functional maps, and map-quality metrics.
- [x] For each pack, record dependencies on GEOM-005 through GEOM-009, especially linear algebra, optimization, robust predicates, mesh soup/conversion, diagnostics, and benchmark manifests.
- [x] Identify which components are generic geometry infrastructure and which are paper-method implementations.
- [x] Add follow-up task files for the first two implementation packs selected by priority. ([`GEOM-018`](../backlog/geometry/GEOM-018-parameterization-distortion-map-quality-diagnostics.md) and [`GEOM-019`](../backlog/geometry/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md).)
- [x] Document standard fixtures and correctness checks for disk topology, boundary conditions, flipped triangles, distortion metrics, and degenerate inputs.

## Tests
- [x] Run docs link validation. (See Verification.)
- [x] Run task policy validation after adding follow-up tasks. (See Verification.)
- [x] No C++ tests are required unless this roadmap task also adds code examples or fixtures. (No C++ examples or fixtures were added.)

## Docs
- [x] Update `tasks/backlog/geometry/README.md` to link the roadmap or follow-up implementation tasks.
- [x] Cross-link relevant method workflow docs.

## Acceptance criteria
- [x] Parameterization and mapping work is split into reviewable, method-compliant task packs.
- [x] Each proposed implementation pack identifies CPU reference, tests, benchmarks, diagnostics, and forbidden shortcuts.
- [x] The roadmap preserves `geometry -> core` layering and keeps method-specific work under `methods/geometry` where appropriate.
- [x] Documentation and task links validate. (See Verification.)

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

### Commands run during the landing session

- `python3 tools/docs/check_doc_links.py --root .` — passed; checked 556 relative links with no broken relative links found.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed; validated 278 task files with 0 findings.

## Forbidden changes
- Do not implement algorithms in this roadmap task.
- Do not introduce renderer/material/asset dependencies.
- Do not bypass the method workflow for paper-backed algorithms.



