# Architecture Decision Records (ADRs)

This index tracks long-lived architecture decisions for IntrinsicEngine.

## Active ADRs

1. [0001 — Minimal runtime refactor](0001-minimal-runtime-refactor.md)
2. [0002 — Pragmatic medium runtime refactor](0002-pragmatic-medium-runtime-refactor.md)
3. [0003 — Ideal runtime architecture](0003-ideal-runtime-architecture.md)
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

## Conventions

- ADR files are numbered chronologically using `NNNN-<slug>.md`.
- New ADRs should be created from [template.md](template.md).
- Superseded ADRs remain in place and should be marked with an explicit status section.
