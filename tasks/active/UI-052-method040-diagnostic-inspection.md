---
id: UI-052
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive diagnostic-access request; focused executable tests and ordinary source review cover the integration without adopting the research method or claiming quality."
owner: codex
branch: codex/method-040-boundary-partition
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-06T19:33:00+00:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "Adds a diagnostic method choice to the existing validated runtime/config/UI operation and same-topology property transaction. No new layer boundary, backend, or geometry eligibility rule."
---
# UI-052 — METHOD-040 diagnostic engine inspection

## Goal
- Let the operator run the tested METHOD-040 curves profile from the existing
  Mesh / Processing / Curvature panel, with config/agent parity and truthful
  method identity, result diagnostics, and undoable visualization properties.

## Context
- The operator asked to continue working with Claude and test METHOD-040 in the
  engine/UI. This is an explicit diagnostic-access direction, analogous to
  BUG-163; it does not change METHOD-040's negative adoption oracle or defaults.
- Claude reviewed the bounded abstract integration via MCP. Its useful findings
  were absent unavailable component labels, atomic history, stable profile
  identity, and consistent config/UI semantics. Repository/data payloads were
  not sent. Valid inactive GMM settings are preserved for switching methods;
  they are documented as unused by METHOD-040, rather than rejected or erased.
- The fixed profile is serialized by method token `feature_boundary_curves_v1`.
  Its geometry helper is shared by the native runner and runtime. The existing
  feature-radius and hard-dihedral controls remain active; other optimizer
  settings do not affect this profile.

## Non-goals
- No positive adoption, default replacement, semantic-quality claim, GPU
  backend, topology editing, or new algorithm tuning. METHOD-040 remains the
  owner of its failed oracle and formal-custody blocker.

## Engine integration

| Field | Decision |
| --- | --- |
| Least-structured input | Existing canonical embedded triangle mesh sources, finite positions and adjacency; point sets do not supply required faces. |
| Compatible entity sources | Every mesh accepted by the shared curvature-segmentation preflight. |
| RuntimeModule | Existing geometry-processing command; detached solve then existing validated property/history transaction. |
| Config/agent | Existing serialized section and preview/apply path, versioned method token. |
| UI | Existing Curvature window, explicitly experimental METHOD-040 selection and relevant controls only. |
| Publication | Face regions/colors and edge evidence/boundaries/roles/colors preserve topology and unrelated fields; absent fitted-component output removes the prior method-owned component property atomically and undo restores it. |
| End-to-end tests | Config-source parity, runtime publication/history/failure, existing headless UI and visualization contracts; rebuilt Sandbox for operator testing. |

## Acceptance criteria
- [x] Config round-trip and all apply sources recognize the versioned diagnostic token; METHOD-037 stays the default.
- [x] Runtime publishes complete face/edge results with actual METHOD-040 diagnostics and no fabricated fitted components.
- [x] Removal-only, undo/redo, no-change, and failed-input cases preserve the property/history contract.
- [x] UI exposes the experimental choice, hides irrelevant GMM controls, and uses the existing run/visualization path.
- [x] Shared fixed profile matches the prior native comparison constants; tests/build pass and user can run the rebuilt Sandbox.
- [x] Method docs and active METHOD-040 record distinguish this diagnostic access from adoption.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryFeaturePartitionTests IntrinsicCurvatureBoundaryMesh
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentationOperations|SandboxCurvatureSegmentationPanel|SandboxConfigSections.CurvatureSegmentation|CurveCoverageV1Profile' --timeout 120
cmake --preset dev
cmake --build --preset dev --target ExtrinsicSandbox
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Review
- Claude's abstract design review informed absent fitted-component publication,
  versioned profile identity, and atomic history. Source review is local.
- The removal-only case exposed the existing value counter returning zero for
  a removed field; this path now counts removed fitted-component slots.
- UI review also guarded inactive GMM/patch clamping so selecting METHOD-040
  preserves valid inactive settings rather than silently changing them.
- Automated clean-workshop and strict layering checks pass. Manual rows:
  exported result types are CPU values; renderer ownership, pass IDs, and recipe
  dependencies are unchanged; no maturity adoption or temporary exception is
  introduced. The diagnostic input remains borrowed only during the synchronous
  call, and publication uses the existing detached-mesh/history transaction.
- The rebuilt native profile produces byte-identical frog labels and edge
  records to the saved overnight curves run. This is a profile-refactoring
  check, not new quality, stability, or performance evidence.

## Verification result — 2026-09-06
- `ci` configured successfully; the four focused targets built successfully.
  All ten selected CTest cases passed, with no skips. This turn used focused
  CPU coverage; the prior overnight sanitizer runs are separate evidence.
- `dev` configured and `ExtrinsicSandbox` built successfully at
  `build/dev/bin/ExtrinsicSandbox`. The running operator instance was left
  intact and needs restarting to load the rebuilt executable.
- Strict layering, task policy, doc links, test layout, method manifests,
  root hygiene, and automated clean-workshop checks pass. The module inventory
  was regenerated. No desktop click-through or new quality verdict is claimed.
