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
  ordered `"PostProcessAAPass"` graph pass + 20-byte
  `PostProcessFXAAPushConstants` std430 push block + contract tests),
  D.1 (three SMAA pipelines — edge / blend / resolve — + per-shader
  16-byte std430 push blocks + umbrella fan-out alongside FXAA,
  mutually exclusive per `PostProcessSettings::AntiAliasing`), D.2a
  (AA umbrella split into three ordered passes
  `"PostProcessAA{Edge,Blend,Resolve}Pass"` + recipe-side
  `PostProcess.AATemp` rename into
  `.{Edges,Weights,Resolved}` + edge / blend pipeline format flip to
  `RG8_UNORM` / `RGBA8_UNORM` + per-stage SMAA pass body
  `Execute{Edge,Blend,Resolve}` + per-pass renderer helpers + AA
  feature-flag plumbing + contract test updates), and D.2b
  (device-aware `PostProcessSystem::Initialize(device, textureMgr,
  bufferMgr)` overload that allocates + uploads the retained SMAA
  `AreaTex` / `SearchTex` LUT textures and the
  `PostProcessExposureHistory` storage buffer, idempotent across
  renderer rebuild, with `Shutdown()` releasing the leases +
  survive-rebuild contract test
  `PostProcessSMAALookupTexturesSurviveOperationalRebuild`) are
  landed. Slice E (Histogram compute pipeline + readback drain
  consuming the exposure-adaptation history buffer Slice D.2b
  allocated) remains queued. Next verification step: open Slice E in
  a follow-up session.
