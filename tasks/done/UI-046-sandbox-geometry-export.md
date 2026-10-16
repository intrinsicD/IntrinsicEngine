---
id: UI-046
theme: J
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: completed interactively under HARDEN-091; reviewed source and actual CPU evidence replace a custody report, retaining the task full-CPU requirement
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - io.geometry-format-capabilities
---
# UI-046 — Sandbox geometry export

## Goal
Export the selected mesh, graph or point cloud through the existing geometry
writers, with Runtime owning canonical entity projection and App owning the UI.

## Acceptance criteria
- [x] Runtime projects committed CPU geometry in object space into existing writers; App invokes its command through File / Export.
- [x] The existing format capability catalog disables unsupported domain/format pairs with a reason; no second catalog was added.
- [x] Supported properties are retained and dropped properties/topology are reported.
- [x] An imported mesh fixture exports and reloads with matching counts; cloud and graph supported channels are covered.
- [x] Invalid export requests preserve an existing destination; writer success precedes replacing the destination.
- [x] Real ImGui controls invoke the Runtime export command; no App-to-Geometry dependency is added.
- [x] Sandbox README documents the surface. The capability catalog did not move; the conditional geometry-IO documentation change is not applicable.
- [x] The task's full CPU verification was executed and all resulting failures were resolved with focused rechecks.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure --no-tests=error -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j 8
ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -R '^(SceneEditingGeometryExport|SandboxEditorPresentation|SandboxDomainPanels)\.'
python3 tools/repo/check_layering.py --root src --strict
```

## Completion and limits
Completed 2026-10-17. Commit: `761040df0`. Independent reviewer `/root/final_review`
accepted all original acceptance, implementation, test and applicable doc criteria.
See the canonical [completion and verification record](RETIREMENT-LOG.md#2026-10-17--src-workflow-follow-up).
The explicit micro re-profile records interactive completion under HARDEN-091;
the original full CPU requirement was retained and executed.

The export uses a text path field; native browsing stays with
[UI-047](../backlog/ui/UI-047-file-chooser-for-import-and-scene-paths.md).
GPU previews must be accepted into canonical CPU geometry before export.
The PLY writer emits radius, but the existing PLY reader does not materialize it;
tests inspect that written channel directly. No new format, batch/agent export,
scene-document format or GPU readback was added.
