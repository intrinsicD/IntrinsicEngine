# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GEOM-006`](GEOM-006-indexed-mesh-soup-conversion-contracts.md) — Indexed
  mesh/soup container and conversion contracts. Promoted from
  `tasks/backlog/geometry/` on 2026-05-21; Slice 1 added the owning
  `Geometry.MeshSoup` container and validation, and Slice 2 added
  `Geometry.Mesh.Conversion` soup ↔ halfedge owning conversion contracts with
  direct clang-22 smoke verification. Status: in-progress; next verification step is repairing the
  local `clang-20`/Draco-cache preset blockers, then running focused
  `IntrinsicGeometryTests` and CTest.
- [`GRAPHICS-075`](GRAPHICS-075-default-recipe-postprocess-chain-wiring.md) —
  Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap →
  FXAA/SMAA). Promoted from `tasks/backlog/rendering/` on 2026-05-21 as the
  next unblocked Theme A default-recipe leaf after GRAPHICS-074's retirement.
  Status: in-progress; Slices A (ToneMap), B.1/B.2 (Bloom downsample +
  upsample + per-mip iteration + recipe-side `BloomScratch.MipLevels`
  clamping + barrier-sequence contract test), and C (FXAA pipeline +
  `RecordPostProcessFXAAPass(...)` helper running in its own ordered
  `"PostProcessAAPass"` graph pass declared with `Read(SceneColorLDR) +
  Write(PostProcess.AATemp)` so the framegraph compiler emits the
  read-after-write barrier the FXAA shader needs + 20-byte
  `PostProcessFXAAPushConstants` std430 push block + contract tests) are
  merged via PRs #902 / #904 / #906. Slice D.1 (three SMAA pipelines —
  edge / blend / resolve — + per-shader 16-byte std430 push blocks +
  `RecordPostProcessSMAAPass(...)` umbrella fan-out behind the same
  `"PostProcessAAPass"` branch alongside FXAA, mutually exclusive per
  `PostProcessSettings::AntiAliasing`, with both pass bodies'
  `IsStageEnabled` gate enforcing the selector) is landing on branch
  `claude/setup-agent-workflow-jXYUh`. Slice D.2 (retained `AreaTex` /
  `SearchTex` LUT textures + exposure-adaptation history buffer +
  recipe-side `PostProcess.AATemp.{Edges,Weights}` rename + survive-
  rebuild contract test for the retained LUTs) and Slice E (Histogram
  compute + readback drain) remain queued behind Slice D.1's CPU/null
  contract gate. Next verification step after Slice D.1 lands: open
  Slice D.2.
