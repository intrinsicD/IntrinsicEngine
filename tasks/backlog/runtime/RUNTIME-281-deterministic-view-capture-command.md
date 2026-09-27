---
id: RUNTIME-281
theme: F
depends_on: [GRAPHICS-109, RUNTIME-287]
maturity_target: Operational
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-281 — Deterministic view capture command

## Progress — 2026-09-27 (slice A, `claude/view-capture`)
Done: `Extrinsic.Runtime.ViewCapture` (`ViewCaptureModule`: queued requests, one
renderer capture at a time, viewport crop via `ResolveSceneViewportPixels` or whole
window, PNG through the existing stb writer, `.partial` + rename so failures leave
no file, Null/non-operational fail-closed with a reason); agent tools
`view_screenshot` (read-only, image only) and `view_capture` (file inside the
allowed roots, image unless `path_only`), answered through deferred
`AgentOperationContinuation` replies; UI-062 callers; CPU tests
(`Test.ViewCapture.cpp`, `Test.AgentOperations.cpp`) and the `gpu;vulkan`
`ViewCaptureGpuSmoke` (PNG size and triangle-versus-clear pixels, current camera).
Remaining: camera presets with fit-to-entity, width/height override, property
visualization apply/restore with colormap range metadata and legend strip, and
`SavedToFile` artifact publication.

## Goal
- Capture the viewport to a PNG from a named camera preset or explicit camera,
  optionally with a property visualization and colormap-range metadata, through one
  runtime command used by the editor (UI-062), the batch CLI (RUNTIME-282) and agents.

## Non-goals
- No PNG encoding or readback code (GRAPHICS-109 owns "readback → PNG → `SavedToFile` artifact").
- No bitmap-font text legend in the PNG in this task (metadata + colormap strip first).
- No surface-less Vulkan device; headless capture on Null stays unavailable.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Depends on [GRAPHICS-109](../rendering/GRAPHICS-109-offscreen-frame-capture-png.md). Existing pieces: `Graphics.RenderingContract.cppm` `ViewOutputRecipeDescriptor{Target, CaptureRequested, ReadbackRequested, Mode}`, `IRenderer::SetDefaultRecipeBackbufferReadbackBuffer`, `Runtime.RenderArtifactPublication.cppm` (`SavedToFile`), `ApplyEditorVisualizationRecipeCommand`, `Runtime.CameraControllers.cppm` (`CameraFocusTarget`), `Runtime.CameraFocusCommand.cppm`, `Graphics.Colormap.cppm`.
- Command: `EditorViewCaptureCommand { OutputPath; Width, Height (≤ 4096); CaptureCamera{ Preset{Current, Front, Back, Left, Right, Top, Bottom, Isometric}, optional eye/target/up, FitToEntity }; optional {entity, GeometryPropertyRef, Colormap::Type, range}; Legend }` → `Pending` with an artifact id; completion publishes `SavedToFile` and restores the camera and prior visualization.
- Paths are checked against the caller's allowed roots (ARCH-019) when invoked by an agent.

## Control surfaces
- Config: N/A (per-call command; capture resolution defaults follow GRAPHICS-109's render-output config).
- UI: File > Save Screenshot…, F12 and View > Screenshot (UI-062, retired); presets and legend controls join that window.
- Agent/CLI: `view_capture {…}` (mutating: writes a file; returns MCP `image` content capped at 2048² unless `path_only:true`, plus camera/range/colormap/artifact metadata); `--capture` in RUNTIME-282.

## Required changes
- [ ] `Runtime.ViewCaptureOperations.cppm` + implementation: preset camera math from entity bounds, visualization apply/restore, capture request through the GRAPHICS-109 path, completion polling and artifact publication; fails closed with `DeviceUnavailable` on Null.
- [ ] Colormap strip composited into the image (optional flag) and legend range returned as metadata.
- [ ] Agent operation registered.

## Tests
- [ ] CPU tests: preset camera math (fits bounds, deterministic), legend strip compositor, fail-closed on Null with no file written, path outside roots refused.
- [ ] `gpu;vulkan` readback smoke (extending `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp` support): PNG dimensions and colored-versus-clear pixel classification for a front preset.

## Docs
- [ ] GRAPHICS-109's capture doc gains the editor/agent command; module inventory regenerated.

## Acceptance criteria
- [ ] The same command produces the capture from UI, CLI and agent paths; camera and visualization state restored afterwards.
- [ ] Null/non-operational devices fail closed with a diagnostic and no partial file.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ViewCapture|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ViewCapture|Capture' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- A second PNG encoder or readback path; faking captures on Null.
- Writing outside allowed roots when invoked by an agent.

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the `gpu;vulkan` smoke; on Null hosts the command reports unavailable. `Operational` owned by RUNTIME-281 (capture substrate owned by GRAPHICS-109).
