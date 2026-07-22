# Clean-workshop review — RUNTIME-190 GPU property texture bake

## Change under review

- Change: retire
  [`RUNTIME-190`](../../tasks/done/RUNTIME-190-gpu-property-texture-bake-module.md)
  after moving interactive mesh-property texture baking into an app-composed
  runtime module with a graphics-owned GPU UV-raster recorder, generated-asset
  lifecycle, renderer-consumer bindings, and app-owned editor controls.
- Trigger(s): adds a renderer subsystem surface, changes runtime composition
  order and asset-workflow borrowing, and closes a Vulkan capability task.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` and the strict layer check scanned 753 files, 6,794 import/include references, and 85 CMake links with no violation or allowlist entry. |
| 2 | CMake target links match layer policy | pass | The existing graphics-renderer and runtime module targets gained their owning source/interface units; no `target_link_libraries(...)` edge or target ownership changed. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | `Extrinsic.Graphics.PropertyTextureBake` exports only core, RHI, graphics, and standard DTO vocabulary. Runtime owns every live ECS, `AssetService`, module, and world-binding borrow; no Vulkan type crosses the graphics/runtime surfaces. |
| 4 | Renderer member/subsystem growth justified by an owning seam | pass | `PropertyTextureBake` is the named graphics planning/recording seam, and retained pipeline/resource lifetime is owned by the `TextureBakeModule` GPU participant rather than another renderer field. The runtime module is justified by editor/agent and import-policy consumers plus cross-frame GPU shutdown ordering. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-recipe pass or command-router entry was added. The bake participant records a typed plan into the existing final graphics command context. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, resource declaration, or frame-graph ordering edge changed. Module ordering only ensures the bake producer exists before `AssetWorkflowModule` borrows it. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The task closes at `Operational`: the exact capable-host Vulkan acceptance smoke passed 1/1 with zero skips in 25.15 seconds and proved representative vertex, face, and nearest-edge readback plus lifecycle/render behavior. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No layering exception, allowlist row, warning-mode gate, or temporary shim was added. The user-approved standalone CPU helper remains an explicitly supported compatibility/test API and is unreachable from the live module/editor route; the interactive path has no CPU fallback. |

## Findings → follow-ups

- No findings.
- The unrelated GLFW/X11 LeakSanitizer recurrence observed by the full CPU
  selector is separately tracked by
  [`BUG-118`](../../tasks/backlog/bugs/BUG-118-glfw-x11-input-method-lsan-recurrence.md).
