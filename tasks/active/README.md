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
  `tasks/backlog/geometry/` on 2026-05-21 for Slice 1: owning
  `Geometry.MeshSoup` container, borrowed view naming, `PropertySet`-backed
  vertex/face/corner domains, validation diagnostics, unit tests, docs, and
  module inventory. Status: in-progress; next
  verification step is focused `IntrinsicGeometryTests` plus structural checks.
- [`GRAPHICS-075`](GRAPHICS-075-default-recipe-postprocess-chain-wiring.md) —
  Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap →
  FXAA/SMAA). Promoted from `tasks/backlog/rendering/` on 2026-05-21 as the
  next unblocked Theme A default-recipe leaf after GRAPHICS-074's retirement.
  Status: in-progress; Slice A (ToneMap pipeline + `"PostProcessPass"`
  umbrella executor branch routing through `RecordPostProcessToneMapPass`)
  on branch `claude/setup-agent-workflow-dFrfe`. Slices B–E (bloom, FXAA,
  SMAA + retained LUTs, histogram compute + readback drain) queued behind
  Slice A's CPU/null contract gate. Next verification step after Slice A
  lands: open Slice B for the bloom downsample/upsample pipelines + helper.
