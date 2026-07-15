---
id: ASSETIO-009
theme: B
depends_on:
  - REVIEW-003
  - GRAPHICS-105
maturity_target: CPUContracted
---
# ASSETIO-009 — Loss-aware MaterialX subset import

## Goal

- Import a validated CPU-only subset of MaterialX Standard Surface/OpenPBR
  through a standalone material-library asset route and container that reuses
  the canonical `Assets::AssetModelMaterialPayload`, preserves source
  provenance, and reports every unsupported or lossy construct explicitly.

## Non-goals

- No arbitrary MaterialX graph runtime, evaluator, renderer, shader generator,
  Slang codegen, material specialization, hot reload, or editor graph UI.
- No MaterialX SDK/header or concrete decoder dependency in `src/assets`;
  assets remain CPU-only and depend on `core` only. Runtime privately owns the
  decoder dependency and registration callback.
- No graphics, RHI, Vulkan, ECS, renderer-material handoff, entity creation, or
  visibility integration in this task.
- No claim of full MaterialX, Standard Surface, or OpenPBR fidelity.
- No silent approximation of closures, procedural nodes, transforms, units,
  color spaces, texture addressing, or parameters outside the declared subset.
- No new material registry, second IO bridge, or duplicate CPU/GPU material
  descriptor.

## Context

- Owner/layer: `assets` owns the `.mtlx` route, primary/external byte transport,
  standalone material-library payload and diagnostics, callback dispatch, and
  payload validation. Runtime privately links MaterialX, parses untrusted input,
  maps it into asset-owned records, and registers that concrete decoder on the
  existing asset IO bridge. Neither layer performs graphics/ECS handoff here.
- A material-only `.mtlx` document is not a model scene. It must not be wrapped
  in a fake `AssetModelScenePayload`; the new container reuses
  `AssetModelMaterialPayload` elements while giving their texture indices a
  material-library-local source table and validator.
- V1 is deliberately loss-aware: only parameters and texture references that
  can be represented faithfully by the canonical payload are materialized.
  Unsupported graph structure remains identified in diagnostics and the
  original source/provenance stays available for later re-import or tooling.
- The local vcpkg catalog contains MaterialX 1.39.5, but the repository manifest
  does not currently depend on it.
- Sources: [MaterialX specification](https://materialx.org/Specification.html),
  [MaterialX repository](https://github.com/AcademySoftwareFoundation/MaterialX),
  and [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/).
- Dependency policy: MaterialX must enter through `vcpkg.json` and an explicit
  baseline/override or repository overlay if required. No FetchContent,
  submodule, copied SDK, or direct download is permitted.

## Right-sizing

- Element under evaluation: one new route/container and one concrete runtime
  decoder callback with an explicit mapping table; the file-format boundary
  justifies the parser dependency but not a material-graph framework.
- Simpler alternative: extend `Asset.ImportRouter`,
  `Asset.ModelTexturePayload`, and `Asset.ModelTextureIOBridge` with plain
  records/free validation and one callback lane; reuse
  `AssetModelMaterialPayload` rather than adding a new bridge or descriptor.
- Blast radius: vcpkg metadata, focused assets route/payload/bridge extensions,
  one private runtime decoder/registration implementation, fixtures,
  unit/contract tests, and format docs. No renderer, graphics, ECS, or asset-to-
  scene visibility changes.
- Reintroduction trigger: Slang generation or arbitrary graph execution needs
  a separate task with a present renderer/method consumer and fidelity corpus.

## Required changes

- [ ] Add a standalone `.mtlx` import route: a dedicated `AssetFileFormat`
      value and `AssetPayloadKind::MaterialLibrary` (or equivalently named
      asset-owned kind) in `Asset.ImportRouter`. It must not resolve as
      `ModelScene` and has no export route in V1.
- [ ] Add an asset-owned `AssetMaterialLibraryPayload` (or equivalently focused
      name) containing source identity/version/hash, a bounded external texture-
      source table, `std::vector<AssetModelMaterialPayload>`, and structured
      import diagnostics. `AssetModelTextureReference::ImageIndex` is validated
      against this container-local texture-source table; no second material
      descriptor is introduced.
- [ ] Add focused validation for the material-library container: finite/ranged
      factors, bounded counts/strings/source bytes, valid texture-source indices
      and URIs, stable provenance, unique material identity where required, and
      diagnostics consistent with full versus partial fidelity.
- [ ] Extend the existing `Asset.ModelTextureIOBridge` with one material-library
      callback/registration/import lane. Assets own IO, relative external reads,
      missing-callback errors, dispatch, and post-callback validation exactly as
      they do for model/texture payloads; do not add another bridge/interface.
- [ ] Pin the smallest required MaterialX 1.39.x feature set through the
      repository vcpkg manifest/override/overlay path and link it privately only
      to the runtime decoder owner; do not enable render, viewer, Python, or
      code-generation features not needed by CPU parsing.
- [ ] Extend the existing runtime model/texture IO registration path with one
      private MaterialX decoder callback. MaterialX headers/types stay in the
      runtime implementation and never cross the callback or asset module
      boundary.
- [ ] Define a versioned V1 mapping table for the representable Standard
      Surface/OpenPBR fields: base color, metallic, roughness, opacity when
      exactly compatible, and canonical base-color/normal/metallic-roughness/
      occlusion texture references with explicit color-space expectations.
- [ ] Parse untrusted `.mtlx` input with bounded file/document/node/string
      limits and fail-closed diagnostics for malformed XML, unsupported
      versions, cyclic graphs, unresolved references, non-finite values,
      invalid texture paths, and resource-limit exhaustion.
- [ ] Return the asset-owned material-library container with reused
      `AssetModelMaterialPayload` entries plus structured diagnostics that
      identify unsupported node/category/input paths, lossy-but-rejected
      mappings, defaults used, unresolved resources, and source locations.
- [ ] Preserve source document identity/version and sufficient original source
      or stable source reference/hash for round-trip/re-import. Preservation
      must not make graphics/runtime own the MaterialX graph.
- [ ] Reject unsupported closures/procedurals rather than baking, flattening,
      or silently substituting them. A document may return supported materials
      with explicit partial diagnostics only when each returned material is
      independently faithful.
- [ ] Add a small checked fixture corpus: faithful Standard Surface, faithful
      OpenPBR, texture-backed material, defaults, unsupported closure,
      procedural graph, unresolved image, cycle, malformed document, huge
      count/string, and non-finite input.
- [ ] Route `.mtlx` bytes through the asset-owned bridge and validator, proving
      the standalone payload can be stored/read as its own asset kind. Do not
      invoke model-scene handoff, mint ECS entities/material instances, upload
      textures, or make the imported library visible in the renderer.

## Tests

- [ ] Asset unit/contract tests cover `.mtlx` route resolution, explicit
      `MaterialLibrary` kind, missing/duplicate callback registration, primary/
      external byte transport, callback error propagation, container validation,
      and rejection of attempts to route the document as `ModelScene`.
- [ ] Runtime decoder tests prove exact scalar/color/texture mapping,
      deterministic defaults, source provenance, stable diagnostics, and
      preservation of supported input across parse/serialize/reparse where
      available.
- [ ] Malformed/untrusted-input tests cover cycles, unresolved references,
      resource limits, path traversal/invalid URIs, non-finite values, and
      unsupported nodes without asserts, partial mutation, or NaN leakage.
- [ ] Contract tests prove resulting libraries pass the focused material-
      library validator and that unsupported constructs never masquerade as
      fully supported imports. They must not use
      `ValidateAssetModelScenePayload` as a material-only compatibility shim.
- [ ] Existing glTF/model-material import tests remain unchanged and green.

## Docs

- [ ] Document the exact V1 supported-node/input table, defaults, texture/color-
      space rules, diagnostics, resource limits, provenance preservation, and
      examples of rejected lossy mappings.
- [ ] Document the route/container/callback ownership: assets own transport and
      validated `AssetModelMaterialPayload` collections; runtime owns the
      concrete MaterialX decoder; the result is neither a model scene nor a
      graphics material/executable graph.
- [ ] Update third-party/dependency documentation for the vcpkg-pinned minimal
      MaterialX feature set and private runtime linkage.

## Acceptance criteria

- [ ] `.mtlx` resolves only to the standalone material-library asset kind, and
      the asset-owned bridge validates callback output without importing or
      linking MaterialX in `src/assets`.
- [ ] Every declared V1 fixture maps deterministically and exactly into reused
      canonical CPU material records inside the validated material-library
      container; no fake model-scene payload is constructed.
- [ ] Every unsupported, lossy, malformed, cyclic, unresolved, non-finite, or
      over-budget case returns a structured fail-closed/partial diagnostic with
      source location/provenance; none silently changes semantics.
- [ ] Runtime privately owns and registers the concrete MaterialX decoder;
      MaterialX types do not cross into asset interfaces, and no graphics/ECS
      handoff, executable graph, shader generator, UI, or renderer
      specialization lands.
- [ ] MaterialX is resolved solely through the vcpkg manifest path with only
      required features enabled.
- [ ] Default CPU and structural gates pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MaterialX|MaterialLibrary|AssetImportRouter|AssetModel.*Material' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- FetchContent, copied MaterialX sources, submodules, runtime downloads, or any
  dependency path outside vcpkg manifest/override/overlay files.
- Silent fallback/approximation for unsupported MaterialX semantics.
- MaterialX headers/linkage or concrete decoder code in `src/assets`; imports
  from assets to graphics, RHI, Vulkan, runtime, or ECS.
- Arbitrary graph execution, Slang/codegen, renderer specialization, hot
  reload, editor UI, or unrelated asset-format work.
- Introducing a second material descriptor instead of extending/reusing the
  canonical asset payload and diagnostics.
- Treating `.mtlx` as `ModelScene`, constructing an empty/fake model scene to
  carry materials, adding a second IO bridge, or integrating the imported
  library with renderer/ECS visibility in this task.

## Maturity

- Target: `CPUContracted`; the standalone route/container, asset-owned bridge
  contract, runtime decoder, and fail-closed fidelity behavior are the intended
  endpoint. No `Operational` follow-up is owed.
