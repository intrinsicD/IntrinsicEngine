---
id: <METHOD-ID>
theme: <theme letter from tasks/backlog/README.md, or `none`>
depends_on: []
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - method.engine-integration
  # Add applicable domain contracts from docs/architecture/contract-catalog.yaml.
---
# <METHOD-ID> — <Method task title>

## Goal
- 

## Non-goals
- 

## Context
- Original paper:
- Extensions/improvements reviewed:
- Implemented formulation and exclusions:
- Method package: `methods/<domain>/<method_id>/`

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | <point/vertex, graph, mesh, field, image, ...> |
| Compatible entity sources | <all compatible ECS sources, or N/A with contract reason> |
| RuntimeModule | <binding in this task or named follow-up task> |
| Config/agent | <shared validated control path or named follow-up task> |
| UI | <appropriate domain panels or named follow-up task> |
| Publication | <target entity/domain/property and cardinality/topology policy> |
| End-to-end tests | <domain matrix tests or named follow-up task> |

## Required changes
- [ ] Complete literature intake for the original paper plus relevant extensions/improvements and define the selected method contract.
- [ ] Implement CPU reference backend first.
- [ ] Add correctness tests.
- [ ] Add benchmark harness/manifests.
- [ ] Document diagnostics and known limitations.

## Tests
- [ ] <method test requirement>

## Docs
- [ ] <method documentation update>

## Acceptance criteria
- [ ] CPU reference implementation is present and tested.
- [ ] Benchmarks and manifests are present or explicitly stubbed.
- [ ] Numerical limitations and diagnostics are documented.
- [ ] Every engine-integration row is implemented, explicitly inapplicable, or owned by a named follow-up task.

## Verification
```bash
# Add concrete method verification commands.
```

## Forbidden changes
- Adding optimized CPU or GPU backend before reference parity.
- Claiming performance wins without baseline comparison.

<!--
Method workflow maps directly onto the maturity taxonomy in
docs/agent/task-maturity.md:
  1. Intake + contract           → Scaffolded
  2. CPU reference + correctness → CPUContracted
  3. Benchmark harness/manifests → CPUContracted (with baseline)
  4. Optimized CPU backend       → Operational (CPU)
  5. GPU backend after parity    → Operational (GPU) + ParityProven
Record the intended endpoint in an optional `## Maturity` section when the
method task stops earlier than reference parity.
-->
