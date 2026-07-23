---
id: RUNTIME-185
theme: F
depends_on:
  - RUNTIME-184
  - RUNTIME-129
maturity_target: Operational
---
# RUNTIME-185 — Prune unproven runtime-composition mechanisms

## Status

- Retired on 2026-07-23 at `Operational` on
  `codex/arch-014-kernel-convergence-program` after `RUNTIME-184` retirement.
- Production inventory: nine runtime-owned modules plus the Sandbox-local
  frame-pacing capture module; seven frame-hook registrations across
  `EditorUiModule`, `TextureBakeModule`, `SceneInteractionModule`, and the
  Sandbox capture; two viewport-input hooks across `CameraModule` and
  `SceneInteractionModule`; zero sim-system registrations.
  `OnResolve`/`Require` remain behavior-backed by cross-owner dependencies.
- Deletion decision: remove the sim-system registrar/context/descriptor,
  causal signal DAG and fixed-step schedule branch; remove the unused
  `AfterCommandDrain` phase, hook-context phase echo, resolve-phase registrars,
  redundant built-in service provisions, and test-only registry statistics.
  Retain two-phase service resolution plus the exact frame/viewport hooks.
- Focused module/setup/service/schedule and Sandbox coverage passed 35/35 and
  the broader named cohort passed 22/22. The default CPU selector exercised
  4,270 cases; its sole stale source-shape expectation was repaired and rerun
  cleanly. The fresh ASan and UBSan selectors each passed 2,924/2,924, and the
  promoted Vulkan Sandbox/object-space-normal cohort passed 21/21 including
  shutdown LeakSanitizer. Strict layering and the generated module inventory
  are clean.
- Commit: pending this slice's implementation checkpoint.

## Goal
- Re-audit the landed production composition graph and remove or narrow every
  `IRuntimeModule`, `EngineSetup`, `ServiceRegistry`, and
  `RuntimeModuleSchedule` feature that still has no production consumer.

## Non-goals
- No residual Engine getter/re-export decision or caller migration;
  `RUNTIME-186` owns that semantic API slice.
- No Engine PImpl, implementation-import relocation, or checker/policy
  redesign; `RUNTIME-187` owns that mechanical final surface ratchet.
- No new runtime behavior, app feature, registrar, scheduler, service locator,
  or alternative composition framework.
- No removal of a hook/service/lifecycle operation used by a named production
  owner.
- No test-only consumer accepted as justification for production surface.

## Context
- Owner/layer: runtime composition mechanism.
- The six ADR-0027 owners, Sandbox app composition, operational normal bake,
  and explicit app lifecycle have landed, so this task evaluates actual final
  consumers rather than task intentions.
- At ADR-0027 intake, `ClusteringModule` was the only production implementor;
  `OnResolve`, `Require`, sim-system registration, and every non-empty schedule
  path were unproven. The behavior-owner tasks may have changed those counts.
- Frame-hook use does not automatically justify sim-system registration or a
  causal DAG. Typed `Provide`/`Find` use does not automatically justify
  redundant built-in provisions or a mandatory resolve phase.

## Required changes
- [x] Record every production implementor/caller/provider/finder/requirement,
      frame hook, sim system, and wait/signal edge for the four flagged
      surfaces; classify tests separately.
- [x] Retain the lean type-erased lifecycle operations actually used by
      app-composed owners and remove optional lifecycle phases without a
      production caller.
- [x] Narrow `EngineSetup` to the capabilities and registrars used by the
      landed owners; do not keep sim-system or phase vocabulary for roadmap
      work.
- [x] Remove redundant service provisions with no production lookup consumer.
      Retain `Require`/`OnResolve` only if a real cross-owner dependency proves
      order-independent two-phase resolution and its missing-provider failure.
- [x] Retain the smallest frame-hook scheduling used by production. Remove
      sim-system registration, topological signal machinery, validation, and
      records independently when no real production system/edge uses them.
- [x] Update module/setup/service/schedule contract tests to cover the
      retained behavior and deletion boundary without manufacturing a
      production-like test consumer.
- [x] Preserve registration failure, stable identity, shutdown announcement,
      reverse shutdown, and any behavior-backed hook/service ordering.

## Tests
- [x] Focused module/setup/service/schedule tests prove every retained branch
      and the fail-closed invalid registration/dependency cases that remain.
- [x] Canonical Sandbox integration proves all composed owners boot, execute
      their real hooks/services, and shut down after the pruning.
- [x] Run strict layering, the complete default CPU-supported gate, and ASan/
      UBSan variants because exported lifecycle records and teardown paths may
      shrink.
- [x] Configure/build `ci-vulkan` and rerun the Sandbox/object-space-normal
      smoke cohort so pruning exported setup/module records cannot silently
      invalidate the already-landed Vulkan fixtures.

## Docs
- [x] Update runtime architecture, ADR-0027 validation evidence, feature-module
      guidance, and runtime source README with exact retained consumer counts.
- [x] Regenerate the module inventory for every changed `.cppm` surface.

## Acceptance criteria
- [x] Every retained composition method/type/branch names at least one
      production consumer and passes its deletion test.
- [x] No `Require`/`OnResolve`, sim-system registrar, causal-DAG branch,
      redundant provision, or schedule record remains for tests or hypothetical
      work alone.
- [x] App composition, module behavior, failure diagnostics, and teardown
      remain Operational through the Sandbox path.
- [x] No replacement abstraction or Engine domain facade is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|EngineSetup|ServiceRegistry|ModuleSchedule|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Retaining a production surface because a test double calls it.
- Deleting a behavior-backed frame hook/service merely to minimize counts.
- Replacing the current mechanisms with another interface/registry/scheduler.
- Mixing Engine PImpl/import/getter/checker work into this semantic slice.

## Maturity
- Target: `Operational`; the pruned mechanism must still compose and execute
  every landed owner through the canonical Sandbox lifecycle.
