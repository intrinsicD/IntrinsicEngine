---
id: RUNTIME-178
theme: F
depends_on:
  - HARDEN-085
maturity_target: Operational
---
# RUNTIME-178 — Restore the pre-ratchet Runtime.Engine convergence budget

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

## Required changes

- [ ] Audit the direct dependencies needed for the three private Engine
      declaration headers and the `GRAPHICS-122` UV-view material-binding
      query, including lifetime and facade consumers.
- [ ] Remove `Engine::GetMaterialTextureAssetBindings` by routing the UV-view
      query through an existing owning facade/module/service seam or a smaller
      shape justified by the audit.
- [ ] Reduce `Runtime.Engine.cppm` to `<=43` plain imports and `<=23` domain
      imports without restoring retired public module surfaces.
- [ ] Ratchet `kernel_convergence_policy.json` and the dated architecture
      snapshot in the same change.

## Tests

- [ ] Focused Engine-private-glue, asset residency, render extraction, ImGui,
      Sandbox parameterization, and UV-view CPU contracts pass.
- [ ] The delivered `GRAPHICS-122` opt-in Vulkan UV-view smoke passes on an
      operational promoted-Vulkan host.
- [ ] The kernel-convergence checker and default CPU-supported gate pass.

## Docs

- [ ] Update the kernel target-state snapshot and runtime ownership docs for
      the selected dependency/lifetime shape.
- [ ] Regenerate the module inventory if any module surface changes.

## Acceptance criteria

- [ ] Plain imports are `<=43`, domain imports are `<=23`, and the temporary
      getter debt is zero under the authoritative checker.
- [ ] No retired service BMI or compatibility re-export returns.
- [ ] Existing runtime teardown order and the operational GPU UV-view path are
      preserved.

## Verification

```bash
python3 tools/repo/check_kernel_convergence.py --root . --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R UvView --timeout 180
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
