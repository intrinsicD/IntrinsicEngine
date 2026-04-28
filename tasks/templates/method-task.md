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
- 

## Docs
- 

## Acceptance criteria
- CPU reference implementation is present and tested.
- Benchmarks and manifests are present or explicitly stubbed.
- Numerical limitations and diagnostics are documented.

## Verification
```bash
# Add concrete method verification commands.
```

## Forbidden changes
- Adding optimized CPU or GPU backend before reference parity.
- Claiming performance wins without baseline comparison.
