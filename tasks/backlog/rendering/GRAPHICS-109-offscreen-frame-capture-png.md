---
id: GRAPHICS-109
theme: none
depends_on: []
---
# GRAPHICS-109 — Offscreen frame capture to PNG (headless figure renders)

## Goal
- Finish the offscreen-capture lane so a rendered frame (point-cloud / mesh view) can be read back and written to a PNG at a requested resolution, enabling reproducible publication-quality rendered figures (teaser, hierarchy panels, comparison scatters) from headless or interactive runs.

## Non-goals
- No vector-plot generation (RDF/RAPS/periodogram plots are RUNTIME-133 + external scripts).
- No new renderer passes or shading models; this captures existing renderer output.
- No video/animation export.

## Context
- Status: backlog.
- Owning subsystem/layer: `graphics`/`runtime` — the render-recipe/offscreen config and readback already exist (`Graphics.RenderRecipeConfig` `target: OffscreenTexture`/`mode: Headless`/`captureRequested`, `Runtime.RenderArtifactPublication` with `SavedToFile`, `Runtime.GpuReadbackJob`, `Graphics.RenderingContract` `RenderOutputKind::Color`/`ReadbackBuffer`). What is **missing** is the final step: a PNG encoder + save path wired to the capture request. Gate on `RHI::IDevice::IsOperational()`.
- Pairs with RUNTIME-131 (agent/CLI config facade) so captures can be driven without ImGui, and with RUNTIME-134 for an in-editor "export frame" action.

## Required changes
- [ ] Wire the existing offscreen color-target readback to a PNG encoder (via an approved `vcpkg.json` image-write dependency or a repository-owned encoder) and write to a caller-specified path.
- [ ] Honor a requested capture resolution and color format from the render-recipe/headless config; document the supported formats.
- [ ] Publish the saved file through `Runtime.RenderArtifactPublication` (`SavedToFile`) with metadata (path, resolution, format) and fail closed with diagnostics when the device is non-operational or the path is unwritable.
- [ ] Provide a minimal headless entry (driven by config / the RUNTIME-131 facade) that renders a scene to an offscreen target and saves a PNG, usable from CI/agent contexts.

## Tests
- [ ] Add a `gpu;vulkan` (offscreen) test under `ci-vulkan` that renders a known scene headless and asserts a non-empty, correctly-sized PNG is written and re-decodes to the expected dimensions.
- [ ] Add a fail-closed test for non-operational device / unwritable path (explicit diagnostic, no partial file).
- [ ] Confirm the default CPU gate stays green (capture tests are label-gated).

## Docs
- [ ] Document the capture/export flow and supported formats under `docs/architecture/` (rendering) and the headless usage recipe; update `docs/benchmarking` or method docs if used for figure capture.
- [ ] Record any new dependency in `vcpkg.json`/`vcpkg-configuration.json` and the dependency docs; regenerate `docs/api/generated/module_inventory.md` if surfaces change.

## Acceptance criteria
- [ ] A headless render produces a PNG at the requested resolution, published as a `SavedToFile` artifact with metadata.
- [ ] Non-operational device and unwritable paths fail closed with diagnostics; no partial files.
- [ ] `gpu;vulkan` capture tests pass under `ci-vulkan`; the default CPU gate remains green.
- [ ] Layering preserved (gate on `IsOperational()`, no `Vk*` leakage through RHI, no ECS knowledge in graphics).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Capture|Png|Offscreen' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding renderer passes or shading models in this capture task.
- Leaking `Vk*` types through RHI or adding ECS knowledge to graphics.
- Adding an image dependency outside the vcpkg manifest flow.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; this task owns the offscreen-capture `Operational` milestone (`Operational` owned by GRAPHICS-109). On Null/non-Vulkan hosts capture reports unavailable and is skipped.
