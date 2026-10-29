---
id: RUNTIME-352
theme: F
depends_on: [RUNTIME-349]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: removal of a recorded layering exception; evidence is the diff and the source rule
contract_schema: 1
contracts: []
contract_review: two service modules stop including editor headers directly and reach the editor through the operation framework module; a layering cleanup with no behaviour change; no catalog contract covers module include structure
---
# RUNTIME-352 — Clustering and consolidation modules reach the editor only through the framework module

## Goal
- Remove the temporary exceptions (AGENTS.md §13) of the source rule
  `OperationModuleBoundary.ModulesIncludeNoEditorInternals` (`RUNTIME-349`): two files under
  `src/runtime/Modules` include editor headers directly:
  - `Clustering/Runtime.ClusteringModule.cpp`: `Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp`,
    `Editor/internal/Runtime.EditorMutation.Internal.hpp`;
  - `PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp`:
    `Editor/internal/Runtime.EditorPropertyHistory.hpp`, `Editor/internal/Runtime.EditorMutation.Internal.hpp`,
    `Editor/internal/Runtime.EditorFramedGpuJob.hpp`.
- The removal goes through `Extrinsic.Runtime.OperationRuntime` (export what they need there), or
  with their migration onto the operation runtime (k-means and consolidation are in ADR 0031's
  operation table).
- Expiry: 2026-12-31. After that the rule's owner lists are emptied, whatever state these files are in.

## Acceptance criteria
- [ ] Neither file includes an `Editor/internal/` or `Editor/Operations/` header; the rule's owner
      lists and include counts for them are removed.

## Verification
```bash
python3 tools/repo/check_source_rules.py --root . --suite OperationModuleBoundary
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
```
