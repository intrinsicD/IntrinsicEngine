---
id: BUG-103
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-103 — Render-graph lifetime test culls its history chain

## Status

- Implementation complete on 2026-07-16 at `CPUContracted`; uncommitted for
  parent-agent integration.
- The pre-fix test failed identically in `build/ci` and `build/ci-vulkan`.
  Ccache-disabled rebuilds confirmed current-source behavior. After the
  test-only correction, the exact regression passed 20 consecutive runs in
  each tree and both 57-test `GraphicsRenderGraph` suites passed.

## Goal

- Restore the deterministic render-graph lifetime regression by connecting its
  declared history uses to the live, ordered execution that the lifetime
  contract measures.

## Non-goals

- No change to render-graph pass-culling policy or transient alias placement.
- No return to declaration-order lifetimes for culled passes.
- No Vulkan/RHI behavior change; the failing test exercises the CPU compiler.

## Context

- Owner: `graphics/framegraph` CPU compiler contract and its integration test.
- `GraphicsRenderGraph.LifetimeFirstAndLastUseTracksPassIndices` fails
  deterministically in both `build/ci` and `build/ci-vulkan`: the disconnected
  `HistoryWrite`/`HistoryRead` chain is culled, while the `Present` root is live
  at execution rank zero.
- Commit `672bab38` changed resource lifetimes to live topological execution
  ranks for transient placement. The older test fixture was not updated to
  make the history chain live; current architecture and framegraph docs require
  execution-rank lifetimes and exclude passes absent from `TopologicalOrder`.
- The framegraph compiler and failing test blobs are byte-identical to
  `origin/main`, establishing this as a pre-existing source inconsistency rather
  than a regression in the current branch.

## Required changes

- [x] Connect the present root to the history read through the existing
      `RenderGraphBuilder::DependsOn()` contract so the chain is both live and
      ordered before present.
- [x] Keep the lifetime assertions in execution-rank space and pin the expected
      topological order so a future fixture regression is immediately legible.
- [x] Do not change compiler behavior because ccache-disabled rebuild evidence
      confirms the stale-test diagnosis.

## Tests

- [x] Rebuild `IntrinsicRuntimeIntegrationTests` with ccache disabled and
      reproduce the pre-fix failure to rule out stale module artifacts.
- [x] Pass the exact focused test repeatedly in both `build/ci` and
      `build/ci-vulkan` after the fix.
- [x] Pass the focused `GraphicsRenderGraph` integration suite used by the
      touched executable.

## Docs

- [x] Record the source inconsistency, ccache-disabled rebuild evidence, root
      cause, and verification in this task; no architecture-doc edit is
      required because the documented live execution-rank contract remains
      unchanged.

## Acceptance criteria

- [x] The history write/read and present uses are all live in deterministic
      topological order `{0, 1, 2}`.
- [x] History lifetime is `[0, 1]` and backbuffer lifetime is `[2, 2]` in
      execution-rank space.
- [x] Focused verification passes in both configured Clang 20+ preset trees
      without changing production framegraph code.

## Verification

```bash
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure \
  -R '^GraphicsRenderGraph\\.LifetimeFirstAndLastUseTracksPassIndices$' \
  --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^GraphicsRenderGraph\\.' --timeout 60
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^GraphicsRenderGraph\\.LifetimeFirstAndLastUseTracksPassIndices$' \
  --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^GraphicsRenderGraph\\.' --timeout 60
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
git diff --check
```

Verification completed on 2026-07-16. The exact regression passed 20/20 in
each preset tree; `GraphicsRenderGraph.*` passed 57/57 in each tree. Strict task
validation and policy checks passed with zero findings, and `git diff --check`
passed. No production source or architecture documentation changed.

## Forbidden changes

- Treating culled declarations as executed lifetime uses.
- Weakening or removing the lifetime assertions.
- Editing task indexes or `tasks/SESSION-BRIEF.md` in this delegated slice.
- Introducing unrelated graphics, runtime, geometry, or asset changes.

## Maturity

- Achieved: `CPUContracted`; the render-graph compiler is backend-neutral and no
  `Operational` follow-up is owed.
