---
id: BUG-071
theme: G
depends_on: []
---
# BUG-071 — Sim-systems registered during OnResolve bypass FinalizeForBoot

## Status
- Completed 2026-07-13 at `CPUContracted` on branch
  `codex/bug-071-resolution-retirement`.
- Commit: this local closure commit.
- Production fix `a1704053` is already present on `main`; this closure slice
  adds the missing pre-fix reproduction, real-engine regression, contract
  documentation, and retirement evidence.

## Goal
- Ensure every module sim-system is topologically ordered and signal/cycle/
  duplicate-validated, regardless of whether it is registered during `OnRegister`
  or `OnResolve`.

## Non-goals
- No change to the `EngineSetup` surface offered to modules.
- No change to the two-phase register/resolve boot model itself.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.Engine.cpp`,
  `src/runtime/Runtime.ModuleSchedule.*`.
- At the exact pre-fix parent `7e77e47f`, the sim-system registrar was wired
  into `EngineSetup` for **both** boot phases, but
  `RuntimeModuleSchedule::FinalizeForBoot()` ran exactly once between the
  `OnRegister` and `OnResolve` loops. A resolve-registered system was therefore
  appended after the sort and escaped signal, cycle, and duplicate validation.
- Production commit `a1704053` chose contract (a): keep resolve-time system
  registration legal and move the single finalization to the end of
  `ResolveRuntimeModulesForBoot()`, after the full contribution set and service
  validation are complete.
- The closure regression registers one valid `(module, system)` identity in
  `OnRegister` and the same identity in `OnResolve`. On `7e77e47f`, engine boot
  incorrectly returned in five of five repetitions; current production must
  reject the combined set before `Initialize()` returns.

## Required changes
- [x] Choose and implement one: (a) run `FinalizeForBoot()` after the resolve
      phase so late-registered systems are included in the sort/validation, or
      (b) reject sim-system registration during `OnResolve` with a clear
      diagnostic (frame-hooks/services only in resolve), or (c) re-finalize if
      any system was added during resolve. Document the chosen contract.
- [x] Ensure the chosen behavior composes with the `BUG-070` duplicate/cycle
      validation (whichever runs must see the full system set).

## Tests
- [x] A module that registers a sim-system in `OnResolve` either has it correctly
      ordered/validated, or is rejected with a diagnostic — assert the chosen
      contract.
- [x] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [x] Document in the feature-module playbook which registrations are legal in
      `OnRegister` vs `OnResolve`.

## Acceptance criteria
- [x] No sim-system can reach execution without passing through ordering and
      validation.
- [x] The `OnResolve` registration contract is explicit and tested.
- [x] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Closure evidence collected on 2026-07-13:

- The exact pre-fix parent `7e77e47f` built with the Clang 23 `ci` preset and
  ASan/UBSan enabled. The new
  `RuntimeModule.ResolveRegisteredDuplicateJoinsFullBootValidationSet`
  regression failed five of five repetitions with `Result: failed to die`,
  proving `Engine::Initialize()` returned after the late duplicate bypassed
  finalization.
- Production commit `a1704053` is an ancestor of the closure base
  `88f9f677`; it moves the only `FinalizeForBoot()` call from the end of
  registration to the end of resolution. The same regression is the direct
  before/after discriminator.
- Against the production fix, the regression passed five of five repetitions
  under the Clang 23 `ci` preset with ASan/UBSan. The focused `RuntimeModule`
  CTest selection passed 13/13.
- `IntrinsicTests` built successfully and the complete default CPU-supported
  gate passed 3,640/3,640 tests in 392.32 seconds.
- Strict layering, test-layout, task-policy, task-state-link, documentation-
  link, documentation-sync, session-brief freshness, PR-contract, and diff-
  whitespace checks pass. Root hygiene also completed in its configured
  warning mode with the pre-existing `ara/` allowlist finding.

## Completion

- Completed: 2026-07-13. Maturity: `CPUContracted`.
- Outcome: sim-system contributions remain legal through `OnResolve`, and the
  single boot finalization validates the complete register-plus-resolve set
  before any scheduled pass can execute.
- This is a backend-neutral boot/schedule contract, so no `Operational`
  follow-up is owed.

## Forbidden changes
- Do not silently accept an unordered/unvalidated sim-system.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; module boot and schedule validation are
  backend-neutral CPU contracts with no GPU or host-capability path.
