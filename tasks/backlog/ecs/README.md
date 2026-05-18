# ECS Backlog

Promoted ECS layer hardening: scene bootstrap, hierarchy/transform parity,
layer boundary cleanup, event/command seams, geometry-source authoring,
render-sync/export policy, bounds propagation, identity metadata, and authoring
contracts. `ecs -> core` only; geometry handles/types are allowed when
explicitly required, and graphics/runtime/platform/app imports are forbidden.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [HARDEN-060 — Promote ECS scene bootstrap contract](../../done/HARDEN-060-ecs-scene-bootstrap-contract.md) (done).
- [HARDEN-061 — Promote ECS hierarchy and transform system parity](../../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md) (done).
- [HARDEN-062 — Harden ECS layering and component boundaries](../../done/HARDEN-062-ecs-layering-and-component-boundary-hardening.md) (done).
- [HARDEN-063 — Define promoted ECS event and command seams](../../done/HARDEN-063-ecs-events-and-command-seams.md) (done).
- [HARDEN-064 — Define ECS collider and rigid-body authoring contracts](HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).
- [HARDEN-065 — Promote ECS geometry-source population and dirty-domain helpers](../../done/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md) (done).
- [HARDEN-066 — Define ECS render-sync/export dirty-tag policy](../../done/HARDEN-066-ecs-render-sync-export-policy.md) (done).
- [HARDEN-067 — Add ECS world-bounds propagation system](../../done/HARDEN-067-ecs-bounds-propagation-system.md) (done).
- [HARDEN-068 — Define ECS stable identity and scene metadata contract](../../done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md) (done).

## Convergence

- HARDEN-060..068 contribute to **Theme D — ECS hardening parity**.
- HARDEN-060..062 are required prerequisites for **Theme A — Shortest path to
  sandbox visible geometry** (renderable extraction needs promoted scene
  bootstrap and hierarchy/transform behavior).
- HARDEN-064 is in **Theme C — Physics readiness** and is gated by
  [`physics/ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md);
  it must not start before that decision lands.
- HARDEN-065 is a near-term follow-up from the
  [`src/ecs` gap analysis](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md)
  and must not move GPU residency or runtime extraction ownership into ECS.
  HARDEN-066 is the same family and landed as a CPU-only tag-forwarding pass
  (see [`tasks/done/HARDEN-066`](../../done/HARDEN-066-ecs-render-sync-export-policy.md)).
- HARDEN-067 depends on promoted transform hierarchy semantics from
  [`HARDEN-061`](../../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md)
  and pairs naturally with runtime fixed-step activation.
- HARDEN-068 should precede runtime scene-serialization implementation that
  needs stable ECS entity references.
- Forbidden across all members: physics-world handles, runtime sidecars,
  graphics handles, RHI handles, or live `AssetService` traffic in canonical
  ECS components.
