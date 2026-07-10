---
id: BUG-071
theme: G
depends_on: []
---
# BUG-071 — Sim-systems registered during OnResolve bypass FinalizeForBoot

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
- The sim-system registrar is wired into the `EngineSetup` for **both** boot
  phases: `OnRegister` (`Runtime.Engine.cpp:243-247`) and `OnResolve`
  (`:288-292`). But `RuntimeModuleSchedule::FinalizeForBoot()` runs exactly once
  (`Runtime.Engine.cpp:273`), **between** the register loop and the resolve loop
  (`ResolveRuntimeModulesForBoot` at `:276`).
- A sim-system registered during `OnResolve` (a plausible pattern: resolve a
  service, then conditionally register a system) is appended to `m_SimSystems`
  after the sort. It is never topologically ordered, its `WaitForSignals` is
  never validated, and it escapes cycle/duplicate detection. If an `OnRegister`
  system waits on a signal such a late system emits, it is inserted before its
  signaler and the substep fails to compile every tick (see `BUG-069`).

## Required changes
- [ ] Choose and implement one: (a) run `FinalizeForBoot()` after the resolve
      phase so late-registered systems are included in the sort/validation, or
      (b) reject sim-system registration during `OnResolve` with a clear
      diagnostic (frame-hooks/services only in resolve), or (c) re-finalize if
      any system was added during resolve. Document the chosen contract.
- [ ] Ensure the chosen behavior composes with the `BUG-070` duplicate/cycle
      validation (whichever runs must see the full system set).

## Tests
- [ ] A module that registers a sim-system in `OnResolve` either has it correctly
      ordered/validated, or is rejected with a diagnostic — assert the chosen
      contract.
- [ ] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [ ] Document in the feature-module playbook which registrations are legal in
      `OnRegister` vs `OnResolve`.

## Acceptance criteria
- [ ] No sim-system can reach execution without passing through ordering and
      validation.
- [ ] The `OnResolve` registration contract is explicit and tested.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not silently accept an unordered/unvalidated sim-system.
- Mixing mechanical file moves with semantic refactors.
