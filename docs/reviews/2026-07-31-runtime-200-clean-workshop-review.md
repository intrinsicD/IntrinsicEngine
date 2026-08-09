# Clean-workshop review — RUNTIME-200 staged asset import

## Change under review

- Change: retire
  [`RUNTIME-200`](../../tasks/done/RUNTIME-200-staged-asset-import-materialization-recipe.md)
  after consolidating runtime asset import behind one staged recipe and the
  existing app-composed `AssetWorkflowModule`, then deleting the public
  pipeline, role callback registries, IO bridges, and handoff facades.
- Trigger(s): changes runtime composition and public module ownership, removes
  cross-layer callback seams, and closes a parity/cleanup task at `Retired`.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` scanned 717 source files, 6,333 import/include references, and 85 CMake links with no violation or allowlist entry. Assets remain CPU-only; runtime alone imports and composes ECS, graphics residency, and lower decoders. |
| 2 | CMake target links match layer policy | pass | Private runtime implementation file sets replace public orchestration modules inside the existing runtime target. No new target or `target_link_libraries(...)` edge was introduced. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The only retained public orchestrator is `Extrinsic.Runtime.AssetWorkflowModule`; lower assets and geometry APIs expose CPU payloads, IDs, decode/export functions, and no runtime, ECS, graphics, or Vulkan ownership. No `Vk*` type crosses an RHI or renderer API. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, pass, or graphics service was added. Existing runtime residency calls continue through `GpuAssetCache`. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or command-routing surface changed. The seven import stages use the typed `AssetImportStage` vocabulary. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, resource declaration, or frame-graph ordering edge changed. Import-stage order is recipe data owned by the runtime workflow. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The task closes at `Retired`: every promoted import route uses the staged workflow, deleted seams have no production consumers, CPU/ASan/UBSan route gates pass, and promoted-Vulkan import/model smokes prove the capable-host residency path. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No compatibility shim, layering exception, allowlist row, warning-mode gate, or replacement registry was added. The old public pipeline, bridges, callback registries, and handoff surfaces are deleted. |

## Architecture checklist result

- Ownership and lifetime are explicit: copied worker-stage values cross into a
  bounded, generation-revalidated main-thread apply; the workflow PImpl owns
  private helpers and invalidates queued asset callbacks on teardown.
- Reimport failure states remain explicit and transactional. Both direct and
  queued geometry reimport preserve `ExistingAsset`, advance its payload
  generation, and author no duplicate ECS entity.
- No new backend axis, config lane, hot-path blocking, performance claim, or
  benchmark obligation was introduced.
- Documentation, module inventory, task state, test labels, and strict
  structural validators are synchronized.

## Findings → follow-ups

- No RUNTIME-200 findings.
- The unrelated intermittent retired-scene-save terminal-event loss observed
  under ASan remains tracked by
  [`BUG-123`](../../tasks/done/BUG-123-retired-scene-save-terminal-event-race.md).
- The unrelated stale geometry-presentation Vulkan assertion remains tracked
  by
  [`BUG-124`](../../tasks/done/BUG-124-geometry-presentation-gpu-smoke-stale-unsupported-slot.md).
