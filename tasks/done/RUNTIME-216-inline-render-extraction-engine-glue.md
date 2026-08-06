---
id: RUNTIME-216
theme: F
depends_on: [RUNTIME-215]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-runtime216"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T14:45:49Z"
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this follow-up changes only an Engine-private storage/forwarding shape and preserves public extraction, rendering, lifecycle, data-domain, and control contracts."
---
# RUNTIME-216 — Inline shallow render-extraction Engine glue

## Goal

- Delete the Engine-private `RenderExtractionService` forwarding class and let
  `Engine::Impl` own its extraction cache, render-world pool, and frame index
  directly without changing extraction or frame behavior.

## Non-goals

- No change to `Extrinsic.Runtime.RenderExtraction`, `RenderWorldPool`,
  renderer hooks, extraction algorithms, residency, frame order, or public
  Engine surface.
- No other runtime module/file consolidation; each additional candidate needs
  its own deletion test and task.
- No overlap with the mechanical source move in `RUNTIME-215`.

## Context

- Owner/layer: Engine-private `runtime` composition state.
- The current include-only declaration plus implementation total roughly 130
  lines. Every method delegates to one cache/pool member or increments the
  frame index, and Engine is the only production owner/caller.
- `RUNTIME-169` correctly retired the former public BMI but retained a private
  wrapper to preserve the then-current Engine declaration shape. `RUNTIME-187`
  subsequently moved all Engine state behind `Engine::Impl`, removing that
  declaration/import reason. The current deletion test therefore differs from
  the earlier decision: deleting the wrapper makes the forwarding layer vanish
  without recreating a public dependency or another caller-side abstraction.

## Right-sizing decision

- **Element:** `Rendering/Runtime.RenderExtractionService.Internal.hpp` and
  `Rendering/Runtime.RenderExtractionService.cpp`; pure-forwarding facade
  heuristic with one production owner.
- **Simpler alternative:** store `RenderExtractionCache`,
  `std::unique_ptr<RenderWorldPool>`, and the monotonic extraction frame index
  directly in `Engine::Impl`; keep tiny null/invalid-slot checks at their one
  Engine call site.
- **Blast radius:** Engine implementation, runtime CMake source list,
  source-contract tests, runtime README/architecture wording, and no public
  module inventory entry.
- **Reintroduction trigger:** a second production owner needs the complete
  cache/pool/frame-index lifetime as a reusable unit, or a genuine test-double
  seam must replace the current owner-level coverage.

## Required changes

- [x] Move the cache, pool, and frame-index values into `Engine::Impl` at the
      same relative lifetime position.
- [x] Replace forwarding calls with direct owner operations while preserving
      pool configuration, slot release, cache publication, scene clearing,
      maintenance, and cache-before-renderer shutdown order.
- [x] Delete both private service files and remove them from CMake/includes.
- [x] Update source-contract tests to assert the direct Engine-owned shape
      rather than a private forwarding class.

## Tests

- [x] Focused Engine-private glue, render extraction, render-world pool,
      lifecycle, and Sandbox acceptance tests pass.
- [x] The complete default CPU-supported selector and strict layering gate pass.

## Docs

- [x] Update runtime ownership prose to describe direct Engine-private cache,
      pool, and frame-index state.
- [x] Refresh task/session indexes and generated inventory if the generator
      changes.

## Acceptance criteria

- [x] The two `RenderExtractionService` files and class name are absent.
- [x] Runtime file count decreases by exactly two without a replacement file,
      wrapper, interface, module, or public API.
- [x] Engine teardown order and all extraction/pool behavior remain unchanged
      under focused and complete CPU verification.
- [x] No unrelated runtime consolidation is mixed into this task.

## Status

- Completed on 2026-08-06 as the semantic right-sizing follow-up to the
  independently reviewed `RUNTIME-215` path move.
- Implementation commit: `7e61e215`.
- The facade's 130 declaration/implementation lines are deleted; runtime file
  count moved from 170 to 168 with no replacement file or public surface.
- Focused runtime coverage passed 70/70 and the final default CPU selector
  passed 4,103/4,103 with only the expected environment-gated GLFW/LSan skip.
  Strict layering, task policy, docs sync/links, module-inventory equality, and
  clean-workshop automated rows pass. Manual clean-workshop rows 3–6 are
  `n/a`: no public API, renderer member/subsystem, frame-graph pass, recipe
  dependency, or dependency edge changed.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEnginePrivateGlue|RenderExtraction|RenderWorldPool|RuntimeSandboxAcceptance|RuntimeEngineLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes

- Reintroducing a standalone service BMI, PImpl, callback facade, or aggregate
  dependency record to hide the same three values.
- Changing extraction algorithms, renderer contracts, public Engine methods,
  frame phase order, or module imports.
- Consolidating any second file/module candidate in this slice.
