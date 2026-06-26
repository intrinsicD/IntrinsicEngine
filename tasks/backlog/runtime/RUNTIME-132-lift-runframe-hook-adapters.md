---
id: RUNTIME-132
theme: F
depends_on: []
---
# RUNTIME-132 — Lift single-use RunFrame hook adapters out of the RunFrame body

## Goal
- Make the main loop read clearly by lifting the inline single-use frame-hook
  adapter structs and the pick-context capture block out of `Engine::RunFrame`,
  leaving it a short ordered list of phase setup + `Execute*Contract` calls
  (P5). Strictly behavior-preserving.

## Non-goals
- Any behavior change whatsoever (this is a pure readability/locality refactor).
- Introducing a hook registry/factory or a generic stage framework (P1: adapters
  stay plain structs adapting runtime members to the existing `Core.FrameLoop`
  contract interfaces; the goal is line-count and locality, not new indirection).

## Context
- `Engine::RunFrame` (`src/runtime/Runtime.Engine.cpp`) is large (~680 lines)
  largely because six inline `I*FrameHooks` adapter structs and the `BUG-026`
  pick-context capture block (~lines 2276–2325) are defined inline. The frame
  shape itself is already an ordered, documented sequence (see the `Engine`
  header comment); the body just buries it under inline adapter definitions.
- Owner/layer: `runtime` (composition root). The frame contract free functions
  (`Core.FrameLoop` `Execute*Contract`) already exist and are not changed.

## Required changes
- [ ] Move the six inline adapter structs (e.g. `PlatformFrameHooks`,
      `OperationalTransitionHooks`, `RenderFrameHooks`, `TransferHooks`,
      `StreamingHooks`, `AssetHooks`) into named free helpers / small adapter
      units under `src/runtime`.
- [ ] Move the `BUG-026` pick-context capture into a named free helper.
- [ ] Leave `RunFrame` as a short ordered list of phase setup + `Execute*Contract`
      calls.

## Tests
- [ ] `Test.RuntimeFrameLoopContract` and `Test_HeadlessEngine` (and the runtime
      frame tests) stay green with zero behavioral change.
- [ ] Default CPU gate stays green.

## Docs
- [ ] No doc change required beyond a one-line note in `src/runtime/README.md` if
      it indexes `RunFrame` structure.

## Acceptance criteria
- [ ] `RunFrame` shrinks to a short, ordered, readable phase list (~150 lines).
- [ ] Zero behavioral change; existing runtime/headless tests unchanged and green.
- [ ] No hook registry / stage framework introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Any behavior change to the frame loop.
- Introducing a hook registry/factory or generic stage framework.
- Changing the `Core.FrameLoop` `Execute*Contract` signatures.
