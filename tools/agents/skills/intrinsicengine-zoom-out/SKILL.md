---
name: intrinsicengine-zoom-out
description: Ask the agent to zoom out and give a layer-cake map of the code under discussion, using IntrinsicEngine's domain vocabulary (`core`, `geometry`, `assets`, `ecs`, `physics`, `graphics/rhi`, `graphics/vulkan`, `graphics/*`, `platform`, `runtime`, `app`, `methods`, `benchmarks`, `tests`) and `.cppm` module surfaces. Use when you're unfamiliar with a section of code, need higher-level context before editing, or want a map of how a touched file fits the larger system. Trigger phrases include "zoom out", "give me the layer map", "where does this fit", "I don't know this area".
disable-model-invocation: true
---

# IntrinsicEngine Zoom Out

I don't know this area of the engine well. Go up a layer of abstraction.

Give me:

1. **Which layer this file lives in** (`core` / `geometry` / `assets` / `ecs` /
   `physics` / `graphics/rhi` / `graphics/vulkan` / `graphics/<other>` /
   `platform` / `runtime` / `app` / `methods` / `benchmarks` / `tests`).
2. **The `.cppm` module surface(s) it exports or imports** — what names are
   visible across the layer boundary, and what is implementation-private.
3. **The layers above and below** that depend on or are depended on by this
   code, with the *concrete* module names — not "the renderer" but
   `Extrinsic.Graphics.Renderer.PassRegistry`.
4. **Who composes this** — usually a single composition site in `runtime` for
   non-trivial wiring. Name the file.
5. **The ADR(s) under `docs/adr/`** that govern decisions in this area, if any.
6. **The owning task(s)** under `tasks/active/` or recently-retired tasks in
   `tasks/done/` that last touched it, if any.

Use the IntrinsicEngine layering vocabulary exactly — don't drift into
"component", "service", "API". Use `module`, `interface unit` (`.cppm`),
`partition`, `seam`, `composition root`, `backend`.

If the file is under `methods/`, also name the backend split (`reference` vs
`optimized` vs `gpu`) and the public method contract it satisfies.

Keep it to a short map I can hold in my head. No code, no implementation
detail — just the structure.
