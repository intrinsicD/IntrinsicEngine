# Clean-workshop review — GRAPHICS-115 object-space normal dilation

## Change under review

- Change: retire
  [`GRAPHICS-115`](../../tasks/done/GRAPHICS-115-object-space-normal-gpu-dilation.md)
  by adding graphics-owned object-space normal bake dilation resources,
  ping-pong render/fragment command recording, Vulkan sampled bridge slot
  reservations, and CPU/Vulkan coverage.
- Trigger(s): RHI/Vulkan descriptor ownership changed and an operational
  graphics task was retired.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` ran `check_layering.py --root src --strict`; no violations. |
| 2 | CMake target links match layer policy | pass | No new CMake link edge was added. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | `Extrinsic.Graphics.ObjectSpaceNormalTextureBake` exports only graphics/RHI/core data surfaces; it does not name ECS/runtime/platform/app types. |
| 4 | Renderer member/subsystem growth justified by an owning seam | pass | Dilation lives in the existing object-space normal bake module and shader asset, not as a new renderer member or subsystem. |
| 5 | New passes use typed IDs, not string routing | n/a | The slice records a standalone bake/dilation command sequence; no frame-recipe pass or route was added. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame-recipe dependencies were added. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `GRAPHICS-115` retires at `Operational`; runtime import scheduling remains tracked by `RUNTIME-129`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No layering allowlist row or temporary compatibility exception was added. |

## Findings -> follow-ups

- No findings.
