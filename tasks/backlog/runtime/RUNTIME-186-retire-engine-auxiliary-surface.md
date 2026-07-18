---
id: RUNTIME-186
theme: F
depends_on:
  - RUNTIME-185
maturity_target: Operational
---
# RUNTIME-186 — Retire residual Engine auxiliary surface

## Goal
- Make the final semantic public-API decisions for residual Engine
  observation/setup conveniences and migrate their callers before the
  mechanical PImpl and exact-checker ratchet.

## Non-goals
- No Engine PImpl, implementation-import relocation, convergence-checker
  redesign, or exact import/getter policy update; `RUNTIME-187` owns that
  mechanical leaf.
- No domain-owner extraction. If a SceneEditing, Camera, ConfigControl,
  AssetWorkflow, EditorUi, or AsyncWork facade remains, correct its owning
  predecessor rather than absorbing it here.
- No new umbrella telemetry service, service locator, compatibility re-export,
  callback registry, or generic setup facade.
- No new runtime feature, renderer pass, config field, editor behavior,
  composition mechanism, or world-switch policy.

## Context
- Owner/layer: runtime kernel caller-facing API and app/runtime composition
  boundary.
- The behavior owners, Sandbox composition, application lifecycle, and
  composition-mechanism deletion test have landed. The remaining API choices
  are semantic because they decide how real callers observe kernel state or
  register kernel-consumed input actions.
- At ADR-0027 intake, `Runtime.Engine` re-exported
  `Runtime.FramePacingDiagnostics` and `Runtime.InputActions`, exposed
  frame-pacing/render-extraction observations through convenience getters, and
  registered input actions directly through Engine. Re-export removal and
  caller migration are reviewable API work, not a side effect of hiding
  implementation state.
- Render extraction and frame pacing remain frame-loop/kernel responsibilities.
  That classification does not require Engine to forward every diagnostic.
  Input-action dispatch and the capture carrier also remain frame-loop-owned;
  app-composed owners receive only the narrow registration capability their
  production behavior proves.

## Required changes
- [ ] Inventory every remaining public Engine declaration and production/test
      caller after `RUNTIME-185`; classify each as kernel control, kernel
      observation/setup, test-only seam, or residual domain facade.
- [ ] If the inventory finds a residual domain facade or owner import, stop
      and correct the responsible behavior-owner task. Record that correction
      rather than broadening this auxiliary-surface slice.
- [ ] Remove the two Engine re-exports. Make users that name
      `RuntimeFramePacingDiagnostics` or runtime input-action records import
      their owning modules explicitly.
- [ ] Remove Engine forwarding getters for frame-pacing/render-extraction
      observations where callers can use an already-landed owning snapshot or
      narrow read-only composition capability. If a production caller still
      needs an observation, expose the smallest direct value/snapshot surface
      at the kernel owner; do not introduce a catch-all telemetry facade.
- [ ] Route production input-action registration through the narrow
      behavior-backed setup capability used by app-composed owners. Keep the
      action registry and dispatch in the frame loop. If no production owner
      registers an action after the app migration, delete the public
      registration capability instead of preserving it for tests.
- [ ] Move test-only diagnostic and event-replay callers to explicit test
      seams or owning component snapshots; do not keep a public Engine
      convenience solely for a fixture.
- [ ] Preserve the frame-loop-owned input-capture value: reset once per frame,
      borrowed by each ephemeral hook context, completed by EditorUi before
      later behavior hooks and input-action dispatch read it.
- [ ] Preserve the composition contract accepted by `RUNTIME-185`, queued
      command/event pump order, world/job teardown, renderer sequencing, and
      direct-dispatch prohibition.

## Tests
- [ ] Add focused contract coverage for every retained kernel observation or
      setup capability and structural coverage that Engine re-exports remain
      absent.
- [ ] Preserve frame-pacing/render-extraction diagnostics, input-action
      registration/dispatch/capture gating, event replay, Sandbox composition,
      Null/headless omission, and app lifecycle behavior.
- [ ] Run all focused runtime/app coverage, strict layering and convergence
      checks, and the complete default CPU-supported gate.
- [ ] Run ASan and UBSan CPU variants because public caller and capability
      ownership changes can expose lifetime errors.
- [ ] Configure/build `ci-vulkan` and rerun the Sandbox/object-space-normal
      Vulkan smoke cohort so caller migration cannot invalidate
      `RUNTIME-129` evidence.

## Docs
- [ ] Update ADR-0024/0027-linked runtime architecture, feature-module
      playbook, kernel target state, app documentation, and affected source
      READMEs.
- [ ] Record the before/after public declarations and where each removed
      observation/setup caller now obtains its capability.
- [ ] Regenerate the module inventory for changed module surfaces.

## Acceptance criteria
- [ ] Engine re-exports neither frame-pacing nor input-action records, and
      callers import owning modules explicitly.
- [ ] Engine exposes no residual domain facade and no auxiliary forwarding
      getter or registration convenience retained only for tests.
- [ ] Every retained kernel observation/setup surface has a named production
      caller, is read-only or narrowly mutating as appropriate, and is smaller
      than a generic Engine/telemetry facade.
- [ ] Input actions and the coherent capture value remain frame-loop-owned
      without exposing Engine or ImGui types through module/app behavior.
- [ ] The public Engine declarations and all callers are semantically settled
      so `RUNTIME-187` can change only representation and exact structural
      policy.
- [ ] Canonical Sandbox, CPU, ASan, UBSan, and Vulkan smoke gates pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'FramePacing|RenderExtraction|InputAction|InputCapture|RuntimeSandboxAcceptance|EngineLayering|KernelConvergence' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
- Hiding a behavior-owner correction inside this residual API slice.
- Adding a generic `EngineTelemetry`, `EngineServices`, or compatibility
  forwarding facade.
- Putting Engine behind PImpl or changing exact checker policy.
- Reintroducing a composition method removed by `RUNTIME-185`.
- Changing frame behavior while migrating observation/setup callers.

## Maturity
- Target: `Operational`; the settled public API must drive the canonical
  Sandbox and pass CPU, sanitizer, and Vulkan-smoke behavior/lifetime gates.
