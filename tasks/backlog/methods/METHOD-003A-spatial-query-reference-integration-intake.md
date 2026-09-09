---
id: METHOD-003A
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive integration-ownership intake only; implementation and research evidence remain in their selected tasks.
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# METHOD-003A — Spatial-query CPU-reference integration intake

## Goal

Own the deferred engine-integration decisions for METHOD-003, METHOD-004,
METHOD-005, METHOD-015, METHOD-027, METHOD-028 and METHOD-032 when each accepted
CPU reference is selected for adoption. Keep those reference tasks' algorithms,
research gates and backend scope unchanged.

## Context

The spatial-acceleration inventory update enrolls these older task notes in
the current contract schema. This intake supplies their missing named adoption
owner, following METHOD-007A and METHOD-033A. It is planning memory, not an
instruction to implement all seven methods or to start paused research.
Select parents individually under the standing product priority and operator
direction; a failed scientific gate does not justify engine promotion.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Each accepted parent contract: point properties/spans for point methods, exact boundary/surface topology or analytic oracles where semantically required. |
| Compatible entity sources | All canonical element domains satisfying the selected parent; no point-cloud-only or fixed property-name filter. |
| RuntimeModule | This intake selects the existing operation/composition owner and allocates a bounded implementation task per adopted parent. |
| Config/agent | Scope one validated serializable apply path with actual backend and query capabilities. |
| UI | Allocate discovery/readiness work on every compatible source through runtime's preflight. |
| Publication | Freeze each parent's transform, property, sampled-field or new-mesh destination, preserving unrelated data and explicit topology/history ownership. |
| End-to-end tests | Allocate source/config-to-publication, stale/failure, CPU/GPU provenance and UI coverage to named implementation owners. |

## Spatial acceleration consideration

Consult the [consumer inventory](../../../docs/architecture/spatial-index-consumers.md).
Choose stable entity cache, private evolving workspace, existing index/scan or
a separately scoped missing-query extension from actual primitive/metric needs.
Do not treat point proximity as a triangle oracle, an exact Gaussian sum or an
octree parity lattice. Record deferred query integration in the implementation
task so reference completion cannot be mistaken for engine integration.

## Acceptance criteria

- [ ] For each parent, record accepted-reference and product-selection status;
      retain a named owner for any deferred adoption decision.
- [ ] For a selected parent, seed bounded runtime/config/UI/publication tasks
      with the spatial-query reuse decision and tests; update its matrix before
      retiring this intake. Record explicit disposition for unselected or
      rejected parents without promoting them.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```
