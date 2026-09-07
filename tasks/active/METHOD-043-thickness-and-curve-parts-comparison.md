---
id: METHOD-043
theme: I
depends_on: [METHOD-042]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Operator-directed interactive experiment, capped at four method iteration rounds; source, controls, raw observations and Claude review are retained."
owner: codex
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-07T12:01+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
contract_review: "Surface topology and embedding are semantic requirements. Same-cardinality results preserve source properties and topology. Any accepted candidate needs shared runtime/config/UI publication; this note owns those deferred decisions."
---
# METHOD-043 — Thickness and curve-aligned parts comparison

## Goal
- Compare an interior-aware shape-diameter signal and curve-aligned separator proposals against the rejected METHOD-042 neck sweep before choosing a reusable CPU method.

## Context
- The operator explicitly approved continuation with Claude on 2026-09-07, with **at most four iteration rounds**. This is directed method work outside the standing product-convergence focus, not production-design approval. METHOD-042's [record](../../docs/methods/frog_parts_experiment.md) retains the exact seam oracle and failed controls.

## Four-round budget
- A round is a changed scientific signal or selection rule plus its declared controls/cohort; an unsuccessful formulation consumes its round. No fifth tuning pass is allowed. Numerical/unit-test repairs are disclosed separately and cannot hide a changed formulation.
- Round 1: independent shape-diameter measurement and analytic/synthetic controls before frog.
- Round 2: a frozen area/persistence-based regional selector, if the field is usable.
- Round 3: one evidence-chosen boundary/proposal change, if warranted.
- Round 4: final predeclared robustness comparison or one remaining bounded change; stop even if quality remains unresolved.
- Existing METHOD-042 source/configs and production defaults remain immutable. Initial implementation is offline CPU diagnostic code, with native/config/UI adoption still gated on evidence.

## Acceptance criteria
- [x] Freeze an exact formulation and independent hypothesis for thickness, separate from contour proposal geometry.
- [x] Use asymmetric/off-axis necks, decorative grooves/ridges, and matched density/triangulation controls before inspecting frog.
- [ ] Distinguish explicit reference-seam energy, the best available proposal, and the selected proposal; measure inside-triangle versus source-edge approximation. **Deferred:** METHOD-042 retains its existing oracle; no new curve-aligned implementation fits within the exhausted four-round budget.
- [x] Keep every negative cell and compare against unchanged METHOD-039/040 baselines without frog-specific threshold tuning.
- [x] Only after a positive control verdict, select the smallest C++ CPU reference and integration slice; otherwise record another bounded rejection. **No adoption:** synthetic counts alone are insufficient and the native sculpt comparison regresses.

## Current stop-state
- All four authorized rounds are complete; no further experiment is authorized by this budget. [Report](../../docs/methods/frog_thickness_parts_experiment.md), [record](../../ara/evidence/diagnostics/method043/record.json), and ARA C66–C68 retain the bounded CPU observations and failures.
- Offline Python reference and tests only, not a new engine backend. The thickness-only taper/bulge result does not establish whole-part segmentation. No sculpt guard or default change was implemented.
- This note stays active as shared memory for the unimplemented comparison/integration decisions, pending operator review and a new work budget; it is not marked Operational or retired. The operator authorized publication of the outstanding research batch on 2026-09-07; the historical measurements retain their dirty-source classification after publication.

## Engine integration
| Field | Decision / owner |
| --- | --- |
| Least-structured input | Oriented embedded triangle surface for ray thickness and surface separators. |
| Compatible entity sources | Any mesh satisfying that contract, not arbitrary point/graph provenance. |
| RuntimeModule | METHOD-043 owns selection after a positive reference result; currently unimplemented. |
| Config/agent | METHOD-043 owns serialized, validated config and shared apply; no UI-only tuning. |
| UI | METHOD-043 owns any new parts selector; existing extremum overlay remains UI-053. |
| Publication | METHOD-043 owns detached face properties, preserving geometry and unrelated properties. |
| End-to-end tests | METHOD-043 owns compatible-source-to-publication coverage if a method is selected. |

## Verification
- `python3 tests/regression/tooling/Test.ShapeDiameterParts.py`
- `python3 tests/regression/tooling/Test.PartSeamOracle.py`
- `python3 tests/regression/tooling/Test.NeckSweep.py`
- `cmake --preset ci && cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Strict task, ARA, documentation-link, layout and manifest checks; exact experiment commands are retained per round.

## Limits
- No novelty claim, anatomical guarantee, CGAL installation, backend token, runtime change or default adoption. No fifth method iteration.
