---
id: BUG-266
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
contract_schema: 1
contracts: []
contract_review: Renderer-internal descriptor publication and compiler-warning cleanup; no layering, method or configuration contract changes.
---
# BUG-266 — Frame-sampled slot flicker on click, and compiler warnings

## Operator direction (verbatim, 2026-11-09)
1. "and? i see the ref frames flashing"
2. "please look at all of these warnings. can they be fixed?"
3. "fix these:" (a CLion Debug build log)
4. "please merge to main and push to remote" (answer to the follow-up question: "The 55 uncommitted files")

## Goal
- A click or selection change no longer shows black, red or stale frames on the promoted Vulkan path.
- The warnings of the pasted Debug build are gone, without changing behavior.

## Cause and fix
The Vulkan bindless set is update-after-bind, so a frame still pending on the GPU samples whatever a
bridge slot holds when it executes. A slot must keep one texture frame after frame.
- Only a pass's `ShaderRead` textures are published (`RenderGraphPassDeclaration::SampledTextures`); a
  depth attachment read no longer overwrites slot 0.
- `HZBBuildPass` samples `SceneDepth` from its own slot 4.
- When a slot's texture has to change, the renderer waits for the pending frames first
  (`IDevice::WaitIdle()`); an unchanged slot costs nothing.
- The editor's texture-bake command reports the two atlas refusals (`OverlappingUvCharts`,
  `UnderresolvedAtlas`) as a failed command.

The slot contract is in `src/graphics/vulkan/README.md`; the triage skill names it.

## Acceptance criteria
- [x] `RendererFrameLifecycle.DefaultBridgeSlotOnlyEverHoldsThePostProcessInput`,
      `RendererFrameLifecycle.BridgeSlotChangesWaitForPendingFrames` and
      `RuntimeTextureBakeModule.EditorBakeCommandFailsWhenTheAtlasIsRefused` pass.
- [x] The CPU gate (`ci` preset) builds without warnings and passes locally (2026-11-09: 6304 of 6304,
      two skipped).
- [ ] An independent correctness review of the final diff (an earlier state got "approve with nits").
- [ ] A `gpu;vulkan` smoke asserts no glitch frame across a selection change (the session measured 45
      glitch bursts before and 0 after in a 17 s run, by hand).

## Known limits
- `WaitIdle` also waits for method compute; a click during a long GPU run may stall. Wait on the frame
  fences only if that shows.
- The one-texture rule holds for the default recipe. Reconstruction, FXAA/SMAA, deferred composition
  and a debug view without a selection still publish other textures to slot 0.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 --parallel 4
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
```
