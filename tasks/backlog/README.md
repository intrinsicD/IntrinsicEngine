# Backlog Tasks

Backlog tasks are approved or proposed work items that have not started yet.

## Categories

- [`architecture/`](architecture/) — architecture and layering decisions.
- [`assets/`](assets/) — promoted CPU asset authority and import/export ingest.
- [`bugs/`](bugs/) — reproducible correctness bugs and harness defects.
- [`ecs/`](ecs/) — promoted ECS scene/components/systems hardening.
- [`geometry/`](geometry/) — geometry algorithms, IO, and method readiness.
- [`methods/`](methods/) — paper/method packages following the method workflow.
- [`physics/`](physics/) — physics layer ownership and phenomena roadmap.
- [`rendering/`](rendering/) — renderer, frame graph, and RHI work.
- [`runtime/`](runtime/) — runtime composition root and lifecycle.
- [`ui/`](ui/) — editor/UI integration seams.

## Convergence themes

Use this section when picking the next active task. Each theme groups backlog
work that converges on one engine outcome; "cross-domain dependency anchors"
record the edges agents must respect when selecting work, so per-category DAGs
stay globally aligned.

The agent contract in [`/AGENTS.md`](../../AGENTS.md) is the authoritative
source for the engine mission and layering invariants. Themes below describe
how the *current* backlog maps onto that contract.

### Theme A — Shortest path to sandbox visible geometry (P0)

Render real geometry from `Sandbox::App` through the promoted runtime/graphics
path. Origin: [sandbox geometry rendering gap analysis (2026-05-08)](../../docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md).

Members:
- [`rendering/GRAPHICS-029-runtime-reference-scene-bootstrap.md`](rendering/GRAPHICS-029-runtime-reference-scene-bootstrap.md)
- [`rendering/GRAPHICS-030-runtime-geometry-residency-bridge.md`](rendering/GRAPHICS-030-runtime-geometry-residency-bridge.md)
- [`rendering/GRAPHICS-031-default-debug-surface-material.md`](rendering/GRAPHICS-031-default-debug-surface-material.md)
- [`rendering/GRAPHICS-032-minimal-surface-present-command-path.md`](rendering/GRAPHICS-032-minimal-surface-present-command-path.md)
- [`rendering/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](rendering/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md)
- [`rendering/GRAPHICS-034-asset-backed-mesh-residency-bridge.md`](rendering/GRAPHICS-034-asset-backed-mesh-residency-bridge.md)
- [`runtime/RORG-031-runtime-composition.md`](runtime/RORG-031-runtime-composition.md)
- [`assets/ASSETIO-001-asset-model-texture-ingest-ownership.md`](assets/ASSETIO-001-asset-model-texture-ingest-ownership.md)
- [`geometry/GEOIO-002-geometry-io-parity-hardening.md`](geometry/GEOIO-002-geometry-io-parity-hardening.md)
- [`ecs/HARDEN-060` (active)](../active/HARDEN-060-ecs-scene-bootstrap-contract.md)
- [`ecs/HARDEN-061-ecs-hierarchy-transform-system-parity.md`](ecs/HARDEN-061-ecs-hierarchy-transform-system-parity.md)
- [`ecs/HARDEN-062-ecs-layering-and-component-boundary-hardening.md`](ecs/HARDEN-062-ecs-layering-and-component-boundary-hardening.md)

### Theme B — Rendering modernization (P1, gated by Theme A)

Promote the post-reorganization renderer toward 2026-era features without
breaking the foundation. All leaves stay planning-only until Theme A is
unblocked.

Members:
- [`rendering/GRAPHICS-035-modernization-roadmap.md`](rendering/GRAPHICS-035-modernization-roadmap.md) (umbrella).
- `rendering/GRAPHICS-036..058` planning-only leaves: pipelined frames,
  async compute and multi-queue scheduling, HZB occlusion culling, clustered
  light binning, TAA and reconstructor seam, Slang shader pipeline, PBR
  completeness and IBL, visibility buffer, meshlets, ray tracing RHI extension,
  hybrid GI (ReSTIR/DDGI), virtual shadow maps, Gaussian splatting rasterizer,
  neural radiance cache, neural texture compression, differentiable rendering
  mode, deltaful GPU-resident scene, mesh shaders, work graphs, streaming
  virtual textures, virtualized meshes with cluster LOD, GPU decompression,
  frame generation. See [`rendering/README.md`](rendering/README.md) for the
  full list and DAG.

### Theme C — Physics readiness (P1, gated by ARCH-001)

Define physics layer ownership before any solver code lands; then implement the
rigid-body reference backend behind that contract.

Members:
- [`physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md`](physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md) (gating decision).
- [`physics/ARCH-002-physics-phenomena-roadmap.md`](physics/ARCH-002-physics-phenomena-roadmap.md).
- [`methods/METHOD-001-rigid-body-dynamics-reference-backend.md`](methods/METHOD-001-rigid-body-dynamics-reference-backend.md).
- [`ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md`](ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) (also Theme D).

### Theme D — ECS hardening parity (P0)

Promote ECS scene/hierarchy/component contracts out of `src/legacy` while
keeping `ecs -> core` and explicit geometry handles only.

Members:
- [`ecs/HARDEN-060` (active)](../active/HARDEN-060-ecs-scene-bootstrap-contract.md).
- [`ecs/HARDEN-061-ecs-hierarchy-transform-system-parity.md`](ecs/HARDEN-061-ecs-hierarchy-transform-system-parity.md).
- [`ecs/HARDEN-062-ecs-layering-and-component-boundary-hardening.md`](ecs/HARDEN-062-ecs-layering-and-component-boundary-hardening.md).
- [`ecs/HARDEN-063-ecs-events-and-command-seams.md`](ecs/HARDEN-063-ecs-events-and-command-seams.md).
- [`ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md`](ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) (also Theme C).

### Theme E — Geometry IO completion (P0)

Finish geometry-owned IO parity so legacy graphics importers/exporters can
retire and asset ingest can route through promoted decoders.

Members:
- [`geometry/GEOIO-002-geometry-io-parity-hardening.md`](geometry/GEOIO-002-geometry-io-parity-hardening.md).

### Theme F — Architecture/runtime/UI foundation seeds

Keep cross-cutting backlog stubs honest with current state and reachable from
the convergence map.

Members:
- [`architecture/RORG-031A-architecture-foundation.md`](architecture/RORG-031A-architecture-foundation.md).
- [`runtime/RORG-031-runtime-composition.md`](runtime/RORG-031-runtime-composition.md) (also Theme A).
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`ui/RORG-031-ui-integration.md`](ui/RORG-031-ui-integration.md).

### Theme G — Active bugs

Reproducible correctness/regression fixes only. Origin:
[`bugs/index.md`](bugs/index.md).

Members:
- Currently no active reproducible issues are tracked.

## Cross-domain dependency anchors

These edges constrain task selection across categories. Respect them when
promoting backlog tasks to active so per-category DAGs do not diverge.

- **GRAPHICS-034 ⇐ ASSETIO-001 ⇐ GEOIO-002.** Asset-backed mesh residency
  depends on promoted asset routing, which depends on geometry decoder parity.
- **GRAPHICS-029..034 ⇐ HARDEN-060..062.** Sandbox renderable extraction needs
  promoted ECS scene/hierarchy/transform parity.
- **METHOD-001 ⇐ ARCH-001.** Rigid-body reference must wait for the physics
  layer ownership decision before runtime/ECS integration.
- **HARDEN-064 ⇐ ARCH-001.** ECS collider/rigid-body authoring contract must
  wait for the physics layer ownership decision.
- **GRAPHICS-035..058 ⇐ Theme A.** Rendering modernization leaves stay
  planning-only until the visible-geometry foundation is complete.

## Promotion checklist

Before promoting a backlog task to active:

1. Confirm the task scope is small and reviewable.
2. Confirm acceptance criteria and verification commands exist.
3. Confirm required docs updates are listed.
4. Confirm the cross-domain dependency anchors above are satisfied or are
   explicitly recorded as out-of-scope in the task file.

## Related

- [`/AGENTS.md`](../../AGENTS.md) — authoritative repository contract.
- [`tasks/README.md`](../README.md) — task lifecycle and ID prefix conventions.
- [`docs/agent/contract.md`](../../docs/agent/contract.md) — expanded contract.
- [`docs/agent/task-format.md`](../../docs/agent/task-format.md) — task file structure.
- [`docs/agent/review-checklist.md`](../../docs/agent/review-checklist.md) — pre-commit/PR review checklist.
- [`tasks/backlog/rendering/README.md`](rendering/README.md) — rendering DAG (Themes A and B detail).
- [`tasks/backlog/runtime/README.md`](runtime/README.md) — runtime backlog index (Themes A and F detail).
