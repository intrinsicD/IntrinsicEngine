# Clean-workshop review — RUNTIME-129 operational normal bake

## Change under review

- Change: retire
  [`RUNTIME-129`](../../tasks/done/RUNTIME-129-schedule-gpu-normal-bake-after-import.md)
  after wiring the existing private runtime normal-bake queue to live
  `GpuWorld` residency, retained graphics bake resources, exact generated
  texture generations, default/editor producers, and transactional material
  binding.
- Trigger(s): changes a graphics residency boundary, records an offscreen
  renderer command path, and closes a Vulkan capability task.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` and `check_layering.py --root src --strict` scanned 749 files and found no violation or allowlist entry. |
| 2 | CMake target links match layer policy | pass | No CMake target or link edge changed. The work stays in the existing graphics and runtime targets. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | Graphics exports only RHI/graphics residency and bake DTOs; it imports no ECS, runtime, live asset service, app, platform, or Vulkan handle. Runtime owns every live composition borrow. |
| 4 | Renderer member/subsystem growth justified by an owning seam | pass | No renderer member or subsystem was added. Retained pipeline/dilation state remains private to the existing `ObjectSpaceNormalBakeService` participant composed by `AssetWorkflowModule`; `GpuWorld` gained only the already-justified plain residency view/accessor. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-recipe pass or command-router entry was added. The existing graphics bake recorder consumes a caller-provided command context and typed handles. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe or frame-graph dependency changed. Recording uses the existing runtime frame-command hook and does not acquire, present, or create a second frame path. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The task closes at `Operational`: the complete ASan+UBSan `gpu` + `vulkan` selector passed 47/47, including the real-app import/readback smoke and shutdown LeakSanitizer contract. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No compatibility shim, layering exception, allowlist row, warning-mode gate, or unowned TODO was introduced. CPU texture generation remains only the pre-existing compatibility route when no GPU queue is composed. |

## Findings → follow-ups

- No findings.
