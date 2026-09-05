---
id: METHOD-036
theme: I
depends_on: [METHOD-032, METHOD-034, METHOD-035]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# METHOD-036 — Normal-orientation method comparison evidence (publication protocol)

## Goal
- If METHOD-032 yields an accepted implementation, produce its publication
  comparison against MST, iPSR, and PGR using separate orientation-only and
  joint estimation/reconstruction groups, honest input-information contracts,
  matched fixture identities, and stable per-method benchmark IDs.

## Operator decisions and start gate (2026-09-05)

- A retired dependency is not proof that its comparator exists. Before any
  comparison execution, verify METHOD-032's terminal disposition and accepted
  public implementation. If its killing gate rejects the method without that
  implementation, skip this publication comparison and record the parent
  verdict for an inapplicable/superseded closure. Do not invent comparator
  results or silently turn this into a baseline-only study; the latter needs
  separate operator direction.
- Keep methods faithful to their selected contracts. Separate orientation-only
  runs from methods that estimate directions/reconstruct jointly; matching
  fixture positions is not evidence of identical information consumption.
- Use the existing benchmark metric allowlist and structured diagnostics.
  A new gating metric requires a separate justified schema decision; this task
  does not assume METHOD-032 widens the allowlist.

## Non-goals
- No neural baselines (dipole propagation, learned orientation networks): the engine has no ML runtime, and training-dependent results are not reproducible under this repository's determinism policy. This exclusion is recorded in the report so the competitor set is honest about its scope.
- No new algorithm implementations and no changes to the compared methods — measurement and reporting only.
- No performance or quality claims outside the measured data; no claims enter `README`/`paper.md`/task closures before the results audit.

## Context
- Orientation-only group: METHOD-032 and MST receive the same precomputed
  unoriented normals and positions (estimation once with `OrientationMode::None`).
  Freeze the same scramble and any shared preprocessing for those runs.
- Joint estimation/reconstruction group: iPSR initializes its own seeded
  directions and PGR solves for surfel vectors. They share the fixture
  positions but keep their actual method-specific inputs. Ground-truth normals
  are scoring-only, and supplied normals are neither fabricated as an input
  nor injected into an algorithm merely to pass a comparability guard.
- Freeze fixture sampling, scramble, and method-specific initialization seeds
  separately. Report end-to-end versus orientation-only costs distinctly;
  cross-group outcome tables must disclose their different information and
  preprocessing budgets, not claim an isolated orientation-only advantage.
- Fixture matrix: the `METHOD-032` synthetic suite (sphere, torus, thin plate, hollow shell, open hemisphere, noise ladder) for the smoke lane; larger declared scan datasets for the heavy/nightly lane per `docs/methods/dataset-policy.md` — no external data in smoke.
- Metrics: `oriented_correct_fraction` (primary), per-method diagnostics (parity conflict rate, iPSR iterations, PGR residual), `runtime_ms` (reported, never the headline), and per-fixture failure statuses (a method that fails closed on the open hemisphere is *correct*, and the report says so).
- Report lands under `methods/geometry/octree_parity_orientation/reports/` following `docs/methods/report-template.md`; audited per the results-audit checklist before any claim propagates.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Frozen finite position spans and only the additional typed inputs each method actually consumes; domains/provenance do not manufacture extra algorithm inputs. |
| Compatible entity sources | Fixture arrays may originate from any compatible property domain; this evidence task adds no ECS eligibility filter or adapter. |
| RuntimeModule | Not applicable to comparison execution; invoke public method APIs from benchmark runners, not runtime/UI internals. |
| Config/agent | Versioned manifests bind exact inputs, params, seeds, budgets, and source identity; no new engine config lane. |
| UI | No editor promotion is intended; this task produces reproducible reports, not a Sandbox selector. |
| Publication | Result JSON and audited evidence tables only; no mutation of source properties, cardinality, or ECS state. |
| End-to-end tests | Benchmark invocation, per-group input guards, result validation, failed-cell accounting, and audit replay. |

## Required changes
- [ ] Record the METHOD-032 start-gate verdict before allocating comparison
      runs. The measurement requirements below apply only when that gate admits
      the study; otherwise preserve the negative parent evidence and skip it.
- [ ] Create a benchmark manifest family with stable per-method IDs and a
      declared comparison group, common fixture hash, actual consumed-input
      hashes, preprocessing, params, budgets, and seeds. Freeze formulas for
      existing allowed quality metrics; retain orientation correctness,
      conflicts, iterations, and residuals in structured diagnostics.
- [ ] Heavy/nightly manifests with declared datasets per `docs/methods/dataset-policy.md`.
- [ ] Add a group-aware comparability guard: positions must match for every
      applicable run, precomputed normals must match within the orientation-only
      group, and joint methods must match their declared inputs/initialization
      without access to scoring-only ground truth. Record unconsumed fields as
      such instead of claiming their hashes prove consumption.
- [ ] Comparison report in `methods/geometry/octree_parity_orientation/reports/` with per-method, per-fixture tables and failure-status accounting.
- [ ] Measured tables and competitor discussion folded into the `METHOD-032` package `paper.md`.

## Tests
- [ ] Benchmark result payloads validate (`tools/benchmark/validate_benchmark_results.py`) for every compared method.
- [ ] The guard rejects changed positions, changed orientation-group normals,
      mismatched params/seeds/budgets, leaked scoring truth, and false
      declarations of consumed inputs; legitimate method-specific input
      differences pass only in their declared group.
- [ ] A refuted/no-implementation METHOD-032 disposition prevents comparison
      execution rather than producing missing or fabricated comparator cells.

## Docs
- [ ] Report document (per template) with limitations and the neural-baseline exclusion rationale.
- [ ] `METHOD-032` package `README.md`/`paper.md` updated with measured results only.

## Acceptance criteria
- [ ] The admitted-study branch measures all four methods on their declared
      smoke groups and executes the declared heavy lane at least once. The
      negative-parent branch records why no comparison was run and makes no
      baseline-only study or measurement claim.
- [ ] For an admitted study, the report and results audit are complete before
      closure, with findings resolved or recorded. A negative-parent closure
      cites the accepted parent verdict instead of pretending a study ran.
- [ ] No unmeasured or extrapolated claims anywhere in the touched docs.

## Verification
```bash
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No edits to the compared method implementations (fixes go through their owning tasks).
- No dataset additions outside `docs/methods/dataset-policy.md`; no external data in the smoke lane.
- No selective reporting: every configured fixture appears in the report, including failures.
- No forced identical-normal input for algorithms that do not consume it,
  algorithm changes to fit the harness, or undisclosed cross-group advantages.

## Maturity
- Evidence task: no engine maturity or `Operational` follow-up is owed.
  Admitted measurements require the results audit; a negative-parent
  supersession records non-execution and cites the parent's accepted verdict.
