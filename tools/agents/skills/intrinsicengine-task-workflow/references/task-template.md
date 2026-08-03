---
id: <TASK-ID>
theme: <theme letter from tasks/backlog/README.md, or `none`>
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: <why no catalog contract applies; remove when contracts are declared>
---
# <TASK-ID> — <Task title>

## Goal
- 

## Non-goals
- 

## Context
- 

<!--
Optional. Add when the task changes feature control surfaces or backend
selection policy.

## Control surfaces
- Config:
- UI:
- Agent/CLI:

## Backends
- Backend axis: present, not applicable, or deferred to <TASK-ID>.
-->

<!--
Required when `method.engine-integration` is declared. For geometry methods,
describe typed property domains rather than container/provenance shorthand:
point-set methods accept compatible properties on any element domain; graph
methods add only named adjacency/property requirements and therefore include
meshes satisfying them. State same-domain publication separately from explicit
topology/cardinality edits. Do not use `Vertices` or `VertexProperty` as an
eligibility boundary.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | |
| Compatible entity sources | |
| RuntimeModule | |
| Config/agent | |
| UI | |
| Publication | |
| End-to-end tests | |
-->

## Required changes
- [ ] <required change>

## Tests
- [ ] <test or verification requirement>

## Docs
- [ ] <documentation update>

## Acceptance criteria
- [ ] <acceptance criterion>

## Verification
```bash
# Add concrete commands for this task.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

<!--
Optional. Add `## Maturity` when the stop-state is ambiguous (rendering,
Vulkan, asset ingest, hot reload, pass command bodies, runtime composition,
legacy retirement). Levels are defined in docs/agent/task-maturity.md:
Scaffolded, CPUContracted, Operational, ParityProven, Retired. Example:

## Maturity
- Target: Operational on Vulkan-capable hosts; CPUContracted everywhere else.
- This slice closes Scaffolded → CPUContracted. Operational owned by <TASK-ID>.
- If CPUContracted is the intended endpoint, state: no Operational follow-up is owed.
-->
