---
id: RUNTIME-328
theme: F
depends_on: [ARCH-020, RUNTIME-327]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: refactor of how the vector-field draw style travels from extraction to the draw record; the draw record, shader and drawn result are unchanged; no catalog contract covers this path
---
# RUNTIME-328 — The vector-field glyph style is carried once (moonshot M2, slice 3)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `eee3836dd`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim, 2026-10-28)
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."
5. "Wait and continue with M2"
6. "Do both" (the rendering half, and the declare-once table for the next recipe: `RUNTIME-329`)

## Goal
- Moonshot M2, rendering half of the vector-field style (`ARCH-020` row 3, Q1 "the vector-field
  style re-declared in M2: 29 files become 3 or fewer"). After `RUNTIME-327` the authoring side is
  one table row; from the layer to the GPU each uniform style value is still copied by hand:
  layer → `VectorFieldRequest` (extraction) → `VectorFieldPacketInputs` (runtime recipes) →
  `VectorFieldOverlayPacket` (graphics) → draw record → shader. The style is validated twice with
  different rules (`AppendVectorFieldPacket`, `VectorFieldStyleValid`), and the line-width bounds
  0.5..32 are declared in runtime and in graphics.

## Plan
1. **One glyph style** in `Graphics.VisualizationPackets.cppm`: `VectorFieldGlyphStyle` holds the
   uniform values that reach the draw (Scale, NormalizeLength, LineWidthPx, Color, DepthTested,
   ColorMap, ColorRangeMin, ColorRangeMax; defaulted `==`). `VectorFieldOverlayPacket` and
   `VectorFieldPacketInputs` carry it as `Style` in place of those eight members.
2. **One style check**: `IsValidVectorFieldGlyphStyle(style, colorMode)` holds the style part of
   the packet's current rule (finite positive scale, finite color, width in bounds, a supported color
   mode, and in colormap mode a supported colormap with a finite min < max). The packet check keeps
   its own positive `RowStride` and finite `ObjectToWorld` and calls it; renderability and the batch
   diagnostics both use that packet check. `AppendVectorFieldPacket` calls the shared check and
   returns `InvalidRange` as now. Tightening: Append newly refuses an unsupported colormap enum in
   colormap mode and an unsupported color mode (both produced packets the renderer refused later).
3. **No request copy**: extraction keeps the enabled layers themselves (copied, as the request
   copy did) and builds `inputs.Style` with one function from the layer; `VectorFieldRequest`
   goes.
4. **Width bounds once**: `kMin/kMaxGeometryVectorFieldLineWidthPx` are deleted; the style row
   uses the graphics constants (the row file imports the packets module; the recipe interface
   does not).
- Member accesses move to `.Style` in every reader, packing unchanged: the explicit-recipe encoder
  (`VisualizationRecipes.cpp`, keeps `NormalizeLength = false`), the record fill (upload helper),
  the colormap lookup (`Graphics.Renderer.cpp`), the depth pipeline choice
  (`Pass.VisualizationOverlay.cpp`). No compatibility aliases.
- Unchanged: draw record layout and bytes, the shader (its defensive width clamp stays; the
  record is written only for renderable packets), Renderer and pass behaviour, scene and agent.
- Not in this slice: the recipe holding the glyph style itself (its LengthMode enum and the
  CPU-only fields differ), and generating the shader struct from C++.

## Counting rule
- Same representative change as `RUNTIME-327`: one uniform style field that changes drawing.
  Before (after `RUNTIME-327`): authoring 3; rendering: extraction (request member, request fill,
  inputs fill), `VisualizationRecipes.cppm` (inputs member), `VisualizationRecipes.cpp` (append
  copy, append check), `VisualizationPackets.cppm` (packet member), `VisualizationPackets.cpp`
  (packet check), upload helper `.cppm`/`.cpp` (record member and fill), shader. Expected after:
  rendering `VisualizationPackets.cppm` (style member), extraction (one line), upload helper
  `.cppm`/`.cpp`, shader, plus `VisualizationPackets.cpp` only for a ranged field.

## Tests (predictions as assertions)
- `RuntimeVectorFieldExtraction.EveryStyleFieldReachesThePacket` (written first, against the flat
  packet; a refactoring test, passes before and after): a layer differing from the defaults in
  every drawn style field yields a packet whose style equals the layer's, field by field.
  Mutation: dropping one field's copy in extraction turns it red.
- `VisualizationRecipes.VectorFieldAppendRejectsUnsupportedColormapAndMode`: with valid buffers,
  an unsupported colormap in colormap mode and color mode 255 each return `InvalidRange` and append
  nothing (red before: `Encoded`).
- Existing packet, overlay-pass, extraction and recipe suites pass with only the member path
  changed (`.Style.X`).

## Acceptance criteria
- [x] `VectorFieldRequest` deleted; packet and inputs carry `VectorFieldGlyphStyle`.
- [x] One style check used by Append and by the packet; one declaration of the width bounds.
- [x] Draw record, shader and scene format unchanged.
- [x] The count recorded here and in `ARCH-020`.
- [x] Codex plan and code review; focused suites and the full CPU gate pass.

## Verification
```bash
cmake --build --preset ci -j6
# focused: the owning suites
./build/ci/bin/IntrinsicRuntimeGraphicsCpuTests --gtest_filter='RuntimeVectorFieldExtraction.*'
./build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='VisualizationRecipes.*'
ctest --test-dir build/ci -R 'VisualizationOverlayPass|VisualizationPackets|RendererFrameLifecycle' --output-on-failure --timeout 60
# full CPU gate: the packets module interface is imported widely
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' -j6 --output-on-failure --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-28: Plan counter-review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested gpt-6-astra/xhigh, read-only sandbox; thread `01a18147-cf7f-78a0-877e-552beef78eb3`;
  plan sha256 at review in the session scratchpad): verdict "revise". B1 state the whole validation
  contract (row stride and transform stay packet checks; the mode check moves into the shared rule,
  a second tightening of Append, to be tested); B2 list the other readers (explicit-recipe encoder,
  record fill, colormap lookup, depth pipeline choice). Both folded into the plan; O1–O3 (count
  with a concrete field, reuse existing record tests, focused verification) taken. Recursion
  protection instruction-only (other MCP servers stay reachable).
- 2026-10-28: Implemented. Predictions held: `EveryStyleFieldReachesThePacket` passed before (refactoring
  test); `VectorFieldAppendRejectsUnsupportedColormapAndMode` was red before (`Encoded`, batch grew).
  Mutations, one at a time, restored after: extraction without the width copy → the extraction test
  red only; Append without the shared check → the Append test red only. Focused: extraction 14/14,
  recipes 21/21, overlay/packets/frame-lifecycle/extraction ctest 190/190. Full CPU gate 5929/5929.
  No Vulkan run (record bytes and shader unchanged by construction; the GPU smoke only had its
  initializer nested).
  - Count, representative change: a new uniform float the shader draws with (say an arrow head
    size). Authoring 3 as after `RUNTIME-327`. Rendering before: extraction (request member and
    fill, inputs fill), `VisualizationRecipes.cppm`, `VisualizationRecipes.cpp`,
    `VisualizationPackets.cppm`, upload helper `.cppm`/`.cpp`, shader = 7 (8 with a range check in
    `VisualizationPackets.cpp`). After: `VisualizationPackets.cppm` (style member), extraction (one
    line), upload helper `.cppm`/`.cpp`, shader = 5 (6 ranged). Total without tests 10–11 → 8–9.
    Fields read elsewhere differ (depth test: the pass; colormap: the renderer's lookup). Q1 ("29
    files become 3 or fewer", tests included) stays unmet; the next big step would be generating the
    shader struct and record from one declaration.
- 2026-10-28: Code review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  gpt-6-astra/xhigh, read-only sandbox; thread `01a18151-9175-7ce3-a5d5-c9f7a78c3a3a`; patch sha256
  `dda67a7abe7efc44445160fc65d55fdac863b199304f1efc75741fd7f439c209`): verdict "approve", no P1; N:
  record the count in `ARCH-020` (done). Duplication check (jscpd MCP, exact): no new clones in the
  touched files. Recursion protection instruction-only.
- 2026-10-28: Live and Vulkan check at `51d4dccf2` (NVIDIA GeForce RTX 4090, driver 580.159.04, X11
  :1, 1600x900 viewport, pixel ratio 1, validation on): gpu;vulkan smokes 50/50, no skips
  (`VisualizationOverlay|VectorField|RuntimeSandboxAcceptance`, incl. the vector-field style,
  length/color property and depth/length-mode readbacks). Running Sandbox through the agent socket:
  a vertex field on the Duck took width, color, stride, length, normalized/raw mode, depth test on/off,
  a scalar color property through Coolwarm/Jet with a manual range and clearing it again, each visible in
  screenshots; pure red reads back (245,0,0); `line_width` 100 refused ("at most 32.0", the one graphics
  bound); validation errors 0. Observation, not from this change: overlay colors are lit and
  tone-mapped, so dark channels lift ((0.1,0.2,1) shows as (171,203,245)).
