# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-075`](GRAPHICS-075-default-recipe-postprocess-chain-wiring.md) —
  Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap →
  FXAA/SMAA). Promoted from `tasks/backlog/rendering/` on 2026-05-21 as the
  next unblocked Theme A default-recipe leaf after GRAPHICS-074's retirement.
  Status: in-progress; Slices A (ToneMap), B.1/B.2 (Bloom downsample +
  upsample + per-mip iteration + recipe-side `BloomScratch.MipLevels`
  clamping + barrier-sequence contract test), C (FXAA pipeline +
  `RecordPostProcessFXAAPass(...)` helper running in its own ordered
  `"PostProcessAAPass"` graph pass declared with `Read(SceneColorLDR) +
  Write(PostProcess.AATemp)` so the framegraph compiler emits the
  read-after-write barrier the FXAA shader needs + 20-byte
  `PostProcessFXAAPushConstants` std430 push block + contract tests),
  and D.1 (three SMAA pipelines — edge / blend / resolve — +
  per-shader 16-byte std430 push blocks +
  `RecordPostProcessSMAAPass(...)` umbrella fan-out behind the same
  `"PostProcessAAPass"` branch alongside FXAA, mutually exclusive per
  `PostProcessSettings::AntiAliasing`, with both pass bodies'
  `IsStageEnabled` gate enforcing the selector) are merged via PRs
  #902 / #904 / #906 / #907. Slice D.2 has been split into D.2a (AA
  umbrella restructuring — split `"PostProcessAAPass"` into three
  ordered passes `"PostProcessAA{Edge,Blend,Resolve}Pass"` so edge /
  blend / resolve pipelines can target format-incompatible
  `RG8_UNORM` / `RGBA8_UNORM` / backbuffer attachments — + recipe-side
  `PostProcess.AATemp` rename into `.{Edges,Weights,Resolved}` + edge
  / blend pipeline format flip + contract test updates) and D.2b
  (retained `AreaTex` / `SearchTex` LUT textures + exposure-adaptation
  history buffer + device-aware `PostProcessSystem::Initialize(device)`
  overload + survive-rebuild contract test for the retained LUTs), and
  Slice E (Histogram compute + readback drain) remains queued behind
  D.2a/D.2b. Next verification step: implement Slice D.2a in a
  follow-up session.
