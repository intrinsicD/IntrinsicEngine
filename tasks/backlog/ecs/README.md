# ECS Backlog

Promoted ECS layer hardening: scene bootstrap, hierarchy/transform parity,
layer boundary cleanup, event/command seams, geometry-source authoring,
render-sync/export policy, bounds propagation, identity metadata, and authoring
contracts. `ecs -> core` only; geometry handles/types are allowed when
explicitly required, and graphics/runtime/platform/app imports are forbidden.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

No open ECS backlog tasks are currently queued here.

## Convergence

- HARDEN-060..068 contribute to **Theme D — ECS hardening parity**.
- HARDEN-060..062 are required prerequisites for **Theme A — Shortest path to
  sandbox visible geometry** (renderable extraction needs promoted scene
  bootstrap and hierarchy/transform behavior).
- HARDEN-064 is in **Theme C — Physics readiness** and is retired to
  `tasks/done`.
  The upstream
  `ARCH-001`
  gate is accepted via
  [ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md),
  and the ECS authoring contract shipped without storing physics-world handles
  in canonical ECS components.
- HARDEN-065 is a near-term follow-up from the
  [`src/ecs` gap analysis](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md)
  and must not move GPU residency or runtime extraction ownership into ECS.
  HARDEN-066 is the same family and landed as a CPU-only tag-forwarding pass
  (see `tasks/done/HARDEN-066`).
- HARDEN-067 depends on promoted transform hierarchy semantics from
  `HARDEN-061`
  and pairs naturally with runtime fixed-step activation.
- HARDEN-068 should precede runtime scene-serialization implementation that
  needs stable ECS entity references.
- HARDEN-080 is a module-interface hygiene follow-up for promoted ECS `.cppm`
  targets found by the 2026-06-06 implementation-body audit. It must preserve
  the existing ECS contracts from HARDEN-060..068 and move only non-trivial
  non-template bodies plus implementation-only includes/imports.
- HARDEN-081 retired the final named ECS compatibility decision gate from
  `LEGACY-011` while keeping `ecs -> core` only.
- HARDEN-083 retired the promoted `GeometrySources` CPU source-availability
  contract: it separates underlying provenance from available vertex/node,
  edge, halfedge, and face source data so runtime/UI consumers do not treat
  exact `ActiveDomain` as the only capability query.
- Forbidden across all members: physics-world handles, runtime sidecars,
  graphics handles, RHI handles, or live `AssetService` traffic in canonical
  ECS components.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [HARDEN-060 — Promote ECS scene bootstrap contract](../../done/HARDEN-060-ecs-scene-bootstrap-contract.md) (done).
- [HARDEN-061 — Promote ECS hierarchy and transform system parity](../../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md) (done).
- [HARDEN-062 — Harden ECS layering and component boundaries](../../done/HARDEN-062-ecs-layering-and-component-boundary-hardening.md) (done).
- [HARDEN-063 — Define promoted ECS event and command seams](../../done/HARDEN-063-ecs-events-and-command-seams.md) (done).
- [HARDEN-064 — Define ECS collider and rigid-body authoring contracts](../../done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) (done).
- [HARDEN-065 — Promote ECS geometry-source population and dirty-domain helpers](../../done/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md) (done).
- [HARDEN-066 — Define ECS render-sync/export dirty-tag policy](../../done/HARDEN-066-ecs-render-sync-export-policy.md) (done).
- [HARDEN-067 — Add ECS world-bounds propagation system](../../done/HARDEN-067-ecs-bounds-propagation-system.md) (done).
- [HARDEN-068 — Define ECS stable identity and scene metadata contract](../../done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md) (done).
- [HARDEN-080 — ECS module implementation splits](../../done/HARDEN-080-ecs-module-implementation-splits.md).
- [HARDEN-081 — ECS legacy component compatibility decisions](../../done/HARDEN-081-ecs-legacy-component-compatibility-decisions.md) (done):
  retired remaining legacy `NameTag`, `AxisRotator`, DEC wrapper, and
  feature-token compatibility gaps without moving runtime/graphics ownership
  into ECS.
- [HARDEN-083 — Geometry source availability and provenance contract](../../done/HARDEN-083-geometry-source-availability-contract.md)
  (done, 2026-06-19, `CPUContracted`): `GeometrySources` now reports CPU
  source capabilities separately from exact active domain and provenance.
