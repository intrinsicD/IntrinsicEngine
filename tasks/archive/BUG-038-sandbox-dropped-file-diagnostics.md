---
id: BUG-038
theme: G
depends_on: [UI-007, BUG-021, BUG-027]
maturity_target: CPUContracted
---
# BUG-038 — Dropped file imports fail silently in the sandbox

## Goal
- Make sandbox dropped-file imports produce deterministic runtime diagnostics at receipt, queue/routing, and completion so failed drops are observable without relying on an open editor panel.

## Non-goals
- No new file-format importers or asset IO semantic changes.
- No renderer, GPU upload, Vulkan, shader, or material behavior changes.
- No native file-dialog work; OS drag/drop and path-entry import remain the supported boundaries.
- No platform backend replacement or new platform event ownership model.

## Context
- Symptom: dropping a mesh file, and likely any unsupported or invalid file, can appear to do nothing because there is no runtime log after the file-drop event fails or queues deferred geometry work.
- Expected behavior: a drop should leave a breadcrumb when runtime receives it, when it queues or routes each path, and when the import succeeds or fails with the error code/payload kind.
- Impact: existing coverage proves direct import, runtime platform-event dispatch, and GPU visibility paths, so asset IO and GPU upload are not the first suspected owners. The missing observable is the runtime event/diagnostic boundary around deferred dropped imports.
- Owner/layer: `runtime` owns platform drop handling, import facade composition, streaming executor handoff, and import-event recording. `platform` only emits `WindowDropEvent`; `assets` only provides route/decode results.
- Ranked hypotheses tested by this task:
  1. Drop handling is working through the runtime facade, but failed/queued drops are silent because `HandleWindowDropEvent`, `ImportDroppedFilePaths`, `QueueDroppedGeometryImport`, and `RecordAssetImportEvent` do not log.
  2. Existing tests bypass live backend queue timing by calling `DispatchPlatformEventForTest`; add a failure-status regression at the runtime handler seam first, then defer native OS injection unless this seam fails.
  3. Streaming applies geometry import results on a later frame; a missing status before completion is expected, but a missing completion diagnostic is a bug.
  4. GPU upload/rendering is downstream of the reported silence; do not touch it unless the CPU/null diagnostic regression proves import materialization succeeds but render extraction fails.

## Completion
- Completed: 2026-06-12. Commit/PR: this retirement commit.
- Root cause: the runtime recorded `RuntimeAssetImportEvent` state after dropped imports, but it did not emit logs at drop receipt, route/queue decision, or shared completion. Invalid/missing dropped files therefore looked like a no-op outside the editor status panel even though the platform event and import facade were reachable.

## Required changes
- [x] Add focused regression coverage proving failed dropped imports leave runtime log entries and a failed `RuntimeAssetImportEvent`.
- [x] Log dropped-file receipt and per-path routing/queue decisions from `Engine`.
- [x] Log import completion from `RecordAssetImportEvent` for both success and failure, including path, payload kind, and error code.
- [x] Keep deferred geometry decode/conversion off the platform polling path and keep `AssetService`/ECS mutation on the main-thread apply phase.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Add/extend `contract;runtime` coverage in `Test.SandboxEditorUi.cpp`.
- [x] Run the focused dropped-import diagnostic regression.
- [x] Run the focused sandbox import/drop subset.
- [x] Run relevant structural checks.

## Docs
- [x] Update runtime/sandbox docs only if the public behavior description changes.
- [x] Update `tasks/backlog/bugs/index.md`, retire the task record, append the retirement narrative, and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] Dropping an invalid mesh path produces a log at drop receipt, queue/routing, and failed completion.
- [x] The last import event records the failed path, requested payload kind, and error code.
- [x] Valid dropped mesh imports still materialize through the deferred runtime path and remain selectable.
- [x] The fix does not introduce layering violations.

## Verification
```bash
# Red gate before the fix:
#   SandboxEditorUi.DroppedFileImportFailureLogsDiagnostics failed because no
#   "File drop received", "Queued dropped geometry import", or
#   "Asset import failed" log entries were emitted for a missing OBJ drop.

cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi\.DroppedFileImportFailureLogsDiagnostics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 1/1 tests.

ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi\.(PlatformDropEvent|DroppedFilePaths|DroppedFileImportFailureLogsDiagnostics|EngineImportFacade)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 7/7 tests.

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2993/2993 tests.

python3 tools/agents/check_task_policy.py --root . --strict
# Passed: findings=0.

python3 tools/docs/check_doc_links.py --root .
# Passed: no broken relative links.

python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
# Passed: docs sync rules satisfied.

python3 tools/repo/check_layering.py --root src --strict
# Passed: no layering violations.

python3 tools/repo/check_test_layout.py --root . --strict
# Passed: findings=0.

git diff --check
# Passed.
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Moving file import authority into platform, graphics, or UI layers.
- Reintroducing synchronous geometry decode/conversion inside platform polling.
- Treating GPU/Vulkan visibility as the owner of missing drop diagnostics without a CPU/null repro.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed because this task fixes backend-neutral runtime diagnostics around file-drop/import state. Native OS drag/drop visual proof remains covered by the existing sandbox acceptance lanes.
