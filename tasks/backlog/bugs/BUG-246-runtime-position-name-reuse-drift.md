---
id: BUG-246
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: bounded existing-CI regression; evidence is the unchanged failing guard and reviewed one-line correction
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# BUG-246 — Runtime position name bypasses the canonical constant

## Goal
Restore the existing CPU gate while implementing HARDEN-091. The default-branch
[full-cpu run](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38960072158)
at `ffe7e8372` fails only
`RuntimeReuseDriftGuard.RuntimeUsesTheSharedFiniteCheckAndPositionName` out of
4,046 cases. `Runtime.MeshTopologyOperations.Topology.cpp` constructs its output
property with a string literal despite already importing the canonical owner.

## Acceptance criteria
- [ ] Use `GeometrySources::PropertyNames::kPosition` at the reported output construction without changing behavior or weakening the guard.
- [ ] The same guard fails on the original source and passes after the correction; include the affected runtime build in HARDEN-091's combined verification.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -R '^RuntimeReuseDriftGuard.RuntimeUsesTheSharedFiniteCheckAndPositionName$'
```

The original failure was reproduced locally in this session. CI and local logs
are temporarily retained under ignored `tasks/evidence/BUG-246/`; this is a
source reuse regression, not an environmental failure. Independent review and
the integrated build remain pending.
