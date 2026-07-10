---
id: RUNTIME-168
theme: F
depends_on:
  - CI-003
  - RUNTIME-144
maturity_target: Operational
---
# RUNTIME-168 — Privatize the Sandbox default policies surface

## Goal
- Replace the low-fanout `Extrinsic.Runtime.SandboxDefaultPolicies` module
  surface with private runtime/app glue while preserving the existing default
  import, input-action, and post-import policy behavior.

## Non-goals
- No default policy behavior changes and no new sandbox UX.
- No movement of asset, ECS, graphics, or runtime ownership into `app`.
- No change to `Runtime.AssetImportPipeline` public semantics.

## Context
- Owner/layer: `runtime` for default policy registration; `app/Sandbox` only
  composes the returned handles.
- Local 2026-07-10 triage measured `Runtime.SandboxDefaultPolicies.cppm` at up
  to 53.135s despite a 37-line interface with two imports and one production
  consumer (`src/app/Sandbox/Sandbox.cppm`), plus tests.
- `RUNTIME-144` retired the policy seam; this task only changes whether the
  seam is exported as a module.

## Required changes
- [ ] Inventory which declarations need app/test visibility and which can stay
      implementation-local.
- [ ] Convert the module surface to private header/source glue or fold it into
      the sandbox composition path, keeping the policy implementation in
      `runtime`.
- [ ] Update app and test consumers without introducing lower-layer imports in
      `app/Sandbox`.
- [ ] Remove the `.cppm` from runtime CMake module file sets if no module
      surface remains.
- [ ] Record before/after compile timing and module/import counts.

## Tests
- [ ] Run sandbox editor, asset import format coverage, and runtime input-action
      tests that exercise default policy registration/unregistration.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update `src/app/Sandbox/README.md`, `src/runtime/README.md`, and generated
      module inventory if the module surface is removed.

## Acceptance criteria
- [ ] Sandbox default policy behavior and returned-handle cleanup are unchanged.
- [ ] The default policy seam is no longer a broad exported module surface unless
      measurements show the module should remain.
- [ ] `app` still depends on `runtime` only.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeInputActions|AssetImportFormatCoverage|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Reintroducing app ownership of assets, ECS, graphics, or renderer state.
- Changing default policy semantics while changing module/header shape.
- Adding a public compatibility module solely to preserve the old import name.

## Maturity
- Target: `Operational`; the default policies remain exercised through the
  sandbox runtime path, so no `Operational` follow-up is owed.
