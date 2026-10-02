---
id: RUNTIME-277
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# RUNTIME-277 — Structured `ActionReadiness` reasons

## Goal
- Let action readiness report every independent blocking reason with a code and the
  offending config field, so the UI can mark the exact control and an agent knows
  which parameter to change, instead of one first-failure string.

## Non-goals
- No restructuring of the cached point-input readiness (`EditorPointInputReadinessState`).
- No family-wide readiness consolidation: UI-037 (active) owns that; this task introduces the type and adopts it in the mesh-field family only.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- `ActionReadiness { bool Enabled; std::string DisabledReason; }` in `src/runtime/Editor/Runtime.EditorProcessing.cppm`; `ResolveEditorProcessingActionReadiness`; `Target()`-style functions (e.g. `Runtime.MeshFieldOperations.Smoothing.cpp`) return the first failing string; `DrawProcessingActionButton`/`DrawDisabledReasonTooltip` show one string.
- Coordination: [UI-037](../active/UI-037-linear-domain-action-readiness-tooltips.md) (active) owns readiness across all families. Land this as a small type-introducing slice that UI-037's remaining slices adopt; check UI-037's current continuation before starting and do not duplicate its cached verdicts. This task does not edit UI-037's note; the UI-037 owner records adoption there.
- Design: `enum class ActionReadinessCode : std::uint8_t { Ok, WorkspaceUnavailable, MissingEntity, WrongDomain, MissingProperty, IncompatibleProperty, ElementCountMismatch, InvalidConfig, ConflictingOptions, DeviceUnavailable, KernelUnavailable, JobActive, StaleInput, PendingVerdict }`; `ActionReadinessReason { Code; Field /* schema key, empty = whole action */; Message; }`; `ActionReadiness` gains `std::vector<ActionReadinessReason> Reasons` and `DisabledReason` stays the first reason's message (existing `{ok, diagnostic}` aggregate initializations keep compiling); `MakeActionReadiness(std::vector<ActionReadinessReason>)` fills both.

## Slice log
- Slice 1: `ActionReadinessCode` (plus `Unclassified` for text-only producers),
  `ActionReadinessReason`, `ActionReadiness::Reasons`, `MakeActionReadiness`,
  `ActionReadinessReasons` and `ToString`; `ResolveEditorProcessingActionReadiness` prepends
  `WorkspaceUnavailable`; `ReadinessWhileGpuRunPending` reports `JobActive`. Config reasons come
  from `CollectDeclaredFieldErrors` (one `InvalidConfig` per out-of-range field with its schema key)
  through `AppendConfigReadinessReasons`. The four mesh-field `Target()` owners collect config,
  property and device reasons; `preview_*` answers `reasons[]` (`code`, `field`, `message`).
  Multi-fault tests in the four family suites, a field-key test and an agent preview test
  (`EditorMeshFieldAgent`). UI-058 can now render all reasons and field markers.

## Control surfaces
- Config: `Field` uses the section schema key from RUNTIME-276.
- UI: UI-058 lists all reasons and marks offending controls.
- Agent/CLI: every `*_preview`/`*_run` agent operation returns `readiness.reasons[]` (code name, field, message); a disabled action is a normal result, not `isError`.

## Required changes
- [x] Add the code enum, reason struct, `Reasons` field, `MakeActionReadiness` and `ToString(ActionReadinessCode)`.
- [x] `ResolveEditorProcessingActionReadiness` prepends a `WorkspaceUnavailable` reason.
- [x] Mesh-field family (`Smoothing`, `Eigenbasis`, `HarmonicField`, `Gradient` owners): `Target()` collects independent reasons instead of returning early; dependent checks still short-circuit.
- [x] Agent operations for these families serialize the reasons.

## Tests
- [x] `Test.PropertySmoothingOperations.cpp` (and the other adopted family tests) assert full reason lists for multi-fault configs, including `Field` keys.
- [x] Existing readiness tests stay green.

## Docs
- [x] `docs/architecture/sandbox-editor-feature-boundaries.md` (readiness section) documents codes and field keys; module inventory regenerated.

## Acceptance criteria
- [x] Multi-fault mesh-field configs report every independent reason with correct codes and fields; `DisabledReason` equals the first reason.
- [x] No behavioral change for enabled actions; all existing callers compile unchanged.

## Completion

Commit: `c45f47926`, `6dffa4d4a`. Completed 2026-10-02 with an independent Opus review.
- `ActionReadiness` carries structured reasons (code, field, message). `DisabledReason` stays the first message.
- The mesh-field owners collect independent reasons, and dependent checks still short-circuit.
- Config range faults name their `ConfigFieldSpec` field; a valid config allocates nothing.
- Agent `preview_*` returns `reasons[]` alongside `reason`.
- Full CPU suite green.
- Other operation families adopt the reasons under UI-037; the UI tooltip and field markers come in UI-058.
- Maturity: CPUContracted.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PropertySmoothing|Eigenbasis|HarmonicField|ScalarGradient|EditorProcessing' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Restructuring UI-037-owned cached readiness or adopting other families here.
- Removing `DisabledReason`.
