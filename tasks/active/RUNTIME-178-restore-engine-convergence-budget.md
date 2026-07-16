---
id: RUNTIME-178
theme: F
depends_on:
  - HARDEN-085
maturity_target: Operational
---
# RUNTIME-178 — Restore the pre-ratchet Runtime.Engine convergence budget

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- Design audit complete; next gate: implement the two opaque Engine members,
  registered render-extraction cache query, and exact policy ratchet.

## Goal

- Restore `Runtime.Engine.cppm` to at most the fixed 2026-07-13
  legacy-interim convergence budget of 43 plain imports / 23 domain imports
  under the exact v1 checker and remove the temporary
  `GetMaterialTextureAssetBindings` Engine getter without reversing the
  compile-surface reductions that exposed the debt.

## Non-goals

- No resurrection or re-export of retired `ImGuiEditorBridge`,
  `RenderExtractionService`, or `AssetResidencyService` module/BMI surfaces.
- No broad pimpl conversion selected before a consumer/lifetime audit.
- No UV-view, asset-residency, render-extraction, or ImGui behavior changes.

## Context

- Owner/layer: runtime composition under ADR-0024 and the `ARCH-014` kernel
  convergence umbrella.
- Current branch debt relative to the fixed 2026-07-13 budget is `+6` plain
  imports, `+5` exact allowlist-complement domain imports, and one public
  getter name. The historical 23 used an unanchored interim classifier; the
  exact v1 checker preserves 23 as a budget rather than retroactively changing
  history. `HARDEN-085` records and prevents growth from the current state.
- The import delta arose while retiring low-fanout service BMIs into private
  Engine module glue; the getter delta arose from the `GRAPHICS-122` UV-view
  material-binding query. The fix must preserve both delivered behaviors and
  decide the simplest honest ownership shape after tracing actual consumers.
- The 2026-07-16 consumer/import audit selected this bounded shape:
  `ImGuiEditorBridge` and `AssetResidencyService` become opaque `unique_ptr`
  members at their existing Engine member positions; `RenderExtractionService`
  stays by value because its `RenderExtraction` and `RenderWorldPool` imports
  are independently required by public Engine APIs.
- Register the existing Engine-owned `RenderExtractionCache` instance in the
  existing `ServiceRegistry`. `SandboxEditorFacades` can resolve that stable
  cache once per prepared context for the UV command instead of adding a new
  Engine callback/accessor/wrapper. The registry dies before the service, the
  Engine is non-movable, and copied facade callbacks remain attachment-epoch
  guarded.
- Remove all three redundant query facades in the same ownership seam:
  `FindSurfaceGpuGeometry`, `GetMaterialTextureAssetBindings`, and the
  test-only `GetMaterialTextureAssetBindingsForTest`. Existing tests can query
  the registered cache directly. Expected exact result is approximately
  `49 -> 42` plain imports, `28 -> 21` domain imports, and `33 -> 31` getter
  names, with two re-exports unchanged.

## Right-sizing

- Two focused opaque members remove five otherwise-private declaration imports
  while preserving each member's position and teardown order. Pimpling
  `RenderExtractionService` buys no import reduction and is rejected.
- Reuse the existing `ServiceRegistry` and existing `RenderExtractionCache`;
  do not add a UV callback, dependency record, wrapper service, registry, BMI,
  or aggregate Engine pimpl.

## Required changes

- [x] Audit the direct dependencies needed for the three private Engine
      declaration headers and the `GRAPHICS-122` UV-view material-binding
      query, including lifetime and facade consumers.
- [ ] Make `ImGuiEditorBridge` and `AssetResidencyService` opaque Engine-owned
      `unique_ptr` members at their existing positions; construct and destroy
      them out of line without changing lifecycle order.
- [ ] Register the existing `RenderExtractionCache` instance as a borrowed
      service and route the UV facade query through it; remove both production
      UV Engine methods and the test-only material-binding getter.
- [ ] Reduce `Runtime.Engine.cppm` to `<=42` plain imports, `<=21` domain
      imports, and `<=31` public getter names without restoring retired module
      surfaces.
- [ ] Ratchet `kernel_convergence_policy.json` and the dated architecture
      snapshot in the same change.

## Tests

- [ ] Focused Engine-private-glue, asset residency, render extraction, ImGui,
      Sandbox parameterization, and UV-view CPU contracts pass.
- [ ] ServiceRegistry coverage proves the registered cache identity; removed
      getter tests query that owner directly; copied UV commands still fail
      closed across detach/reattach.
- [ ] The delivered `GRAPHICS-122` opt-in Vulkan UV-view smoke passes on an
      operational promoted-Vulkan host.
- [ ] The kernel-convergence checker and default CPU-supported gate pass.

## Docs

- [ ] Update the kernel target-state snapshot and runtime ownership docs for
      the selected dependency/lifetime shape.
- [ ] Regenerate the module inventory if any module surface changes.

## Acceptance criteria

- [ ] Plain imports are `<=42`, domain imports are `<=21`, public getter names
      are `<=31`, and `temporary_debt` is null under the authoritative checker.
- [ ] No retired service BMI or compatibility re-export returns.
- [ ] Existing runtime teardown order and the operational GPU UV-view path are
      preserved.

## Verification

```bash
python3 tools/repo/check_kernel_convergence.py --root . --strict
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEnginePrivateGlue|RuntimeModule|AssetResidency|RenderExtraction|ImGuiEditorBridge|Parameterization|UvView|SandboxEditorSession.*Stale' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'UvViewGpuSmoke.RetainedBackgroundModes|RuntimeSandboxAcceptanceGpuSmoke.ParameterizationUvViewWindow' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Reclassifying domain imports as substrate to hit the budget.
- Recreating standalone modules solely to hide Engine's direct dependencies.
- Adding a new Engine accessor, callback, registry, or service wrapper as a
  replacement for the removed getter.
- Mixing unrelated runtime decomposition into this bounded remediation.

## Maturity

- Target: `Operational`; the slice must preserve the already operational
  Engine-owned services and GPU UV-view path, not only compile structurally.
