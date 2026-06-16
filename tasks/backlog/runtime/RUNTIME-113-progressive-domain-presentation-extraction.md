---
id: RUNTIME-113
theme: F
depends_on: [RUNTIME-111]
maturity_target: CPUContracted
---
# RUNTIME-113 — Progressive domain presentation extraction

## Goal
- Make runtime extraction consume progressive render-data descriptors so mesh,
  graph, and point-cloud lanes can render with defaults, authored/generated
  texture slots, and property-buffer presentation as data becomes ready.

## Non-goals
- No import-time computation or derived-job scheduling.
- No editor UI controls.
- No Vulkan-only proof; backend operational smoke is deferred.
- No GPU compute or graphics-side property discovery.
- No displacement shading beyond descriptor pass-through or explicit
  unsupported diagnostics.

## Context
- Owning subsystem/layer: `runtime` owns live ECS extraction and sidecars;
  graphics consumes snapshots, asset IDs, bindless/material records, and
  graphics-owned buffers.
- ADR-0021 separates render intent components from lane presentation bindings.
- `RUNTIME-111` supplies descriptors and compatible binding state. This task
  proves those descriptors can drive existing extraction paths without blocking
  immediate raw geometry visibility.
- Mesh, graph, and point-cloud paths must be handled with equal priority.

## Required changes
- [ ] Teach extraction to resolve lane-to-presentation bindings for
      `RenderSurface`, `RenderEdges`, and `RenderPoints` without storing full
      materials in those render-lane components.
- [ ] For mesh surface lanes, submit uniform defaults immediately and attach
      authored/generated texture slots only when each slot is ready.
- [ ] For mesh face-color and scalar-field slots, resolve exact face-domain
      presentation to a texture-backed or equivalent data path with explicit
      unsupported diagnostics where a backend is not yet available.
- [ ] For graph lanes, resolve vertex/node and edge property buffers
      independently so selected vertices and selected edges can be highlighted
      or colored separately.
- [ ] For point-cloud lanes, resolve color, scalar, size, and
      normal/orientation property buffers where compatible properties exist.
- [ ] Preserve previous valid generated outputs during pending or failed
      replacement states.
- [ ] Surface extraction diagnostics for missing properties, incompatible
      types, stale generations, pending generated outputs, unsupported domains,
      and fallback/default usage.
- [ ] Keep graphics-facing data free of live ECS, raw property pointers,
      `AssetService`, and runtime ownership.

## Tests
- [ ] Add CPU/null extraction tests for mesh surface default rendering with
      pending normal/albedo/scalar slots.
- [ ] Add CPU/null extraction tests for mesh face-domain color/scalar
      presentation diagnostics and ready-state handling.
- [ ] Add CPU/null extraction tests for graph vertex/node and edge property
      buffers, including separate selected vertex/edge highlight domains.
- [ ] Add CPU/null extraction tests for point-cloud color, scalar, size, and
      normal/orientation property-buffer descriptors.
- [ ] Add stale/pending/failure tests proving previous valid outputs remain
      bound until replacement output is ready.
- [ ] Preserve existing render-lane component and geometry residency coverage.

## Docs
- [ ] Update `src/runtime/README.md` with progressive extraction behavior and
      descriptor-to-snapshot mapping.
- [ ] Update renderer/runtime docs if snapshot material or property-buffer
      ownership changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [ ] A mesh with only positions, UVs, and indices extracts a surface lane with
      defaults before normals or generated textures are ready.
- [ ] Graph vertex and edge presentation can resolve separate property domains
      and report independent diagnostics.
- [ ] Point-cloud presentation can resolve point-domain color/scalar/size/
      normal descriptors or fall back to defaults with visible diagnostics.
- [ ] Extraction never blocks on derived jobs and never applies worker results.
- [ ] The default CPU-supported CTest gate verifies cross-domain presentation
      extraction.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RenderExtraction|Progressive|Presentation|PropertyBuffer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not import live ECS, runtime, or `AssetService` into graphics layers.
- Do not block extraction on asynchronous derived work.
- Do not treat mesh presentation as the only first-class path.
- Do not fabricate mesh UVs or property buffers in graphics.
- Do not implement displacement rendering in this task.

## Maturity
- Target: `CPUContracted`.
- This task closes runtime extraction and snapshot contracts for progressive
  presentation. `Operational` owned by `GRAPHICS-090`.
