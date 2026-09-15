---
id: RUNTIME-264
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; fixed-diff Claude review, compiler dependency checks and existing CPU tests.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality, geometry.property-coherence]
---
# RUNTIME-264 — Processing service and property-comparison locality

## Goal
- Reduce processing/editor compile dependencies and repeated property comparison
  implementations while preserving all existing workflows and lifecycle guards.

## Context and plan
- Operator explicitly requested continued compile-time/reuse work with Claude.
  Baseline is `015a3d852`; the preceding backlog reconciliation is a separate commit.
- Slice A: move the two existing borrowed service declarations and definitions
  into their existing Types owners. Editor service callers no longer import
  lifecycle modules; module composition still owns service binding and lifetime.
  Keep module-owned consolidation preflight behind the operation implementation.
  No new service, forwarding facade, module or allocation is introduced.
- Claude reviewed the initial plan read-only. Cross-module class friendship needs
  a matching C++-linkage declaration for the lifecycle class; service member
  definitions move with their owning Types module. Existing request/result types,
  subscriptions, private Bind and detach/epoch guards retain their semantics.
- Slice B: compare the repeated bit-exact numeric/property comparison helpers in
  mesh processing, Progressive Poisson, consolidation and clustering. Reuse one
  small private runtime header where contracts match, keeping topology tags,
  unsupported-type behavior and metadata checks in the caller.
- Right-sizing: the repeated mechanism has several current consumers. Prefer
  small constrained/ordinary overloads over repeated explicit specializations;
  do not create a general property visitor, policy framework or public module.
  Divergent numerical or topology semantics justify separate callers, not flags.
- RUNTIME-265 owns the remaining workspace/config dependency chain;
  GRAPHICS-138 owns the renderer interface profiling follow-up. This slice establishes dependency boundaries;
  no timing improvement is asserted without a matched benchmark.

## Acceptance criteria
- [x] Editor service interfaces/session do not depend on ClusteringModule or
      PointCloudConsolidationModule; composition and preflight owners still do.
- [x] Service availability, completion subscription, private binding and expired
      prepared-frame behavior pass the existing focused tests.
- [x] Repeated numeric comparison implementations share one owner, with signed
      zero, NaN payload, vector components tested; existing position cardinality guards retained.
- [x] Existing topology/unknown-property/stale-writeback guards remain intact.
- [x] Claude reviews the fixed diff; valid findings are fixed and affected
      verification passes, including the default CPU gate and structural checks.
- [x] Update owner documentation and module inventory; retire this bounded slice
      without presenting remaining measured hotspots as completed work.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'Clustering|Consolidation|SandboxEditorSessionLifecycle|GeometryValueComparison|EditorCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Review decisions
- Initial service compilation exposed a linkage mismatch in the lifecycle
  method definitions. Matching C++ linkage on declarations and definitions
  fixes it; the rebuilt runtime passes. No class layout or lifetime changed.
- Claude found mesh position comparison is numeric, unlike exact-bit property
  comparison. Keep that position loop unchanged. The shared BitEqual helper
  accepts only integral/scalar/vector values; typed materialization and caller
  topology/unknown-property handling remain intact. Its tiny inline overloads
  need no new named module or implementation translation unit.
- Claude's final source review reported no blockers. Its header self-containment
  concern was resolved by documenting the required global-module-fragment include:
  the mesh support header intentionally follows module imports. Moving the new
  include into that header would change module attachment. Claude accepted the
  resolution. Constrained comparison overloads intentionally require explicit
  semantics for future property types; existing unsupported-type gates remain.
- The first focused run exposed a source-ownership test still expecting the
  service declaration in its former module. Updated it to require the Types owner
  and reject a duplicate declaration in the lifecycle module, retaining all
  runtime-route assertions. Claude reviewed this correction without blockers;
  ReadFile already reports missing inputs through an assertion.
- Architecture review: services remain borrowed pointers bound by their lifecycle
  owner; no new allocation, forwarding hop, configuration state, synchronization,
  or lower-layer dependency. Changed comparison users preserve cardinality,
  publication, failure and topology checks. No renderer or backend behavior changed.
- Clean-workshop scorecard: rows 1–3 pass (strict imports/links and public ownership);
  rows 4–6 n/a (no renderer/pass/recipe change); row 7 n/a (behavior-preserving
  refactor, no backend maturity promotion); row 8 pass (no temporary exceptions).
  Automated clean-workshop checks pass.

## Completion
- Completed 2026-09-15. Commit reference: the enclosing refactor commit.
- Maturity: CPUContracted, the intended endpoint of this behavior-preserving
  ownership/reuse slice. No backend capability promotion is claimed.
- Configured the canonical `ci` preset with Clang 23; full `IntrinsicTests` build
  passed. Focused CTest: 96/96 passed after correcting the old owner assertion.
  Full exclusion-only CPU gate: 4,639 passed, zero failures, one expected
  ASan-only GLFW lifecycle skip (4,640 selected). No sanitizer or GPU runtime
  result is claimed for this slice.
- Strict layering, test layout, task policy, documentation links, explicit-file
  docs-sync and automated clean-workshop checks passed. Module inventory was
  regenerated (417 modules, unchanged inventory). Source synopsis audit on the
  seven touched interfaces/headers: zero errors; eight existing review items.
- Production source/header count across the 24 touched files changes from
  25,272 to 25,168 physical lines, net −104. One shared private header replaces
  repeated exact-value comparisons; no implementation unit, named module,
  service or compatibility path is added. The new tests are separate from that
  production line count.
- Compiler-derived checks prove the four selected editor producers do not import
  either lifecycle module. These are dependency assertions, not elapsed-time
  measurements. Remaining editor snapshot/config and renderer work belongs to
  RUNTIME-265 and GRAPHICS-138 and requires a fresh matched baseline.
