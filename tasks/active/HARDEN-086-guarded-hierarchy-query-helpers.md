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

## Status
- In progress; owner: Codex team; branch:
  `codex/harden-086-guarded-hierarchy-queries`; activated 2026-07-18.
- Next gate: inventory the shared child-chain invariants, implement the
  all-or-nothing ECS query contract, and pass focused ECS/runtime coverage.

## Required changes
- [ ] Extend `Extrinsic.ECS.Hierarchy.Structure` with plain status/result structs and free functions `CollectChildren` and `CollectDescendantsPreorder`.
- [ ] Preserve `FirstChild`/`NextSibling` order exactly. Descendant results exclude the queried root and use iterative depth-first preorder so deep valid scenes do not consume the C++ call stack.
- [ ] Define explicit statuses for invalid root, dangling link, missing child hierarchy data, parent mismatch, sibling-backlink mismatch, child-count mismatch, duplicate/cycle, and traversal-limit exhaustion.
- [ ] Treat a valid leaf/root without a hierarchy component as a successful empty result.
- [ ] Return all-or-nothing entity lists: any corruption status clears collected output rather than publishing a trustworthy-looking prefix.
- [ ] During each child-chain walk, require the head's `PrevSibling` to be invalid, every later child's `PrevSibling` to match the previously visited handle, and the enumerated count to equal `ChildCount` (`FirstChild` must be invalid iff the count is zero).
- [ ] Bound work with the existing corruption-guard policy (`kMaxAncestryDepth` or a clearly named sibling constant), not scene policy.
- [ ] Share the checked child-chain mechanics with `ValidateInvariants` so queries and invariant validation cannot drift into different definitions.
- [ ] Keep `IsDescendant` as the narrow ancestry/mutation check.
- [ ] Replace editor delete-plan descendant enumeration with the shared preorder result; a corrupt query must fail closed before recording or deleting any entity.
- [ ] Replace progressive immediate-child composition enumeration with the shared child result and surface a diagnostic when hierarchy data is corrupt instead of presenting a partial summary.

## Tests
- [ ] Extend `tests/unit/ecs/Test.ECS.Hierarchy.cpp` with exact child and descendant-preorder order, leaf/no-component success, and repeat determinism.
- [ ] Cover invalid roots, dangling sibling/child links, missing child components, parent mismatches, broken head/middle `PrevSibling` backlinks, both `ChildCount`/`FirstChild` disagreement directions, duplicate links, cycles, and traversal-limit failure.
- [ ] Assert every failure result contains no partial entities.
- [ ] Add runtime contract coverage preserving valid delete-plan order and proving corrupt delete plans produce no partial command/mutation.
- [ ] Add progressive model coverage for exact valid immediate-child counts and explicit corrupt-hierarchy diagnostics.

## Docs
- [ ] Document query ordering, leaf semantics, statuses, traversal bound, and all-or-nothing contract in `src/ecs/README.md`.
- [ ] Update runtime README descriptions only where the two adopters' failure semantics change.
- [ ] Add this task to the ECS and Theme F backlog indexes.

## Acceptance criteria
- [ ] Both runtime-local hierarchy walks are removed in favor of the promoted ECS helpers.
- [ ] Valid ordering remains stable and corrupted structure never escapes as a partial successful result.
- [ ] No new abstract traversal surface or forbidden ECS dependency is introduced.
- [ ] ECS/runtime focused tests and strict layering checks pass.

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

## Forbidden changes
- Returning partial traversal results with a success-like status.
- Moving runtime composition or editor policy into ECS.
- Introducing recursion for unbounded descendant enumeration or a general iterator framework.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
