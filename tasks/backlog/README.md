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
- [`platform/`](platform/) — windowing/input port and explicit platform backends.
- [`rendering/`](rendering/) — renderer, frame graph, and RHI work.
- [`runtime/`](runtime/) — runtime composition root and lifecycle.
- [`ui/`](ui/) — editor/UI integration seams.
- [`workshop/`](workshop/) — clean-workshop task pack: guardrails, boundary
  fixes, typed routing, renderer decomposition, maturity taxonomy, and
  architecture review gate.

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
Implementation-children gap follow-up: `tasks/backlog/rendering/GRAPHICS-029A..033D` and `runtime/RUNTIME-070` (`BUILD-001` is done).

Planning parents (retired):
- [`rendering/GRAPHICS-029` (done)](../done/GRAPHICS-029-runtime-reference-scene-bootstrap.md)
- [`rendering/GRAPHICS-030` (done)](../done/GRAPHICS-030-runtime-geometry-residency-bridge.md)
- [`rendering/GRAPHICS-031` (done)](../done/GRAPHICS-031-default-debug-surface-material.md)
- [`rendering/GRAPHICS-032` (done)](../done/GRAPHICS-032-minimal-surface-present-command-path.md)
- [`rendering/GRAPHICS-033` (done)](../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md)
- [`rendering/GRAPHICS-033D-vulkan-visible-triangle-smoke.md`](rendering/GRAPHICS-033D-vulkan-visible-triangle-smoke.md)
- [`rendering/GRAPHICS-034-asset-backed-mesh-residency-bridge.md`](rendering/GRAPHICS-034-asset-backed-mesh-residency-bridge.md)
- [`runtime/RORG-031-runtime-composition.md`](runtime/RORG-031-runtime-composition.md)
- [`assets/ASSETIO-001-asset-model-texture-ingest-ownership.md`](assets/ASSETIO-001-asset-model-texture-ingest-ownership.md)
- [`geometry/GEOIO-002-geometry-io-parity-hardening.md`](geometry/GEOIO-002-geometry-io-parity-hardening.md)
- [`ecs/HARDEN-060` (done)](../done/HARDEN-060-ecs-scene-bootstrap-contract.md)
- [`ecs/HARDEN-061` (done)](../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md)
- [`ecs/HARDEN-062` (done)](../done/HARDEN-062-ecs-layering-and-component-boundary-hardening.md)
- [`done/RUNTIME-091`](../done/RUNTIME-091-promoted-ecs-system-bundle-activation.md) (done)

Triangle-path implementation children (newly opened — pick the next in dependency order):
- [`done/GRAPHICS-030A`](../done/GRAPHICS-030A-procedural-geometry-descriptor-cache.md) — procedural geometry descriptor + cache + triangle packer (done; unblocks GRAPHICS-029B and GRAPHICS-030B).
- [`done/BUILD-001`](../done/BUILD-001-sandbox-shader-compile-wiring.md) — Sandbox shader compile wiring (done).
- [`runtime/RUNTIME-070` (done)](../done/RUNTIME-070-fallback-texture-bootstrap.md) — `GpuAssetCache` fallback texture bootstrap.
- [`done/GRAPHICS-029A`](../done/GRAPHICS-029A-reference-scene-skeleton.md) (done; unblocks GRAPHICS-029B), [`done/GRAPHICS-029B`](../done/GRAPHICS-029B-triangle-provider-and-camera.md) (done) — reference scene + camera substitution.
- [`done/GRAPHICS-030B`](../done/GRAPHICS-030B-extraction-procedural-geometry-binding.md) (done), [`done/GRAPHICS-030C`](../done/GRAPHICS-030C-procedural-geometry-retire-ordering.md) (done) — extraction wiring + retire ordering.
- [`done/GRAPHICS-031A`](../done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md) (done), [`done/GRAPHICS-031B`](../done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md) (done) — default debug surface shaders/pipeline + substitution.
- [`done/GRAPHICS-032A`](../done/GRAPHICS-032A-minimal-debug-surface-recipe.md) (done), [`done/GRAPHICS-032B`](../done/GRAPHICS-032B-minimal-debug-surface-pass-body.md) (done), [`done/GRAPHICS-032C`](../done/GRAPHICS-032C-minimal-debug-present-pass-and-acceptance.md) (done), [`done/GRAPHICS-032D`](../done/GRAPHICS-032D-gpu-vulkan-minimal-recipe-smoke.md) (done) — minimal debug recipe + pass bodies + smoke.
- [`done/GRAPHICS-033A`](../done/GRAPHICS-033A-vulkan-operational-status-evaluator.md) (done), [`done/GRAPHICS-033B`](../done/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md) (done), [`done/GRAPHICS-033C`](../done/GRAPHICS-033C-vulkan-minimal-recipe-recording.md) (done), [`done/GRAPHICS-033D`](../done/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md) (done), [`done/GRAPHICS-033E`](../done/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md) (done), [`done/GRAPHICS-033F`](../done/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md) (done) — Vulkan operational gate + diagnostics + recording bodies + visible-triangle smoke + the two gate-input slices (`BarrierValidationClean`, `PublicServiceReconciled`) that let `IsOperational()` flip to `true`.
- [`done/GRAPHICS-080`](../done/GRAPHICS-080-enable-promoted-vulkan-by-default.md) — flip reference config + add `ci-vulkan` preset (done after full default and promoted Vulkan gate verification on 2026-05-18).

Beyond-triangle full-graphics implementation tasks (Theme B′ in the rendering DAG; gated by Theme A's triangle landing):
- Default-recipe pass operational wiring: [`GRAPHICS-070..076`](rendering/) — forward surface, line/point, deferred GBuffer + lighting, shadows, selection/outline + picking readback, postprocess chain, debug view + canonical present.
- Backend transient/visualization upload helpers: [`GRAPHICS-077`](rendering/GRAPHICS-077-transient-debug-primitive-upload-helper.md), [`GRAPHICS-078`](rendering/GRAPHICS-078-visualization-overlay-upload-helper.md).
- Default-recipe ImGui pass: [`GRAPHICS-079`](rendering/GRAPHICS-079-default-recipe-imgui-pass-wiring.md).
- Runtime adapter umbrellas (clarified by `Q` follow-ups): [`runtime/RUNTIME-080..084`, `RUNTIME-090`](runtime/) — texture asset bridge, camera controllers, spatial-debug adapters, visualization adapters, gizmo interaction, ImGui adapter.
- Bootstrap scaffold retirement: [`rendering/GRAPHICS-081`](rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) — once `GRAPHICS-070..076` are retired and the default recipe renders the reference triangle, the `MinimalDebugSurface` recipe + `Pass.Surface/Present.MinimalDebug` classes + three diagnostics counters introduced by `GRAPHICS-032`/`033` are deleted. The scaffold exists only to derisk the triangle path; it must not outlive the operational default recipe.

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
- [`ecs/HARDEN-060` (done)](../done/HARDEN-060-ecs-scene-bootstrap-contract.md).
- [`ecs/HARDEN-061` (done)](../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md).
- [`ecs/HARDEN-062` (done)](../done/HARDEN-062-ecs-layering-and-component-boundary-hardening.md).
- [`done/HARDEN-063`](../done/HARDEN-063-ecs-events-and-command-seams.md) (done).
- [`ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md`](ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) (also Theme C).
- [`done/HARDEN-065`](../done/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md) (done).
- [`done/HARDEN-066`](../done/HARDEN-066-ecs-render-sync-export-policy.md) (done).
- [`done/HARDEN-067-ecs-bounds-propagation-system.md`](../done/HARDEN-067-ecs-bounds-propagation-system.md) (done).
- [`done/HARDEN-068`](../done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md) (done).

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
- [`done/RUNTIME-091`](../done/RUNTIME-091-promoted-ecs-system-bundle-activation.md) (done) (also Theme D).
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`ui/RORG-031-ui-integration.md`](ui/RORG-031-ui-integration.md).
- [`done/HARDEN-067-remove-stale-platform-linuxglfwvulkan.md`](../done/HARDEN-067-remove-stale-platform-linuxglfwvulkan.md) (done).
- [`platform/PLATFORM-004-alternative-platform-backend-onboarding.md`](platform/PLATFORM-004-alternative-platform-backend-onboarding.md) (planning-only seed).

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
  promoted ECS scene/hierarchy/transform parity. `HARDEN-060`, `HARDEN-061`,
  and `HARDEN-062` are all retired to `tasks/done/`, so this gate is
  satisfied; the Theme A renderer leaves are unblocked on the ECS side.
- **RUNTIME-091 ⇐ HARDEN-061.** Runtime fixed-step ECS system activation depends
  on the promoted `TransformHierarchy` system and must keep composition in
  `runtime` rather than adding upward imports to `src/ecs`.
- **HARDEN-067 ⇐ RUNTIME-091 or equivalent scheduling decision.** Bounds
  propagation can be implemented independently, but default-runtime usefulness
  depends on a known ECS system activation path.
- **METHOD-001 ⇐ ARCH-001.** Rigid-body reference must wait for the physics
  layer ownership decision before runtime/ECS integration.
- **HARDEN-064 ⇐ ARCH-001.** ECS collider/rigid-body authoring contract must
  wait for the physics layer ownership decision.
- **GRAPHICS-035..058 ⇐ Theme A.** Rendering modernization leaves stay
  planning-only until the visible-geometry foundation is complete.
- **GRAPHICS-033D ⇐ GRAPHICS-033A (done), GRAPHICS-033B (done), GRAPHICS-033C (done).**
  The opt-in `gpu;vulkan` visible-triangle smoke composes all three
  prior children and runs only on hosts with Vulkan + GLFW.

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
