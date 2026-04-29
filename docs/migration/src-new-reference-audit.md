# `src_new` Reference Audit — 2026-04-29

## Scope

This audit supports `HARDEN-030` by classifying tracked references to `src_new`, `src-new`, or `src new` after the source-layout reorganization.

Legacy retirement is out of scope. This audit does not rename files, move shader assets, or edit stale references except for linking this report from the migration index.

## Grep basis

The classification is based on the exact command requested by `HARDEN-030`, run before creating this audit file:

```bash
git grep -n "src_new\|src-new\|src new" -- .
```

After this file exists, future grep runs will also find this audit. Treat this file as `migration-ok` audit evidence, not as a stale active reference.

## Classification key

- `historical-ok`: old reports, reviews, or completed migration trackers that intentionally preserve historical wording.
- `migration-ok`: migration-scoped documents or tooling compatibility paths that explicitly describe the old migration state.
- `active-stale`: active code, assets, canonical docs, tests, or task names that still imply `src_new` exists or is the current architecture.
- `false-positive`: unrelated text; none were found in this pass.

## Active-stale references

These require cleanup follow-up tasks.

| Match location(s) | Classification | Cleanup task | Rationale |
|---|---|---|---|
| `assets/shaders/instance_cull.comp:6`; old shader subpaths under the former shader `src_new/` root (`culling`, `deferred`, `forward`) | active-stale | `HARDEN-031` | Active shader paths and include strings still used the old `src_new` common include root. |
| `tests/Graphics/Test.Graphics.GpuWorldAndCulling.cpp:138`; `tests/Graphics/Test.Graphics.MinimalTriangleAcceptance.cpp:151` | active-stale | `HARDEN-031`, `HARDEN-041` | Old graphics tests still refer to `shaders/src_new/...`; test taxonomy cleanup owns whether these sources move, update, or remain archived. |
| `src/graphics/rhi/RHI.Types.cppm:111` | active-stale | `HARDEN-031` | Active source comment referenced the old shader common include path; it should follow the shader path rename. |
| `src/app/README.md:3-4,25`; `src/core/README.md:3-4`; `src/ecs/README.md:3-4`; `src/graphics/renderer/README.md:3,98-99`; `src/platform/README.md:3`; `src/runtime/README.md:3` | active-stale | `HARDEN-032` | Canonical source-tree READMEs still describe promoted layers as `src_new` modules. |
| `src/ecs/Systems/ECS.System.TransformHierarchy.cppm:8` | active-stale | `HARDEN-032` | Active source comment still describes `src_new` ECS wiring as future work. |
| `docs/index.md:10,54,61` | active-stale | `HARDEN-032` | Canonical docs index still links `src_new` documents as active navigation entries. |
| `docs/architecture/patterns.md:5,500`; `docs/architecture/src_new-rendering-architecture.md:1,3,5`; `docs/architecture/src_new-task-graphs.md:1,3,7`; `docs/architecture/task-graphs.md:17` | active-stale | `HARDEN-032` | Canonical architecture docs still present `src_new` as current/in-progress architecture or have active filenames containing `src_new`. |
| `docs/roadmap.md:7,9,11-14` | active-stale | `HARDEN-032` | The roadmap still presents `src_new` reimplementation as current cross-cutting work. |
| `tasks/backlog/README.md:10`; `tasks/backlog/legacy-todo.md:10`; `tasks/backlog/rendering/RORG-031-rendering-pipeline.md:23`; `tasks/backlog/src-new/RORG-031-src-new-parity.md:1,4,11,25,29` | active-stale | `HARDEN-032` | Active backlog paths/task names still use `src-new` / `src_new` naming. |
| `tests/Runtime/Test.Runtime.EngineLayering.cpp:28,48,91,101-111` | active-stale | `HARDEN-041` | Old runtime test sources still inspect removed `src_new/...` paths; test taxonomy cleanup owns the move/update/archive decision. |

## Migration-ok references

These references are acceptable while migration records and compatibility tooling remain available.

| Match location(s) | Classification | Follow-up / allowance | Rationale |
|---|---|---|---|
| `docs/architecture/graphics.md:19`; `docs/architecture/index.md:51-53` | migration-ok | `HARDEN-032` may archive/rename later | These entries identify historical migration docs rather than current source ownership. |
| `docs/migration/active-status.md:5,66,75`; `docs/migration/index.md:3,18-19,25`; `docs/migration/source-tree-move-plan.md:3,16,41-50,57,68-69,78`; `docs/migration/source-tree-reorganization.md:10,25`; `docs/migration/src-new-status.md:1,3,16-24,28` | migration-ok | allowlisted by `HARDEN-033` unless archived first | These are migration-scoped status and move-plan documents. |
| `docs/migration/current-repo-inventory.md:30,39,54,56-64,78-79,102,120` | migration-ok | allowlisted by `HARDEN-033` | This is a factual migration snapshot of the old dual-tree repository. |
| `docs/migration/src_new_module_inventory.md:5,20-92` | migration-ok | allowlisted by `HARDEN-033` | This is an archived/generated migration inventory for the removed `src_new` tree. |
| `tools/repo/README.md:11`; `tools/repo/check_layering.py:86`; `tools/repo/generate_module_inventory.py:40` | migration-ok | `HARDEN-033` checker allowlist or later tooling cleanup | Tooling intentionally retains migration compatibility and final-layout inventory generation support. |
| `tasks/active/0001-post-reorganization-hardening-tracker.md:16,24,78,103-106,149-150,157` | migration-ok | current hardening tracker | The active hardening tracker owns `src_new` cleanup tasks and must mention the stale name until the phase closes. |

## Historical-ok references

These references intentionally preserve past decisions or old review context.

| Match location(s) | Classification | Rationale |
|---|---|---|
| `docs/migration/archive/plan.md:13,15-17` | historical-ok | Archived migration plan. |
| `docs/reports/2026-04-23-commit-metrics-analysis.md:18,46` | historical-ok | Historical commit metrics report. |
| `docs/reviews/2026-04-22-commit-review.md:8,45,50,56,64,69`; `docs/reviews/2026-04-27-task-graph-cutover-audit.md:3,8,30` | historical-ok | Historical review/audit records. |
| `tasks/active/0000-repo-reorganization-tracker.md:58,62,90,104-113,118` | historical-ok | Completed RORG tracker evidence. |
| `tasks/active/final-reorganization-audit.md:28` | historical-ok | Completed final reorganization audit evidence. |

## False positives

No false-positive matches were found.

## Follow-up summary

- `HARDEN-031`: Rename active shader asset paths and update shader includes/tests/source comments. Completed on 2026-04-29; the old active shader asset root moved to `assets/shaders/{common,culling,deferred,forward}/`.
- `HARDEN-032`: Rename or archive stale active docs/task names and update canonical navigation.
- `HARDEN-033`: Add a strict stale-reference checker with allowlists for historical and migration-only references.
- `HARDEN-041`: Move/update/archive old runtime and graphics test sources as part of test taxonomy cleanup.

