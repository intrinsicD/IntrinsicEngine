# Architecture Decision Records (ADRs)

This index tracks long-lived architecture decisions for IntrinsicEngine.

## Active ADRs

1. [0001 — Minimal runtime refactor](0001-minimal-runtime-refactor.md) — *superseded by [0024](0024-kernel-module-architecture.md)*
2. [0002 — Pragmatic medium runtime refactor](0002-pragmatic-medium-runtime-refactor.md) — *superseded by [0024](0024-kernel-module-architecture.md)*
3. [0003 — Ideal runtime architecture](0003-ideal-runtime-architecture.md) — *refined/superseded by [0024](0024-kernel-module-architecture.md)*
4. [0004 — Vulkan backend bring-up and fail-closed fallback](0004-vulkan-backend-bringup-and-fallback.md)
5. [0005 — Vulkan operational readiness gate and runtime reconciliation](0005-vulkan-operational-readiness-gate.md)
6. [0006 — Camera, picking-request, and gizmo runtime handoff](0006-camera-picking-and-gizmo-runtime-handoff.md)
7. [0007 — Picking, selection, and outline reporting seam](0007-picking-selection-and-outline.md)
8. [0008 — Spatial debug visualizer runtime adapters](0008-spatial-debug-visualizer-adapters.md)
9. [0009 — Visualization packets, validation, and overlay upload](0009-visualization-packets-and-overlay-upload.md)
10. [0010 — Postprocess chain backend policy](0010-postprocess-chain-backend-policy.md)
11. [0011 — Debug-view inspection table and visualization mode mapping](0011-debug-view-inspection-table.md)
12. [0012 — ImGui overlay submission and `Pass.Present` finalization](0012-imgui-overlay-and-present-finalization.md)
13. [0013 — ECS renderable residency bridge](0013-ecs-renderable-residency-bridge.md)
14. [0014 — Procedural-source residency bridge](0014-procedural-source-residency-bridge.md)
15. [0015 — Runtime reference scene bootstrap](0015-reference-scene-bootstrap.md)
16. [0016 — Texture residency, fallback, and asset cache policy](0016-texture-residency-and-asset-cache-policy.md)
17. [0017 — Default debug surface material (slot 0)](0017-default-debug-surface-material.md)
18. [0018 — Missing-material fallback substitution and diagnostics](0018-missing-material-fallback-substitution.md)
19. [0019 — Physics layer ownership and ECS integration](0019-physics-layer-ownership-and-ecs-integration.md)
20. [0020 — vcpkg manifest dependency management](0020-vcpkg-manifest-dependency-management.md)
21. [0021 — Progressive entity render-data pipeline](0021-progressive-entity-render-data-pipeline.md)
22. [0022 — Vertex storage: uniform SoA with per-channel streaming](0022-vertex-storage-soa-per-channel-streaming.md)
23. [0023 — CPU↔GPU transfer foundation: async readback ring, buffer-transfer math, and a transfer facade](0023-cpu-gpu-transfer-foundation.md)
24. [0024 — Kernel/module runtime architecture and communication contract](0024-kernel-module-architecture.md)
25. [0025 — Parameterization UV view: derived second view, not a separate entity](0025-parameterization-uv-view-and-split-view.md)

## Conventions

- ADR files are numbered chronologically using `NNNN-<slug>.md`.
- New ADRs should be created from [template.md](template.md).
- Superseded ADRs remain in place and should be marked with an explicit status section.
