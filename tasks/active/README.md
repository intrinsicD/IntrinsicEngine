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
  Status: in-progress; Slices A (ToneMap) and B.1/B.2 (Bloom downsample +
  upsample + per-mip iteration + recipe-side `BloomScratch.MipLevels`
  clamping + barrier-sequence contract test) are merged via PRs #902 / #904.
  Slice C (FXAA pipeline + `RecordPostProcessFXAAPass(...)` umbrella fan-out
  + 20-byte `PostProcessFXAAPushConstants` std430 push block + contract
  tests) is landing on branch `claude/setup-agent-workflow-ReViy`; Slices D
  (SMAA + retained LUTs + exposure-adaptation history buffer) and E
  (Histogram compute + readback drain) remain queued behind Slice C's
  CPU/null contract gate. Next verification step after Slice C lands: open
  Slice D for the SMAA edge/blend/resolve pipelines + retained `AreaTex` /
  `SearchTex` LUT textures + exposure-adaptation history buffer + recipe-
  side `PostProcess.AATemp.{Edges,Weights}` rename, behind the same
  `"PostProcessPass"` umbrella branch.
