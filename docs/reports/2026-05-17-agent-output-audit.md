# Agent-output Weekly Audit — 2026-05-17 (calibration)

This is the first calibration audit run against
[`docs/agent/agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md).
It validates that the checklist is actionable on a real recent slice of
agent-authored work, as required by
[`REVIEW-001`](../../tasks/archive/REVIEW-001-human-led-agent-week-review-cadence.md).

## Window

- Date range: 2026-05-14 → 2026-05-15.
- Tasks covered:
  [`GRAPHICS-033E`](../../tasks/archive/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md),
  [`GRAPHICS-033F`](../../tasks/archive/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md),
  [`HARDEN-066`](../../tasks/archive/HARDEN-066-ecs-render-sync-export-policy.md),
  [`RUNTIME-091`](../../tasks/archive/RUNTIME-091-promoted-ecs-system-bundle-activation.md).
- Commits inspected:
  - `be7decf` GRAPHICS-033E Wire BarrierValidationClean operational gate.
  - `c3fe597` GRAPHICS-033E Use recipe-aware validator only for gate-7 publish.
  - `8bb3c17` GRAPHICS-033E Retire to tasks/done after slice 2 verification.
  - `cebf0ef` GRAPHICS-033F Wire PublicServiceReconciled operational gate.
  - `881aa8e` GRAPHICS-033F Retire to tasks/done after CPU gate confirmation.
  - `bb8f20e` HARDEN-066 forward WorldUpdatedTag into DirtyTransform via ECS RenderSync.
  - `eeb0680` HARDEN-066 backfill commit reference in done task file.
  - `7c088f6` RUNTIME-091 activate promoted ECS system bundle in fixed-step runtime.
  - `32f2239` RUNTIME-091 backfill commit reference in done task file.

Total: 9 non-merge commits across 4 tasks. Five are substantive
code/test/doc commits; four are task-file maintenance (one self-correction,
two retires, two backfills).

## Findings

| Row | Failure mode | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Silent scope creep | pass | `git diff --stat` for each substantive commit stays inside the touched task's `Required changes` scope. `bb8f20e` touches only `src/ecs/Systems/`, `src/runtime/Runtime.EcsSystemBundle.{cpp,cppm}`, READMEs in the same subtrees, parity matrix, and the new `Test.ECS.RenderSync.cpp` — all named in HARDEN-066. `7c088f6` adds `Runtime.EcsSystemBundle.{cpp,cppm}` + contract test + parity-matrix/README sync, all named in RUNTIME-091. `be7decf` + `cebf0ef` stay inside `src/graphics/renderer/`, `src/graphics/rhi/`, `src/graphics/vulkan/`, the contract tests they exercise, and the matching backlog cross-links — all called out by GRAPHICS-033E/F. |
| 2 | Decorative comments and docstrings | pass | The added comment blocks in `src/ecs/Systems/ECS.System.RenderSync.cppm` document the producer/consumer ordering between `TransformHierarchy`, `BoundsPropagation`, and `RenderSync` (a non-obvious cross-pass contract), not what each line does. The `Stats` fields could be slightly tighter, but they are genuine cross-module documentation. The `NoteRecipeGraphValidation(bool)` comment in `RHI.Device.cppm` explains *why* the surface exists (operational-gate input) — a real WHY note. No multi-paragraph blocks on internal helpers. |
| 3 | Premature abstraction | pass | New seams in this window each have a concrete consumer in the same commit. `IDevice::NoteRecipeGraphValidation(bool)` is called from `Graphics.Renderer.cpp::ExecuteFrame` and overridden by `VulkanDevice` — single producer, single consumer, no factory/builder layer. `VulkanCommandContext::IsBound()` is an inline predicate used inline by `HasOperationalSafetyPrerequisites()`. `RegisterPromotedEcsSystemBundle` is the runtime entry point for the bundle and is called from `Engine::RunFrame` in the same commit. No `Make*`/`Build*` helpers, no single-method interfaces added without callers. |
| 4 | Documented-but-not-tested | pass | Each behavioral doc claim has a corresponding test. RenderSync ordering claims in `src/ecs/Systems/README.md` are exercised by the FrameGraph case in `Test.ECS.RenderSync.cpp`. The RUNTIME-091 frame-loop description in `src/runtime/README.md` is enforced by the code-inspection layering test in `Test.RuntimeEngineLayering.cpp`. The gate-7 publish documented in `src/graphics/renderer/README.md` is asserted by `RendererFrameLifecycle.PublishesRecipeGraphValidationOnSuccessfulCompile`. The gate-8 preconditions listed in `src/graphics/vulkan/README.md` §10 are covered by `Test.VulkanFailClosedContract`. |
| 5 | Defensive validation at internal boundaries | findings (self-corrected, no follow-up required) | `be7decf` introduced a redundant AND-clause in `Graphics.Renderer.cpp::ExecuteFrame` that combined the recipe-aware validator with `m_RenderGraph.GetLastCompileValidationResult().HasErrors()`. The bare compile-time validator lacks recipe context, so any imported write from a non-side-effect pass (e.g. `CullingPass`) tripped `UnauthorizedImportedBufferWrite` and the AND-clause short-circuited to `false`, preventing gate 7 from ever flipping to `true`. This is a textbook Row-5 instance: two validators combined where one already carries the contract. The self-correction landed in `c3fe597` within the same task and same window, removing the redundant clause and recording the rationale in the surrounding comment. No outstanding follow-up task is required because the fix is in tree; the audit calibrates the row as actionable. |
| 6 | Untracked compatibility shims | pass | `git grep -nE 'TODO\|FIXME\|deprecated\|shim\|backcompat\|temporary'` on the diff of each substantive commit returns no new shim/back-compat phrasing. `ECS.System.RenderSync.cppm` removes its prior `TODO` placeholder rather than adding one. No new entries appear in `tools/repo/layering_allowlist.yaml`. |
| 7 | Ceremony without shipped value | pass | Five substantive code-bearing commits (`be7decf`, `cebf0ef`, `c3fe597`, `bb8f20e`, `7c088f6`) ship in this window vs. four task-file-only commits. Substantive commits collectively add `Extrinsic.Runtime.EcsSystemBundle`, the promoted `ECS.System.RenderSync`, the `BarrierValidationClean` + `PublicServiceReconciled` operational-gate inputs, and matching contract tests — measurable engine-behavior progress, not pure ceremony. The four task-maintenance commits are the §11 task-execution-workflow's normal close-out (retire to `tasks/done/`, backfill commit reference). |
| 8 | Half-finished implementations | pass | Every new public symbol introduced this window has a non-test call site or an end-to-end test in the same commit. `RegisterPromotedEcsSystemBundle` is invoked from `Engine::RunFrame`. `ECS.System.RenderSync::RegisterSystem` is invoked from `RegisterPromotedEcsSystemBundle`. `IDevice::NoteRecipeGraphValidation` is called from `Graphics.Renderer.cpp::ExecuteFrame` and overridden by `VulkanDevice`. `VulkanCommandContext::IsBound()` is consulted by `HasOperationalSafetyPrerequisites()`. `Test.RuntimeEcsSystemBundle.cpp` exercises the parent/child world-matrix composition end-to-end. |
| 9 | Aspirational documentation without `(planned)` marker | pass | `bb8f20e` and `7c088f6` both *retire* "(planned)"/"activation deferred to RUNTIME-091" wording from `docs/migration/nonlegacy-parity-matrix.md`, `src/ecs/Systems/README.md`, and `src/runtime/README.md` — replacing them with current-state assertions that the commits' tests enforce. `src/graphics/vulkan/README.md` §10 documents gate-7 and gate-8 inputs after they were wired (not before). No new present-tense claim was found that the current source does not deliver. |

## Follow-ups

- None. Row 5 has a historical finding (`be7decf`'s redundant AND-clause)
  that was self-corrected in `c3fe597` within the same task and window;
  the calibration records the pattern so future audits recognise it, but
  no new task is needed.

## Calibration note

The audit completed under the 60-minute target (see "Elapsed time"
below). The checklist's nine rows were each decidable from `git
diff`/`git show`/`git grep` against the four window tasks and their
linked task files; no row required deep code exploration to reach a
verdict. Row 5 produced the only finding and was the most informative —
the self-correction pattern (over-restrict in slice 1, remove the
redundant validator in slice 2) is exactly the kind of multi-PR signal
the per-PR review checklist cannot catch in isolation. Future reviewers
should expect Row 5 and Row 8 (dead seams) to be the rows that most
often produce findings.

## Elapsed time

- Start: 2026-05-17T14:40:40Z.
- Finish: 2026-05-17T14:46Z.
- Total: ≈ 5 minutes for the audit itself (well under the 60-minute
  target). Authoring the cadence checklist, contract/roles cross-links,
  and this report took another ≈ 10 minutes of docs-only work in the
  same session; the figure to calibrate against the 60-minute budget is
  the audit-only ≈ 5 minutes, since subsequent reviewers will reuse the
  checklist rather than author it. As a conservative figure, total
  session time for the calibration run was ≈ 15 minutes.
