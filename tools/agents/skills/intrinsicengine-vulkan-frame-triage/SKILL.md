---
name: intrinsicengine-vulkan-frame-triage
description: Domain playbook for debugging wrong-frame-content and validation defects in IntrinsicEngine's promoted Vulkan path — black frames, black backbuffer readbacks, missing/wrong geometry, validation-layer (VUID) cascades, descriptor/bindless anomalies, and driver-level crashes during frame submission. Encodes the per-stage readback bisection recipe and the engine invariants (bindless bridge slot ownership, render-id conventions, integer-attachment clears, recipe clear propagation, QFOT pairing, Y-flip winding) that past bugs re-derived repeatedly. Use whenever the sandbox or a gpu;vulkan smoke renders black or wrong, a validation error cascade appears, vkCmd* crashes inside the driver, or a pick/readback returns impossible values.
---

# IntrinsicEngine Vulkan Frame Triage

`intrinsicengine-diagnose` owns the generic loop (reproduce → rank hypotheses
→ tagged probes → fix → regression test → cleanup). This skill is the
domain-specific playbook for **frame-content defects on the promoted Vulkan
path**, distilled from the bugs that re-derived it piece by piece
(`BUG-012`, `BUG-014`–`BUG-019`, `BUG-022`, `BUG-026`, `BUG-032`,
`BUG-056`–`BUG-060`).

## Step 0 — rule out non-defects

1. **Stale build first.** An unexplained SEGV/ASan/vtable anomaly after module
   changes is presumed a stale-BMI artifact until it survives a clean build —
   run `intrinsicengine-stale-build-triage` before spending GPU-debugging
   effort (`BUG-013` burned a session here).
2. **Operational gate.** Renderer/runtime behavior must be judged against
   `RHI::IDevice::IsOperational()`, not Vulkan diagnostics. A Null-device
   fallback renders nothing by design; confirm the promoted device actually
   reached operational (`INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` +
   `RenderConfig::EnablePromotedVulkanDevice`, `ci-vulkan` preset) before
   treating a black frame as a rendering bug.

## Rule 1 — validation layer first, first message first

For any crash or cascade during frame submission, run with
`VK_LAYER_KHRONOS_validation` enabled and capture **the first validation
message preceding the failure, not the crash site**. `BUG-012`'s driver SEGV
(`vkCmdPipelineBarrier2` inside `libnvidia-glcore.so`) was localised
immediately by its first VUID (`VUID-VkImageMemoryBarrier2-oldLayout-01209`);
`BUG-015`'s cascade resolved in dependency order once the first message was
treated as root. Later messages in a cascade are usually consequences.

## Rule 2 — the black-frame bisection ladder (from `BUG-016`)

All steps run headless; no interactive GUI capture is required.

1. **Per-stage GPU readback bisection (the decisive tool).** The engine owns a
   backbuffer readback path (`CopyTextureToBuffer` + the `gpu;vulkan` smokes
   reporting `nonBlackPixels`). Add temporary, opt-in per-attachment readback
   probes (env-var/debug-flag gated, removed before close) after each
   candidate stage — e.g. `SurfacePass` (`SceneColorHDR`), tonemap/postprocess
   output, present blit target, ImGui target. The first attachment that reads
   back all-zero localises the failing stage.
2. **Validation-layer `debug_printf` shader probes.** Enable validation-layer
   printf and add `GL_EXT_debug_printf` prints to the suspect fragment shaders
   for a few fixed UVs (sampled color, sampled bindless slot index). Output
   reaches stdout headlessly.
3. **Bindless slot / descriptor audit.** Verify at runtime that the sampled
   descriptors resolve to the intended reserved slots across the *first*
   operational frame — cold-start ordering differs from steady state.
4. **Headless `renderdoccmd` fallback.** `renderdoccmd capture` records a
   `.rdc` non-interactively and can dump resources from the command line.
   Fallback, not prerequisite.

Probes follow `intrinsicengine-diagnose` hygiene: tagged, opt-in, deleted
before close.

## Engine invariants that have already caused frame defects

Check these **before** inventing new hypotheses — each shipped as a real bug
at least once, several more than once:

- **Bindless bridge: last host write wins.** The whole frame is recorded into
  one command buffer against one bindless descriptor set and submitted once,
  so the *last* host-side write to a slot is what **every** recorded draw
  observes. The renderer's explicit per-pass `BindFrameSampledTextureAt` is
  the **single authority** for bridge slots; nothing else (barrier paths,
  passes with their own leases) may write them. Bridge slots 0..2 are
  reserved; texture leases start at slot 3. This exact defect class shipped
  three times: `BUG-014` (font atlas clobbered), `BUG-016` (barrier auto-bind
  + ImGuiPass racing slot 0), `BUG-019` (a fix re-colliding on slot 0). The
  contract is documented in `src/graphics/renderer/README.md`.
- **Render-id convention.** GPU instance-table render id =
  `uint32(entt handle) + 1`; `0` is reserved for background/none
  (`entt::null` wraps to 0). `StableEntityLookup::ToRenderId`/`ToEntityHandle`
  are the single authority — a raw `static_cast` of an entt handle silently
  makes entity 0 unpickable (`BUG-026`).
- **Integer-attachment clears are by value, not bit pattern.** UINT/SINT
  attachments must have clear colors converted with `static_cast` into
  `uint32[]`/`int32[]`; punning the float clear writes garbage (a 0.10f clear
  reads back as `0x3DCCCCCD`, `BUG-026`). Picking/ID targets clear to zero,
  never to the scene clear color.
- **Recipe clear colors must survive compilation.** The compiled render-pass
  attachment description must propagate the recipe's clear color; a hardcoded
  clear in `BuildActiveRenderPassDesc` masked content for a full bug cycle
  (`BUG-016`). The default-recipe scene clear presents as light blue,
  ~RGB(170,203,231) in BGRA8_SRGB — a *black* clear where blue is expected is
  itself a symptom.
- **Queue-family ownership transfers pair release/acquire.** An unmatched QFOT
  produces a cascade of layout/ownership VUIDs; fix the first transfer, then
  re-run — do not chase the cascade (`BUG-015`).
- **Framegraph transient handles vs. real handles.** Synthetic/transient
  resource handles must never reach backend barrier submission as if they were
  real Vulkan handles (`BUG-012`).
- **Vulkan Y conventions.** Clip-space Y-flip inverts triangle winding —
  geometry disappearing only on Vulkan is often front-face culling
  (`BUG-022`); screen/input space has +Y down, which has flipped drag and
  pick math before (`BUG-040`, `BUG-026` HiDPI cursor scaling).

## Exit criteria

- Root cause named at the correct seam (renderer / vulkan backend / framegraph
  / runtime extraction), not patched at the symptom.
- A regression exists: extend or add an opt-in `gpu;vulkan` readback smoke per
  `intrinsicengine-gpu-smoke-authoring`; never relax a non-black assertion,
  disable validation, or demote to Null to make a gate pass.
- If the fix clarified an invariant, the owning README
  (`src/graphics/renderer/README.md` or backend README) states it.
- All probes removed; task file records the bisection evidence.
