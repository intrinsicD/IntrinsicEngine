---
id: BUG-264
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: source-backed ownership diagnosis; no memory measurement or operational claim
contract_schema: 1
contracts:
  - geometry.property-coherence
  - runtime.engine-compilation-locality
  - repo.source-documentation
---
# BUG-264 — Retire superseded accepted position fronts without rendering

## Operator direction (verbatim)
1. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
2. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete ARCH-024's no-render resource lifecycle for already materialized point clouds and graphs.
Keep the latest accepted front available for its eventual render copy while releasing superseded
fronts after their actual GPU use ends.

## Source-backed diagnosis
`CommitAcceptedPositions` can call `GpuWorld::CommitGeometryPositions` without rendering.
Successive accepted fronts move the previous pending `SourceLease` into `RetiringSourceLeases`.
`BindRevision` retires the corresponding old canonical residency slot, but its shared lease
prevents reuse. `ReleaseRetiredSourceLeases` currently runs only through `GpuWorld::SyncFrame`
and uses frame age. Neither path progresses while minimized. Source inspection therefore
indicates retained resident bytes can accumulate across repeated Accept operations; this has
not yet been measured or reproduced in an execution test.

The latest pending front must remain leased. Ordinary live previews do not carry this accepted
front lease, so this diagnosis does not establish exhaustion of a single run's preview ring.

## Proposed bounded correction
Reuse `GpuWorld` as the owner: expose completion maintenance for its retired source leases and
call it from Engine's existing non-render completion helper through the renderer. Do not run
`SyncFrame`, geometry residency ticks, or synthetic frame advancement. Preserve the actual
recorded-copy frame; a newer unsubmitted stamp must not hide an older frame still reading it.

## Acceptance criteria
- [ ] Operator authorizes this additional ownership-layer correction under the drift rule.
- [ ] A regression at a frozen frame counter shows bounded resident bytes across repeated commits.
- [ ] Completion maintenance releases superseded leases while retaining the latest pending front.
- [ ] A held older copy frame prevents release despite a newer unsubmitted stamp.
- [ ] Independent review and focused CPU/GPU verification accept the combined owner wiring.

## Verification
Extend the existing `Test.GpuWorldPositionPreviewContract.cpp` fixture using real residency leases
and explicit device-retirement answers. Map it through the generated test inventory, build its
owning target, and demonstrate the regression before the correction. Verify the existing minimized
method tests after wiring. No native-window or performance claim follows from these checks.

## State
Independent source review confirmed the ownership chain. The operator's scope question is pending;
no correction has been applied. ARCH-024's other implementation and verification continue.
