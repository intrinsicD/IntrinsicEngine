# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GEOM-015`](GEOM-015-gjk-termination-diagnostics.md) — GJK termination
  diagnostics and scale-aware tolerance policy. Promoted from
  `tasks/backlog/geometry/` on 2026-05-22 as the deferred successor to
  GEOM-007 Slice 3.3.d (GEOM-007 retired the same day). Status: in-progress
  (Slice 4 next; Slices 1–3 landed). Slice 1 adds
  `RobustPredicates::ApproxZeroSq` / `ApproxZeroLen` helpers with
  `SignedResult`-shaped diagnostics plus unit tests. Slice 2 migrates the
  original-space magnitude guards in `Geometry.Support` / `Geometry.SDFContact`
  to the new helper with axis-local scale choices and adds regression
  coverage for the previously-flipped sub-millimeter Ellipsoid and
  short-axis Cylinder cases. Slice 3 pins the GJK normalized-workspace
  tolerance contract: keeps `GJK_EPSILON` as a dimensionless constant
  (driver already factors `invScale = 1/shapeScale`), adds a
  `static_assert(GJK_EPSILON > 0 && GJK_EPSILON < 1)`, and documents the
  contract in a new "GJK tolerance contract" subsection of
  `docs/architecture/geometry.md`. Slice 4 (GJK termination-reason
  diagnostics surface + parity / convergence regression battery) is
  queued. Next verification step:
  `cmake --build --preset ci --target IntrinsicGeometryTests`,
  `ctest --test-dir build/ci --output-on-failure -R 'GJK|Support|ContactManifold|Overlap|RobustPredicates' --timeout 60`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/docs/check_doc_links.py --root .`, and
  `python3 tools/agents/check_task_policy.py --root . --strict` after each
  slice lands.

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
  feature-flag plumbing + contract test updates), D.2b
  (device-aware `PostProcessSystem::Initialize(device, textureMgr,
  bufferMgr)` overload that allocates + uploads the retained SMAA
  `AreaTex` / `SearchTex` LUT textures and the
  `PostProcessExposureHistory` storage buffer, idempotent across
  renderer rebuild, with `Shutdown()` releasing the leases +
  survive-rebuild contract test
  `PostProcessSMAALookupTexturesSurviveOperationalRebuild`), and
  E.1 (Histogram compute pipeline scaffold +
  `RecordPostProcessHistogramPass(...)` + new ordered
  `"PostProcessHistogramPass"` graph pass split from the
  `"PostProcessPass"` umbrella because Vulkan rejects compute
  dispatches inside an active render-pass scope + pass-local
  16-byte `PostProcessHistogramPushConstants` std430 push block +
  `SetViewport(...)`-driven `ceil(W/16) x ceil(H/16) x 1` dispatch
  shape + contract tests) are landed. Slice E.2 (host-visible
  `Histogram.Readback` buffer + `BeginFrame()`-side readback drain
  mirroring the `Picking.Readback` pattern +
  `PostProcessSystem::PublishHistogramReadback(...)` consuming the
  exposure-adaptation history buffer Slice D.2b allocated) remains
  queued. Next verification step: open Slice E.2 in a follow-up
  session.
