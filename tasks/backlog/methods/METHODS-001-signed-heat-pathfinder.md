# METHODS-001 — Pin signed heat as methods-pipeline pathfinder

## Goal
- [ ] Pin [`METHOD-002 — Signed Heat Method reference backend`](METHOD-002-signed-heat-method-reference-backend.md) as the single concrete method that drives the `methods/` pipeline end-to-end first, validating the `AGENTS.md` §6 method protocol (paper intake → CPU reference → correctness tests → benchmark harness → docs) and the `methods/` scaffolding that has stood unused since `RORG-040..044`.
- [ ] Record the prerequisite chain and slice ordering so the work can be promoted to `tasks/active/` deterministically once the LDLT follow-up [`GEOM-016`](../geometry/GEOM-016-sparse-direct-factorization-seam.md) lands (retired `GEOM-008` alone is insufficient — it ships only the CSR builder and iterative CG; METHOD-002 Step 2 names LDLT).

## Non-goals
- [ ] No re-authoring of METHOD-002 itself; its required-changes list, variants, and acceptance criteria stand.
- [ ] No re-scoping of `GEOM-008` (sparse-solver infrastructure; retired at `CPUContracted` shipping CSR builder + CG only), `GEOM-005` (geometry API style + numeric policy), or `GEOM-016` (the LDLT/LLT follow-up).
- [ ] No selection of an alternative pathfinder method (`METHOD-001` is gated by physics layer ownership and is not the right starter; `METHOD-005`/`METHOD-007` are hard-gated by `GEOM-007` robust predicates; `METHOD-003`/`METHOD-004` are reasonable peers but do not share as much code surface with the existing `Geometry.HalfedgeMesh.Geodesic` / `VectorHeatMethod` modules).
- [ ] No introduction of new method packages or method-program scope.

## Context
- Owning subsystem/layer: `methods/` program governance + `geometry`.
- As of 2026-05-16, `methods/` contains 0 LOC of implementation: only `_template/` and the placeholder `geometry/_example_vector_heat/` scaffold. Seven backlog method cards exist (`METHOD-001..007`) but none has been started. This is the most obvious unstarted track relative to the stated mission ("method-driven research integration"; `AGENTS.md` §1).
- Signed Heat (Feng & Crane, SIGGRAPH 2024) is the strongest pathfinder pick because:
  - The promoted `src/geometry/` layer already ships `Geometry.HalfedgeMesh.DEC`, `Geometry.HalfedgeMesh.Geodesic`, and `Geometry.HalfedgeMesh.VectorHeatMethod`. METHOD-002 reuses cotan-Laplace + lumped mass already assembled by DEC.
  - The CPU reference algorithm is well-documented (three linear-algebra steps after Laplace assembly). It originally gated on a single missing seam: `GEOM-008` (Eigen3 introduction + sparse builders + diagnostics). `GEOM-008` retired on 2026-05-27 shipping the CSR builder and CG iterative solver only; the LDLT path that METHOD-002 Step 2 names is owned by the follow-up `GEOM-016`. The promotion gate is therefore `GEOM-016`, not retired `GEOM-008`.
  - Its acceptance test (analytic disk + sphere convergence) is closed-form, so correctness verification does not depend on a third-party parity oracle in the default build.
  - It produces a demoable output (signed geodesic distance scalar field) that the existing geometry library can visualize without depending on the promoted Vulkan path becoming operational. This decouples methods progress from the GRAPHICS-033 critical path.
- Prerequisite chain (hard, in order):
  1. [`GEOM-005 — Geometry API style and numeric policy`](../../done/GEOM-005-api-style-and-numeric-policy.md) — already retired to `tasks/done/`.
  2. [`GEOM-008 — Geometry linear algebra and solver infrastructure`](../../done/GEOM-008-linear-algebra-solver-infrastructure.md) — Eigen3 dependency, CSR sparse builders + CG / shifted-CG iterative solver + diagnostics. Retired 2026-05-27 at `CPUContracted`. Does **not** ship the LDLT path METHOD-002 Step 2 names.
  3. [`GEOM-016 — Sparse direct factorization solver seam (LDLT/LLT)`](../geometry/GEOM-016-sparse-direct-factorization-seam.md) — the LDLT / SimplicialLDLT seam that METHOD-002 Step 2 actually calls. Promotion gate for METHOD-002.
  4. [`METHOD-002 — Signed Heat Method reference backend`](METHOD-002-signed-heat-method-reference-backend.md) — paper intake, scaffolding, CPU reference, correctness tests, benchmark stub, docs.
- Soft dependency (not blocking): `GEOM-009 — Benchmark manifests / fixtures / smoke`. METHOD-002 can ship its benchmark harness using the `benchmarks/_template` even if `GEOM-009` lands after.
- Cross-link: the methods-program README ordering note ([`tasks/backlog/methods/README.md`](README.md) Convergence §3) records the `GEOM-008` / `GEOM-016` gate split for `METHOD-002/003/004` and is consistent with this pin.

## Required changes
- [ ] Update [`tasks/backlog/methods/README.md`](README.md) to mark METHOD-002 as the methods-pipeline pathfinder pick, with a short rationale and a pointer back to this task.
- [ ] Update [`tasks/backlog/methods/METHOD-002-signed-heat-method-reference-backend.md`](METHOD-002-signed-heat-method-reference-backend.md) Context section to add a bullet linking back here and stating "Pathfinder method per `METHODS-001`."
- [ ] Update [`tasks/backlog/methods/METHOD-002-signed-heat-method-reference-backend.md`](METHOD-002-signed-heat-method-reference-backend.md) Variants section to record the default selection: tick `[x]` for **Variant A — Signed Heat Method on surfaces**. Leave B and C as documented follow-ups.
- [ ] Add a "Pathfinder note" callout to [`docs/methods/index.md`](../../../docs/methods/index.md) explaining that METHOD-002 is the first method that will be driven through the full pipeline, so contributors recognize it as the canonical reference implementation pattern.

## Tests
- [ ] No code is produced by this task. No new automated tests.
- [ ] Manual verification: after the four required-change edits, `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` both pass.

## Docs
- [ ] [`tasks/backlog/methods/README.md`](README.md) reflects the pathfinder pick.
- [ ] [`docs/methods/index.md`](../../../docs/methods/index.md) carries the callout described above.
- [ ] This task file itself is the single source of truth for the pathfinder selection rationale; downstream tasks cite it rather than restating it.

## Acceptance criteria
- [ ] `tasks/backlog/methods/METHODS-001-signed-heat-pathfinder.md` exists in backlog and is referenced from `tasks/backlog/methods/README.md`.
- [ ] METHOD-002 carries a `[x]` mark on Variant A and a Context-section back-reference to this task.
- [ ] `docs/methods/index.md` records the pathfinder pick.
- [ ] No other method backlog card is marked as pathfinder.
- [ ] Strict task-policy and docs-links validators pass.
- [ ] Promotion rule recorded here: when `GEOM-016` retires to `tasks/done/` (the LDLT / `SimplicialLDLT` seam owed to METHOD-002 Step 2), the next agent who picks up methods work moves METHOD-002 from `tasks/backlog/methods/` to `tasks/active/` and starts slice 1 (method-package scaffolding + Variant A reference implementation against the analytic disk test). Retired `GEOM-008` alone does not satisfy this gate — it ships the CSR builder and CG path only, not the LDLT path named in Step 2.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Promoting METHOD-002 to `tasks/active/` before `GEOM-016` retires (the LDLT seam METHOD-002 Step 2 names); the dependency is hard. Retired `GEOM-008` alone is not sufficient.
- Starting implementation work in `src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.*` under cover of this planning task.
- Picking a second method as a parallel pathfinder; the point of this task is exactly one pick.
- Re-authoring the METHOD-002 algorithm contract.
