# <METHOD-ID> — <Method task title>

## Goal
- 

## Non-goals
- 

## Context
- Paper/method: 
- Method package: `methods/<domain>/<method_id>/`

## Required changes
- [ ] Intake paper + define method contract.
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

