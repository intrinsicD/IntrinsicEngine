---
id: METHOD-080
theme: I
depends_on: [METHOD-079]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive descriptive diagnosis; reviewed packet and raw census retained with task evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-080 — Census invalid Nyström target rows

## Goal
1. "Bei welchem sampling Verfahren passiert das?"
2. "Kannst du untersuchen welche Zeilen das genau sind?"
3. "Was zeichnet diese Punkte aus?"
4. "An welcher Stelle werden die affinitätssummen negativ?"

Operator-directed continuation outside Framework24 P0. [Plan](../evidence/METHOD-080/plan.md)
maps all work to these questions. No production remedy or broader sampling comparison.

## Acceptance criteria
- [x] Complete invalid-row IDs for the frozen prefixes, exact sampling semantics and algebra location.
- [x] Within-attempt geometric comparisons and explicitly bounded exact-affinity samples.
- [x] Independently audited report; temporary native probes removed; original focused tests pass.

## Contract matrix
| Contract | Scope / evidence |
|---|---|
| method.engine-integration | Existing public CPD runner and full point fixture; diagnostic probes only. No runtime/UI/config integration change. |

## Engine integration
No new selectable backend, method API, property output or runtime capability. Existing point
fixture/domain is reused through the public registration API. Native instrumentation is
temporary and removed after capture; integration ownership remains with the existing CPD method.

| Field | Disposition |
| --- | --- |
| Least-structured input | Existing point-position sets; no contract change. |
| Compatible entity sources | Existing mesh vertices, graph nodes and point clouds. |
| RuntimeModule | Existing CPD registration; diagnostic only, no new wiring. |
| Config/agent | Existing method configuration; temporary capture removed before delivery. |
| UI | Existing CPD controls unchanged. |
| Publication | Existing transformed source, same cardinality. |
| End-to-end tests | Actual Register path in existing landmark runner; no capability promotion. |

## Verification
```sh
python3 tasks/evidence/METHOD-080/verification/test-controls.py
python3 tasks/evidence/METHOD-080/verification/test-technical.py
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^CoherentPointDriftAccelerated\.'
```

## Completion — 2026-10-11

Canonical result and limits: [audit](../evidence/METHOD-080/audit.md), ARA C121.
Frozen execution commit: `37c2a0e0a35ab4f9e1f03588ab4b3fb50758ba7b`.
The four-cell descriptive census completed; production probes are removed and both native
files equal `dcbb0e599` exactly. No production guard, backend or parameter change is adopted.

Independent reviewer `/root/nystrom_census_results` decided:
**“Verdict: approve. All three task acceptance criteria are met.”**
Its [actual answer](../evidence/METHOD-080/reviews/results-review.txt) records the raw audit,
cleanup and 28/28 original Accelerated tests. Full CPU/sanitizer/GPU were not rerun;
the pre-existing BUG-241 document-link failures remain. Other samplers, broader data and
a production remedy remain outside this completed diagnostic slice.
