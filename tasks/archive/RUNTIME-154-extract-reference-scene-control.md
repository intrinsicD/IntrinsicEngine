---
id: RUNTIME-154
theme: F
depends_on: [RUNTIME-153]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-154 — Extract reference-scene lifecycle control out of Engine

## Goal
- Move reference-scene install state, provider resolution, camera-seed caching, and provider teardown out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving the existing `Engine` facade.

## Non-goals
- Changing reference-scene provider semantics, selectors, strict terminate guards, or triangle payload contents.
- Changing `Engine::GetReferenceSceneRegistry()`, `Engine::IsReferenceSceneInstalled()`, or `Engine::GetReferenceCameraSeed()` public behavior.
- Moving camera-controller ownership or changing camera finalization math.
- Retiring `Extrinsic.Runtime.ReferenceScene` or replacing its provider registry contract.

## Context
- Owner: `runtime`; `Engine` remains the concrete composition root, but provider lookup/population bookkeeping and teardown details are reference-scene policy, not general engine lifecycle code.
- `ReferenceSceneRegistry` must remain reachable through `Engine` before `Initialize()` so tests and downstream composition can pre-register providers.
- `Engine::Initialize()` currently resolves providers, stores `ReferenceScenePopulation`, copies the camera seed, and flips an installed flag inline.
- `Engine::Shutdown()` currently repeats provider-specific teardown by carrying registry, population, camera seed, selector, enabled state, and installed flag through the shutdown hook struct.
- This follows the RUNTIME-146 through RUNTIME-153 decomposition pattern: keep stable public facade methods on `Engine`, move subsystem-local policy and state behind a runtime-owned module.

## Required changes
- [x] Add a new runtime module that owns reference-scene registry, population, installed state, install-if-enabled, teardown-if-installed, and camera-seed access.
- [x] Update `Runtime.Engine.cppm` to store the new reference-scene control object instead of `ReferenceScenePopulation`, the optional camera seed, and the installed flag.
- [x] Update `Runtime.Engine.cpp` so initialize, shutdown, `GetReferenceSceneRegistry()`, `IsReferenceSceneInstalled()`, and `GetReferenceCameraSeed()` delegate to the new module.
- [x] Keep `Engine` public API compatibility for callers that pre-register providers or inspect the reference-scene state.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Update the runtime layering/source-contract test to prove provider resolution, population storage, and provider teardown details no longer live in `Runtime.Engine.cpp`.
- [x] Run focused runtime reference-scene and Engine-layering tests.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document the new reference-scene control module and revise the Engine / ReferenceScene descriptions.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` has no `ReferenceScenePopulation`, optional reference-camera storage, or reference-scene installed flag members.
- [x] `Runtime.Engine.cpp` does not name `IReferenceSceneProvider`, `RegisterDefaultReferenceProvidersIfAbsent`, `ReferenceScenePopulation`, or `provider.Populate` / `provider->Teardown` directly.
- [x] Reference-scene tests still prove default-disabled behavior, pre-registered provider install, default triangle install, selectable reference triangle, render extraction, and shutdown reset.
- [x] The new module owns the same strict double-install and missing-provider behavior previously reached through Engine.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Status
- Completed on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.ReferenceSceneControl` now owns the reference-scene registry, installed population, camera seed, install-if-enabled flow, and teardown-if-installed flow.
- `Runtime.Engine` keeps the public reference-scene facade for callers that pre-register providers or inspect installed state, but initialize/shutdown/accessor behavior delegates to the control object.
- `Runtime.Engine.cpp` no longer names provider resolution, provider interface, population storage, or provider populate/teardown calls directly.
- Root hygiene still reports the pre-existing warning-mode entries `ara/` and `imgui.ini`; no task-state or root-hygiene cleanup was part of this slice.
- PR/commit: pending.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ReferenceScene|RuntimeEngineLayering|RuntimeSandboxAcceptance' --timeout 90
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing this extraction with reference-scene feature changes, new selectors, or new provider payloads.
- Changing camera-controller or renderer behavior beyond the existing camera-seed delegation.
- Moving provider implementation out of `Extrinsic.Runtime.ReferenceScene`.
- Introducing app/editor-specific reference-scene policy into the new runtime module.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the Engine initialize/run/shutdown path delegates reference-scene lifecycle to the new module and the focused runtime reference-scene tests plus default CPU gate pass.
