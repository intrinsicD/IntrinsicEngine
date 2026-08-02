---
id: RUNTIME-189
theme: I
depends_on: []
maturity_target: Retired
---
# RUNTIME-189 — Withdraw the premature orientation debug view

## Status

- Withdrawn on 2026-08-01 at `Retired` as a pre-implementation planning
  record; commit reference: this RUNTIME-138 backlog-coherence change.
- No debug snapshot, config section, panel, renderer path, or capability was
  implemented. `Retired` describes the obsolete plan, not an engine feature.
- METHOD-032 has a private killing gate and explicitly owes no runtime/UI
  follow-up. Its public result and diagnostic payload exist only after a
  positive verdict, so this task could not truthfully freeze lifetime,
  primitive, sign-field, config, or Vulkan acceptance contracts in advance.

## Goal

- Remove the speculative Sandbox orientation-diagnostics view from the open
  queue until both the method and a concrete debugging workflow justify a
  bounded presentation task.

## Non-goals

- No implementation or change to METHOD-032, debug rendering, config, UI,
  runtime, or graphics code.
- No promise that a future orientation view must exist or must use this task's
  former glyph/marker/slice design.
- No inference about METHOD-032's eventual positive or negative result.

## Context

- METHOD-032 first freezes and executes a held-out killing protocol. A failed
  result retires without a public method surface or implementation follow-up.
- Even on a positive result, the method task targets `CPUContracted` and says
  runtime/UI integration requires a separately reviewed task based on then
  current public outputs and demand.
- Pre-seeding a production snapshot and Vulkan view assumed both a positive
  result and fields such as a corner sign volume whose final public lifetime
  and payload were intentionally not frozen.
- If a positive method result later leaves a recurring diagnosis that textual
  metrics and method artifacts cannot answer, open a new task after a fresh
  consumer census. That task must name the exact copied payload, owner,
  primitive budget, config needs, and operational proof.

## Required changes

- [x] Reconcile the proposed view against METHOD-032's negative/positive
      branches and no-follow-up contract.
- [x] Remove RUNTIME-189 from the open runtime/method dependency narrative.
- [x] Preserve the idea and its reintroduction trigger in this historical
      record without reserving a production API.

## Tests

- [x] Confirm no production or test source imports a RUNTIME-189-specific
      result, snapshot, config, or debug-draw symbol.
- [x] Run strict task and documentation-link validation for the tracker-only
      withdrawal.

## Docs

- [x] Update METHOD-032 and the runtime/method/cross-domain indexes to say a
      view decision follows only positive evidence plus concrete demand.
- [x] Append the retirement narrative and regenerate the session brief.

## Acceptance criteria

- [x] No open runtime task promises presentation for an unproven method or an
      unfrozen payload.
- [x] METHOD-032 remains independently closable on either its positive or
      negative path.
- [x] A future view requires a new task based on the actual public method
      contract and a named debugging consumer.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes

- Adding speculative method fields or runtime ownership to preserve the old
  view design.
- Treating a positive method result alone as proof that a Sandbox view is
  needed.
- Reusing RUNTIME-189 for a future implementation after this record retires.

## Maturity

- `Retired` applies only to the obsolete planning surface. No capability
  maturity or `Operational` follow-up is claimed or owed.
