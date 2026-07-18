---
id: HARDEN-086
theme: F
depends_on: [HARDEN-061]
maturity_target: CPUContracted
---
# HARDEN-086 — Guarded hierarchy query helpers

## Goal
- Add deterministic, corruption-aware child and descendant collection to the existing promoted hierarchy-structure module and replace two runtime-local traversal copies with it.

## Non-goals
- No new hierarchy module, iterator framework, scene-query service, traversal registry, or ECS ownership change.
- No ancestor/path/LCA/postorder API without a second real consumer.
- No refactor of transform propagation or the mutation-sensitive model-handoff detach loop.
- No runtime, graphics, RHI, asset-service, or physics handles in canonical ECS components.

## Context
- Owner/layer: `Extrinsic.ECS.Hierarchy.Structure`; `ecs -> core` plus existing EnTT/component dependencies only.
- The promoted module currently provides `IsDescendant` and invariant/mutation helpers but no safe enumeration result.
- `Runtime.EditorCommandHistory.cpp::AppendDescendantStableIds` recursively follows siblings with a visited vector and silently returns partial results on corruption.
- `Runtime.SandboxEditorFacades.cpp::AccumulateProgressiveCompositionSummary` independently follows only immediate siblings using `ChildCount` as a guard and can silently truncate malformed chains.
- `Test` contains broader hierarchy iteration ideas, but IntrinsicEngine needs only the two concrete query shapes used today.
- Right-sizing verdict: extend the existing structure module with one status
  enum, one result record, and two free functions used by the two present
  runtime consumers. No new file, interface, iterator, registry, or forwarding
  layer is justified; repository import search plus focused ECS/runtime tests
  bound the blast radius.

## Status
- Completed and retired at `CPUContracted` on 2026-07-18; owner: Codex team;
  implementation branch: `codex/harden-086-guarded-hierarchy-queries`.
- Implementation commit: `f310e872`; merged to `main` as `b983f7c3`.

## Required changes
- [x] Extend `Extrinsic.ECS.Hierarchy.Structure` with plain status/result structs and free functions `CollectChildren` and `CollectDescendantsPreorder`.
- [x] Preserve `FirstChild`/`NextSibling` order exactly. Descendant results exclude the queried root and use iterative depth-first preorder so deep valid scenes do not consume the C++ call stack.
- [x] Define explicit statuses for invalid root, dangling link, missing child hierarchy data, parent mismatch, sibling-backlink mismatch, child-count mismatch, duplicate/cycle, and traversal-limit exhaustion.
- [x] Treat a valid leaf/root without a hierarchy component as a successful empty result.
- [x] Return all-or-nothing entity lists: any corruption status clears collected output rather than publishing a trustworthy-looking prefix.
- [x] During each child-chain walk, require the head's `PrevSibling` to be invalid, every later child's `PrevSibling` to match the previously visited handle, and the enumerated count to equal `ChildCount` (`FirstChild` must be invalid iff the count is zero).
- [x] Bound work with the existing corruption-guard policy (`kMaxAncestryDepth` or a clearly named sibling constant), not scene policy.
- [x] Share the checked child-chain mechanics with `ValidateInvariants` so queries and invariant validation cannot drift into different definitions.
- [x] Keep `IsDescendant` as the narrow ancestry/mutation check.
- [x] Replace editor delete-plan descendant enumeration with the shared preorder result; a corrupt query must fail closed before recording or deleting any entity.
- [x] Replace progressive immediate-child composition enumeration with the shared child result and surface a diagnostic when hierarchy data is corrupt instead of presenting a partial summary.

## Tests
- [x] Extend `tests/unit/ecs/Test.ECS.Hierarchy.cpp` with exact child and descendant-preorder order, leaf/no-component success, and repeat determinism.
- [x] Cover invalid roots, dangling sibling/child links, missing child components, parent mismatches, broken head/middle `PrevSibling` backlinks, both `ChildCount`/`FirstChild` disagreement directions, duplicate links, cycles, and traversal-limit failure.
- [x] Assert every failure result contains no partial entities.
- [x] Add runtime contract coverage preserving valid delete-plan order and proving corrupt delete plans produce no partial command/mutation.
- [x] Add progressive model coverage for exact valid immediate-child counts and explicit corrupt-hierarchy diagnostics.

## Docs
- [x] Document query ordering, leaf semantics, statuses, traversal bound, and all-or-nothing contract in `src/ecs/README.md`.
- [x] Update runtime README descriptions only where the two adopters' failure semantics change.
- [x] Add this task to the ECS and Theme F backlog indexes.

## Acceptance criteria
- [x] Both runtime-local hierarchy walks are removed in favor of the promoted ECS helpers.
- [x] Valid ordering remains stable and corrupted structure never escapes as a partial successful result.
- [x] No new abstract traversal surface or forbidden ECS dependency is introduced.
- [x] ECS/runtime focused tests and strict layering checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ECSHierarchy|EditorCommandHistory|Progressive.*Composition' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Evidence
- 2026-07-18: `cmake --preset ci` configured with Clang 23.0.0.
- 2026-07-18: the five required focused targets built successfully; after the
  full selector identified registered-but-unbuilt executables,
  `cmake --build --preset ci --target IntrinsicTests` supplied the complete
  test registry without weakening the selector.
- 2026-07-18: focused CTest passed 28/28 hierarchy, editor-history, and
  progressive-composition cases.
- 2026-07-18: the exclusion-only full CPU selector passed 4,110/4,110 tests;
  the GLFW LSan capability contract reported its expected skip.
- 2026-07-18: strict layering (743 files, 6,442 references), test-layout,
  documentation-link (2,915 links), and task-policy (142 tasks) checks passed.
- 2026-07-18: module inventory regeneration reported 388 modules and produced
  no content diff.

## Forbidden changes
- Returning partial traversal results with a success-like status.
- Moving runtime composition or editor policy into ECS.
- Introducing recursion for unbounded descendant enumeration or a general iterator framework.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
