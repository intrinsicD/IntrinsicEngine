---
id: RUNTIME-190
theme: F
depends_on: []
maturity_target: Operational
---
# RUNTIME-190 — GPU property-texture bake module

## Status

- Completed and retired on 2026-07-21 at `Operational`; owner: Codex team.
- Commit reference: this retirement commit records the implementation, final
  evidence, review, and generated task inventory.
- Verification evidence:
  - the canonical `IntrinsicTests` target builds with ccache disabled after the
    documented stale-module-state incident;
  - the eight affected ASan runtime/import/module-lifetime cases pass 8/8,
    including document/world replacement and all seven import-format coverage
    regressions;
  - the complete CPU-supported selector passed 4,281/4,282 in 395.01 seconds.
    Its sole failure is the unrelated 408-byte GLFW/X11 input-method
    LeakSanitizer recurrence tracked by `BUG-118`; all texture-bake and runtime
    integration cases passed, and no sanitizer gate was weakened or excluded;
  - the exact capable-host Vulkan acceptance smoke
    `RuntimeSandboxAcceptanceGpuSmoke.PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan`
    passed 1/1 in 25.15 seconds with zero skips after a cache-disabled
    `ci-vulkan` build. It proves vertex, face, nearest-edge, rebake, rename,
    remove, multi-consumer fallback, render-time colormap, framebuffer, and
    shutdown behavior through real readback;
  - the selected-mesh representation/compatibility cohort passed 19/19,
    including direct rejection of storage/encoder combinations that do not
    match the selected property type;
  - strict layering passed over 753 files, 6,794 module/import references, and
    85 CMake links; strict test layout, task policy/state, documentation links,
    root hygiene, generated module inventory, clean-workshop automation, and
    whitespace checks pass.
- Architecture and clean-workshop review: graphics exposes only core/RHI/plain
  DTO vocabulary while runtime owns live ECS and asset-service composition;
  the graphics bake recorder is a named owning subsystem instead of renderer
  member growth. Frame-pass and recipe-edge rows are not applicable because
  the bake is an existing GPU-queue participant, not a frame-graph pass. The
  capability is actually `Operational`, so no maturity follow-up or temporary
  layering exception is owed. Weak target-lifetime tokens guard every deferred
  raw queue borrow across world replacement and shutdown. Full scorecard:
  [`2026-07-21-runtime-190-clean-workshop-review.md`](../../docs/reviews/2026-07-21-runtime-190-clean-workshop-review.md).

## Goal
- Consolidate mesh property-to-texture baking into an app-composed
  `IRuntimeModule` that owns GPU-only bake scheduling, generated-asset
  identity/lifetime, renderer-consumer bindings, and the data-only editor
  command/snapshot contract.

## Non-goals
- CPU texture-bake fallback or synchronous GPU readback.
- Deleting the standalone CPU bake helper used by legacy direct callers and
  deterministic compatibility tests; it must remain unreachable from the live
  editor/module route.
- Computing missing source properties as an implicit part of baking; object,
  world, tangent, or other normal fields must already exist on the entity.
- Texture-sourced attributes for point-cloud or graph rendering.
- Inventing a second asset registry, GPU texture manager, job system, module
  framework, or editor facade.
- Treating an RGBA colormap bake as the default representation of a scalar
  property.

## Context
- Runtime owns composition across ECS/property data, asset lifetime, graphics
  bake submission, and render bindings. Graphics owns the backend-neutral
  raster-bake plan/recording surface and shaders; app owns ImGui layout.
- The existing path is fragmented across `SelectedMeshTextureBake`,
  `MeshAttributeTextureBake`, `ObjectSpaceNormalBake{Queue,Service,Binding,
  Submission}`, `AssetWorkflowModule`, the Sandbox editor facade, and editor
  widgets. Most property kinds currently take a CPU bake path; only
  object-space vertex normals use the production Vulkan raster path.
- Source properties are authoritative. Vertex properties interpolate over a
  triangle, face properties are flat, and edge properties use the agreed
  nearest-triangle-edge reconstruction rule. Missing or invalid texcoords,
  topology, properties, or operational GPU capability fail closed.
- A generated texture is identified by entity, source domain/property, UV set,
  and user-visible output name. Rebaking the same identity replaces its asset
  contents. Renaming creates a distinct identity that is not selected by the
  old-name existence check.
- One generated texture may feed multiple compatible renderer consumers.
  Removing it unbinds every consumer, restores the corresponding property-
  buffer source, and destroys the generated asset through `AssetService`.
- Scalar outputs store raw scalar values and range metadata. Colormap choice is
  evaluated by the rendering shader so it remains interactive; RGBA baking is
  an explicit selectable encoding rather than the default.
- `GRAPHICS-105` remains the authority for unified mesh shading and per-
  renderable material source resolution. This task owns the separately scoped
  generalized GPU bake module and must update that task's deferral wording
  without folding its unrelated import/material cleanup into this patch.
- Right-sizing: `IRuntimeModule` is justified by two production consumers
  (post-import/default policy and editor/agent commands) plus cross-frame GPU
  lifetime and shutdown ordering. The implementation must collapse the current
  role-named bake fragments into the module/PImpl where practical; it must not
  wrap them in an additional Service→Queue→Binding→Submission chain. Reintroduce
  a split only when a second backend or independently owned lifetime requires it.

## Control surfaces
- Config: engine-wide default bake extent/encoding/colormap only if an existing
  validated config field is extended; per-entity bindings remain scene state.
- UI: selected-mesh property-texture list with bake/rebake, rename, remove,
  compatible multi-consumer bindings, encoding, range, and colormap controls.
- Agent/CLI: the same runtime command/service surface used by the UI; no
  app-private mutation path.

## Backends
- Backend axis: Vulkan GPU raster path only. Null/non-operational devices expose
  capability diagnostics and reject bake submission without CPU fallback.

## Required changes
- [x] Add an app-composed texture-bake `IRuntimeModule` and narrow service/
  command/event DTOs following the established runtime-module lifecycle.
- [x] Move ownership of the existing production object-space normal bake
  participant out of `AssetWorkflowModule` into the new module while preserving
  exact world/epoch, cache-generation, frame-ready, stale-completion, and
  shutdown-retirement invariants.
- [x] Replace the CPU `MeshAttributeTextureBake` execution route in selected-
  mesh commands with a generalized GPU raster-bake request for vertex, face,
  and nearest-edge properties.
- [x] Add graphics-owned property-raster bake planning/recording and shaders for
  raw scalar, vector, normal, color, label, and explicit RGBA-colormap encodings
  without passing Vulkan types through graphics/runtime public APIs.
- [x] Add runtime generated-texture records keyed by entity/domain/property/UV
  set/output name, deterministic replace-on-rebake, rename, and destroy-on-
  remove behavior through `AssetService`.
- [x] Support one generated texture bound to multiple compatible material or
  visualization consumers; reject incompatible bindings with diagnostics and
  restore property-buffer sources atomically when a texture is removed.
- [x] Add a raw-scalar texture consumer contract carrying range and colormap ID
  through extraction/material data to the promoted forward and deferred shaders.
- [x] Keep property computation separate: validate the requested property exists
  and has the required domain/count/type before scheduling any GPU work.
- [x] Replace the existing selected-mesh editor controls with data-only module
  snapshots/commands for bake/rebake, rename, remove, encoding, colormap, and
  multi-consumer selection.
- [x] Remove superseded live-route facade/queue fragments after their callers
  migrate. Keep the explicitly accepted standalone CPU compatibility API
  labeled as such, without re-exporting or selecting it from the module/editor
  path.

## Tests
- [x] CPU/null contracts cover property/domain/type/UV validation, GPU-only
  capability failure, identity/rebake/rename/remove rules, multi-consumer
  compatibility, atomic property-buffer fallback, and stale generation rejection.
- [x] CPU/null shader/packing contracts cover raw scalar range + colormap data,
  normal-space metadata, and identical forward/deferred decoding rules.
- [x] Runtime integration tests prove module omission, registration/resolution,
  world replacement, asset destruction, and shutdown ordering.
- [x] App presentation tests prove only compatible consumers are selectable and
  that rename/remove/rebake commands use the shared runtime surface.
- [x] Opt-in `gpu;vulkan` smoke bakes representative vertex, face, and edge
  properties, verifies nonzero covered texels/readback, rebakes after source
  mutation, and demonstrates interactive scalar-colormap rendering.

## Docs
- [x] Update `src/runtime/README.md` for module ownership, commands, identity,
  lifecycle, and GPU-only failure behavior.
- [x] Update `docs/architecture/graphics.md` and renderer documentation for the
  generalized raster-bake and scalar-texture consumer contracts.
- [x] Synchronize the generalized-bake deferral/ownership wording in
  `GRAPHICS-105` and the runtime backlog indexes.
- [x] Regenerate `docs/api/generated/module_inventory.md` for module-surface
  changes and `tasks/SESSION-BRIEF.md` for this active task.

## Acceptance criteria
- [x] Every editor/agent property bake routes through the new runtime module and
  an operational GPU path; no CPU fallback executes.
- [x] Vertex, face, and nearest-edge mesh properties bake through valid atlas
  texcoords with explicit type/encoding/range diagnostics.
- [x] Object/world/other normal bakes consume existing named properties and do
  not silently compute missing fields.
- [x] Rebaking replaces the same named generated asset; renaming preserves a
  distinct asset; removing destroys the asset and restores all affected
  consumers to property-buffer rendering.
- [x] One generated texture can feed multiple compatible consumers without
  mutating another renderable that shares an authored material.
- [x] Raw scalar textures support render-time colormap changes without rebaking;
  RGBA colormap baking remains an explicit selectable target.
- [x] Runtime owns no Vulkan handles, graphics owns no live ECS/AssetService,
  app owns no private mutation path, and strict layering remains green.
- [x] The path reaches `Operational` only with an actually-run `gpu;vulkan`
  smoke; CPU/null tests alone do not close the task.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

Incremental-build note (2026-07-21): after the module-surface edits, Clang 23
ICEd while serializing the unrelated `Sandbox.MethodPanels` BMI. Rebuilding the
same `ExtrinsicSandboxEditor` target with `CCACHE_DISABLE=1` completed
successfully; this was stale incremental module state, not a source workaround.

## Forbidden changes
- Adding a CPU fallback or presenting CPU-produced output as the GPU result.
- Computing normals or other missing properties inside the bake operation.
- Baking scalar fields to RGBA by default or requiring rebake for colormap edits.
- Silently treating edge values as vertex/face values instead of nearest-edge
  reconstruction.
- Mutating shared authored materials when changing one renderable's bindings.
- Leaking `Vk*`, live ECS, runtime, app, or `AssetService` ownership across the
  layer boundaries in `AGENTS.md`.
- Adding another public texture pool/manager or parallel editor facade.
- Mixing unrelated `GRAPHICS-105` import/shading cleanup into this task.

## Maturity
- Target reached: `Operational` on the exercised Vulkan host and
  `CPUContracted` for the
  data/lifetime/error contracts on Null/headless hosts.
- Slice A extracts module ownership and migrates the existing operational
  object-space-normal path while deleting its selected-mesh CPU fallback.
- Slice B generalizes the graphics raster contract and GPU participant to
  vertex/face/nearest-edge property encodings.
- Slice C lands generated-asset identity, replace/rename/remove, and atomic
  multi-consumer binding/fallback behavior.
- Slice D lands raw-scalar shader consumption, runtime colormap selection, and
  promoted forward/deferred parity.
- Slice E lands the app-owned UI and shared agent/editor commands.
- Slice F adds representative Vulkan readback/render smokes and retires
  superseded live-route fragments. The explicitly accepted standalone CPU
  compatibility helper remains outside the interactive path. No `Operational`
  follow-up is owed after Slice F.
