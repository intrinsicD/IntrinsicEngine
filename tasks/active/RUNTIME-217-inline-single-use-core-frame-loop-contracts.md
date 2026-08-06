---
id: RUNTIME-217
theme: F
depends_on:
  - RUNTIME-203
  - RUNTIME-216
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-runtime217"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T17:45:03Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# RUNTIME-217 — Inline single-use Core frame-loop contracts

## Status

- In progress on `main`: preserve owner-level lifecycle assertions first, then
  delete the Core hook family and inline its fixed ordering in Runtime Engine.

## Goal

- Delete the six single-use `Core.FrameLoop` hook interfaces and their Runtime
  adapter structs by expressing the same ordered phases directly in Runtime's
  private Engine frame-loop implementation.

## Non-goals

- No frame order, minimized/close behavior, fixed-step scheduling, renderer,
  transfer, asset, operational-transition, shutdown, or diagnostics change.
- No change to `Runtime.Module` frame/viewport hook registration or to the
  concrete `AssetWorkflowModule` service's lifecycle and optionality.
- No new public Runtime frame-loop module, service, interface, callback bundle,
  context aggregate, or Engine accessor.

## Context

- `Core.FrameLoop` exports `IRenderFrameHooks`, `IPlatformFrameHooks`,
  `ITransferFrameHooks`, `IAssetFrameHooks`, `IOperationalTransitionHooks`, and
  `IShutdownHooks` plus five straight-line ordering functions and three
  contract-only result/phase records.
- Runtime Engine is the only production caller. Five interfaces have one
  Runtime adapter; the asset interface has the AssetWorkflow provider plus an
  Engine aggregate adapter. The family exists to invoke one fixed composition
  order, not to support alternate production orchestrators.
- Tests currently construct interface doubles and thereby preserve the wrapper
  surface instead of exercising only the real Engine lifecycle. Existing
  owner-level Runtime contracts already cover the same order and failure paths.
- Runtime is the layer that owns cross-subsystem composition; Core should not
  retain a runtime-specific hook vocabulary solely to make that one owner
  indirect.

## Right-sizing decision

- **Element:** the complete `Extrinsic.Core.FrameLoop` interface/function
  family and Runtime adapter structs.
- **Deletion test:** inline the five short ordered contracts into named private
  Engine phase functions/call sites, retain concrete asset-hook lookup or
  feature-owned calls, and delete direct interface-double tests. Behavior stays
  with its current Runtime owner rather than being redistributed.
- **Simpler alternative:** readable ordered private Runtime phases using direct
  subsystem calls.
- **Reintroduction trigger:** a present second production composition root or
  test-double boundary must need the same complete lifecycle contract.

## Required changes

- [ ] Ratchet all existing platform, render, maintenance, operational
      transition, and shutdown order/failure behavior through public Engine or
      owner-level tests before deleting wrappers.
- [ ] Replace `Execute*Contract(...)` calls with the same ordered direct Engine
      operations, keeping named phase readability.
- [ ] Remove Engine adapter structs and eliminate the `Core.FrameLoop` module,
      implementation, `RenderFramePhase`, `RenderFrameResult`,
      `PlatformFrameResult`, CMake entry, imports, and module inventory row.
- [ ] Rename the existing private `TickAssets()` operation to a narrow public
      concrete `AssetWorkflowModule::RunFrameMaintenance()` method, remove the
      redundant `IAssetFrameHooks` service publication/lookup, and call the
      already-published concrete service between transfer collection and
      geometry-residency maintenance; null-service omission must remain valid.
- [ ] Delete direct wrapper tests only after equivalent owner-level assertions
      cover every branch.
- [ ] Add structural ratchets against recreating the six names or a replacement
      hook aggregate.

## Tests

- [ ] Focused Engine frame-loop, lifecycle, asset workflow, operational
      transition, renderer, transfer, minimized/close, and shutdown contracts
      pass.
- [ ] Complete default CPU-supported gate passes.
- [ ] Strict layering, docs/task, clean-workshop, and generated-inventory gates
      pass.

## Docs

- [ ] Update Core/runtime architecture and parity inventories to put ordered
      composition solely in Runtime and describe the surviving concrete
      AssetWorkflow maintenance boundary.
- [ ] Regenerate module inventory and task/session/retirement records.

## Acceptance criteria

- [ ] `Extrinsic.Core.FrameLoop`, all six hook interface names, its three
      result/phase records, and all Runtime adapter structs are absent.
- [ ] Engine behavior and ordering remain identical under owner-level tests.
- [ ] Runtime file count does not increase and no replacement public or private
      aggregate wrapper appears.
- [ ] Optional AssetWorkflow omission and lifecycle remain operational.
- [ ] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngine|RuntimeFrameLoop|AssetWorkflow|OperationalTransition|RenderExtraction|Transfer|Shutdown|SandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Reordering any lifecycle phase or weakening a fail-closed branch.
- Moving composition back into Core or into app/lower layers.
- Adding a new Runtime frame-loop abstraction with the same single owner.
- Folding unrelated Engine/module cleanup into this deletion.

## Maturity

- Target: `Retired`; the single-use Core contract family disappears while the
  Runtime-owned lifecycle remains `Operational`/CPU-contracted at its current
  capability level.
