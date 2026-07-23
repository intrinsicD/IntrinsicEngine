---
id: RUNTIME-187
theme: F
depends_on:
  - RUNTIME-186
maturity_target: Operational
---
# RUNTIME-187 — Finalize the exact domain-free Engine surface

## Status

- Retired on 2026-07-23 at `Operational` on
  `codex/arch-014-kernel-convergence-program` after `RUNTIME-186` retired.
- Every Engine field now lives behind `Engine::Impl`; no domain owner moved
  into that storage, and the public declarations accepted by `RUNTIME-186`
  are unchanged. The one affected Vulkan smoke now imports its owning
  `Extrinsic.Graphics.RenderGraph` module explicitly instead of relying on
  accidental transitive reachability through Engine.
- The final exact policy is `12/0/0/5`: twelve declaration-required plain
  imports, zero domain imports, zero re-exports, and five kernel getters whose
  names, return types, owning types, and owning imports are all ratcheted.
  Same-count import substitutions and getter-type drift fail synthetic tests.
- Focused runtime/app coverage passed 54/54 and checker regressions passed
  22/22. The complete CPU selector passed 4,269/4,269; fresh ASan and UBSan
  selectors each passed 2,923/2,923; and the promoted Vulkan intersection
  passed 48/48, including the 96.96-second shutdown LeakSanitizer contract.
  Strict layering reported 753 files, 6,767 references, and zero violations.
- Commit: pending this retirement checkpoint.

## Goal
- Close runtime convergence with a representation-only Engine PImpl change and
  an exact structural ratchet derived from the already-settled public kernel
  API.

## Non-goals
- No public Engine declaration removal, caller migration, lifecycle or
  composition semantic change, domain-owner correction, or new capability.
- No compatibility re-export, generic service/telemetry facade, or numerical
  import budget.
- No new runtime feature, renderer pass, config field, editor behavior, or
  world-switch policy.

## Context
- Owner/layer: runtime kernel public/implementation boundary and its structural
  convergence guard.
- `RUNTIME-186` has settled the public declarations and migrated every caller.
  Zero domain facades/re-exports and no pending caller migration are
  preconditions; if either is false, correct the earlier semantic task rather
  than expanding this leaf.
- ADR-0027 records a candidate set of twelve kernel imports, but explicitly
  requires the implementation to verify and shrink it. The accepted result is
  the exact set required by the final public API, whatever its measured count;
  an unused candidate import fails this task.
- Moving implementation state behind opaque storage is deliberately isolated
  because it can remove implementation-only imports without deciding what the
  public API means.

## Required changes
- [x] Confirm the precondition: the landed Engine API has no domain facade,
      domain re-export, `Engine&` composition leak, old application callback,
      or outstanding production/test caller migration.
- [x] Put all Engine implementation state behind opaque implementation storage
      so implementation-only imports do not enter `Runtime.Engine.cppm`.
- [x] Inventory every settled public Engine declaration and derive the exact
      required import and allowed-kernel-getter/type sets from those
      declarations; remove every unused candidate import.
- [x] Preserve every public declaration and behavior accepted by
      `RUNTIME-186`; if a declaration must change, stop and re-scope that
      semantic change outside this task.
- [x] Extend the convergence checker with exact allowed-kernel-getter and
      owning-type classification, then lower its policy in the same commit to
      the measured plain-import/re-export/getter sets and zero domain
      imports/facades.
- [x] Update structural tests to fail on an unused/new Engine import, domain
      re-export/facade, `Engine&` composition leak, stale policy entry, or
      policy regrowth.

## Tests
- [x] Add focused structural regressions for exact imports, allowed kernel
      getters/types, re-exports, no domain surface, and no `Engine&`.
- [x] Preserve module boot/wiring/hook/shutdown ordering, Sandbox composition,
      Null/headless omission, and application lifecycle behavior.
- [x] Run all focused module/runtime/app coverage, strict layering and
      convergence checks, and the complete default CPU-supported gate.
- [x] Run ASan and UBSan CPU variants because PImpl changes composition-root
      storage and teardown representation.
- [x] Configure/build `ci-vulkan` and rerun the Sandbox/object-space-normal
      Vulkan smoke cohort so storage relocation cannot invalidate
      `RUNTIME-129` evidence.

## Docs
- [x] Update ADR-0024/0027-linked runtime architecture, feature-module
      playbook, kernel target state, app documentation, and module inventory.
- [x] Record exact before/after Engine metrics and the final public import,
      getter/type, and re-export sets.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` contains exactly the imports required by its
      settled public kernel API, no unused imports, zero domain imports, zero
      domain re-exports, and zero domain-facade getters.
- [x] Engine implementation state is opaque and no domain owner is hidden
      behind that storage.
- [x] No public declaration or behavioral caller changes in this task; the PImpl and
      checker patch preserves the `RUNTIME-186` semantic API and already-pruned
      composition behavior.
- [x] Every allowed getter/type and import has a declaration-backed reason;
      the policy contains no capacity for unrelated future surface.
- [x] Canonical Sandbox, CPU, ASan, UBSan, and Vulkan smoke gates pass with the
      exact ratchet green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|RuntimeApplication|RuntimeSandboxAcceptance|EngineLayering|KernelConvergence' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Sandbox|ObjectSpaceNormal.*Bake' --no-tests=error --timeout 180
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Removing or adding a public Engine declaration or migrating one of its
  callers.
- Retaining an import/getter/type/re-export only to match a preselected count.
- Moving a domain responsibility behind Engine PImpl.
- Reintroducing a composition method removed by `RUNTIME-185`.
- Mixing new behavior or mechanism semantics with the boundary ratchet.

## Maturity
- Target: `Operational`; the exact surface must drive the canonical Sandbox
  and pass CPU, sanitizer, and Vulkan-smoke ownership/teardown gates.
