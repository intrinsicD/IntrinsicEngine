---
id: UI-054
theme: J
depends_on: [RUNTIME-231]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive simplification; implementation evidence will be the panel diff and integration tests.
contract_schema: 1
contracts: [geometry.element-domain-sources, repo.source-documentation, method.engine-integration]
---
# UI-054 — Share density and spacing panel workflow

## Goal
Replace repeated processing-panel scaffolding with one typed workflow in the
existing app support owner, starting with density and spacing. Preserve their
different parameters, statistics and user-visible behavior. This is the UI
follow-up from the [complexity review](../../docs/reviews/2026-09-11-processing-complexity-review.md);
the panel pilot is implemented and reviewed, with CPU and UI verification
passed. The note awaits commit/retirement.

## Engine integration
| Field | Preserved contract |
|---|---|
| Least-structured input | Runtime-provided typed property catalogs; no new app-owned geometry access. |
| Compatible entity sources | Every domain currently exposed by each method's canonical preflight. |
| RuntimeModule | Existing operation command handles and attachment guard. |
| Config/agent | Existing serializable config and validated preview/apply path. |
| UI | Shared draft/entity/input/output/run/show/status/dismiss behavior; typed custom parameter widgets. |
| Publication | Runtime-owned named properties and result retention; UI uses the existing Appearance apply path. |
| End-to-end tests | Processing-panel entity-following, explicit entity, canonical output visualization and config acceleration cases. |

## Acceptance criteria
- [x] Density and spacing share draft synchronization, selection tracking, property
  selectors, execution/readiness, output visualization and dismissal scaffolding.
- [x] Keep kernel-specific widgets and statistics explicit. No reflection DSL,
  dynamic plugin registry or mandatory universal panel description.
- [x] UI changes and config/agent changes use the same validated apply path;
  invalid drafts, disabled reasons and method-specific diagnostics remain visible.
- [x] Show-property actions use Appearance without recomputing; entity changes,
  explicit selections, empty selection and acceleration controls retain behavior.
- [x] Trace result ownership for these two panels. Keep lifetime-safe snapshots
  and typed session slots; remove a cache only if its authority is redundant.
  A whole-session erased result store is outside this task.
- [x] Net affected production code decreases including common helpers; record the
  count and remaining per-method custom code. Do not count moving code as deletion.
- [x] Run the relevant UI tests and full CPU gate; build the actual Sandbox and
  independently review the final diff. Update relevant ownership documentation.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -R '^(SandboxProcessingPanels|SandboxEditorPresentation|KernelDensity|PointSpacing)'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

Reintroduction trigger: a distinct panel interaction, such as selecting two
entities or editing topology constraints, keeps a custom section or window.
Reuse its common controls without adding method-name switches to the shared frame.

## Completion evidence

Implemented and reviewed on 2026-09-11; uncommitted, awaiting commit/retirement.
App footprint: 4609 → 4585 physical lines. Frame/session/panel result copies
retain the lifetime roles documented in
[editor boundaries](../../docs/architecture/sandbox-editor-feature-boundaries.md).
The final UI regression clicks both Run buttons, compares nondefault configured
outputs with runtime reference calls, and proves Show submits no job or scalar
rewrite. Combined verification and local evidence are recorded with
[RUNTIME-231](RUNTIME-231-shared-point-property-operations.md#completion-evidence)
and the [review](../../docs/reviews/2026-09-11-processing-complexity-review.md#implemented-pilot-and-reuse-discovery).
