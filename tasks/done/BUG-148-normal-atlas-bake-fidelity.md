---
id: BUG-148
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-bug148"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-10T08:44:50Z"
maturity_target: Operational
contract_schema: 1
contracts:
  - geometry.property-coherence
---
# BUG-148 — Normal texture atlas baking changes the visible normal field

## Status

- Completed and retired on 2026-08-10.
- Completion commit: this retirement commit.
- Operational evidence: the promoted Vulkan generated-atlas fixture executed
  with more than 1,000 varying-normal samples below one degree of angular
  error; the CPU-supported gate passed 4,177/4,177 tests with the expected
  `GlfwLifecycleLsan` capability skip.

## Goal
- Preserve the source object-space normal field when it is baked through the
  generated UV atlas and consumed as a material normal texture.

## Non-goals
- No new UV atlas backend, chart-ID texture, texture service, or generic mip
  generator.
- No tangent-space normal-map support.
- No change to raw/scalar/color property-bake encodings beyond shared default
  extent and padding controls.

## Context
- `runtime` currently generates a 1024x1024 UV atlas with two texels of chart
  padding, then automatically bakes normals into a fixed 64x64 texture with
  four dilation passes. The normalized atlas gutter becomes one eighth of a
  bake texel, small charts lose raster coverage, and dilation can cross the
  chart separation.
- `graphics/renderer` clears uncovered normal texels to encoded +Z with alpha
  zero, while surface shaders linearly filter RGB before checking alpha. A
  boundary sample therefore mixes a covered normal with +Z and is accepted as
  covered, visibly rotating the field.
- The right-sized fix reuses the existing atlas diagnostics, request fields,
  alpha coverage channel, and one-mip texture path. It does not introduce a
  second bake service, chart ownership buffer, or vector-unsafe mip chain.
- Ownership remains unchanged: runtime chooses bake extent/padding; graphics
  records the bake and owns material sampling; geometry remains the CPU
  property authority.

## Control surfaces
- Config/agent: existing `PropertyTextureBakeRequest` and
  `EditorTextureBakeCommand` width, height, and padding fields.
- UI: Sandbox UV/texture-bake panel exposes the same fields and adopts a
  successful regenerated atlas extent.

## Backends
- Backend axis: Vulkan GPU property raster and promoted forward/deferred
  consumers; CPU/null keeps fail-closed command coverage.

## Required changes
- [x] Resolve automatic normal-bake dimensions from the atlas dimensions,
      with a 1024 fallback only when no atlas result is available.
- [x] Use chart-safe two-texel dilation for the default two-texel atlas margin
      at matching resolution; expose the existing padding field in the manual
      bake UI and pass it through the editor command.
- [x] Make uncovered normal texels transparent black and reconstruct filtered
      encoded RGB from alpha coverage before decoding the normal in every
      promoted and retained surface consumer.
- [x] Keep generated normal textures at one mip so no component-wise color
      downsample silently changes vector direction.

## Tests
- [x] Add/update CPU contract coverage for the default extent/padding and the
      transparent normal clear used by raster/dilation.
- [x] Strengthen the existing `gpu;vulkan` imported normal-bake smoke to assert
      the atlas-sized output, varying-normal angular fidelity, covered gutter,
      transparent uncovered texels, and exact generated-texture binding.
- [x] Compile every modified shader and run the focused runtime/graphics tests.

## Docs
- [x] Document atlas-matched normal bake resolution, coverage-aware normal
      filtering, chart-safe padding, and the intentional one-mip policy in the
      runtime and graphics architecture docs.

## Acceptance criteria
- [x] The automatic import path no longer downsamples a default 1024 atlas to a
      64 texture before binding the generated normal.
- [x] Linear filtering across covered/uncovered texels does not rotate the
      decoded normal toward the clear value.
- [x] Default dilation stays within the generated atlas's chart margin at the
      matching bake resolution.
- [x] Manual bake users can see and set padding, and UV regeneration carries
      its actual atlas extent into the bake controls.
- [x] Vulkan evidence executes the real import, bake, generated-asset binding,
      texture readback, and varying-normal comparison path.
- [x] No new dependency edge or abstraction surface is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'PropertyTextureBake|TextureBakeModule|SandboxEditorVisualization' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- Hiding the defect by disabling generated normal textures or always falling
  back to vertex normals.
- Treating normal vectors as ordinary colors during filtering or mip
  generation.
- Increasing only the 64x64 constant without coupling automatic bakes to the
  atlas result and fixing coverage-aware sampling.
- Adding a second normal-only bake service or renderer path.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `CPUContracted` elsewhere.
- The existing imported-normal `gpu;vulkan` smoke is the operational seam and
  must execute the fixed path rather than only inspect request metadata.
- Achieved: `Operational` for the bounded generated-atlas Vulkan fixture and
  `CPUContracted` for the request, UI, clear-color, and shader-consumer
  contracts. No follow-up is owed for this defect.
