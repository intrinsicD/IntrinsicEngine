# ASSETIO-003 — KTX texture import decision and handoff

## Goal
- Decide whether KTX/KTX2 texture support is necessary for current promoted asset/render workflows, and implement only the selected subset plus runtime-to-GPU handoff if it is accepted.

## Non-goals
- No streaming virtual texture implementation; `GRAPHICS-055` remains the planning contract for SVT.
- No GPU transcoding or Basis Universal encoder pipeline in this slice unless explicitly selected by the implementation plan.
- No broad file-format visual sweep; `ASSETIO-004` owns the multi-format proof.

## Context
- Owner/layer: `assets` owns CPU payload shape and route classification; `runtime` owns decoder registration and upload scheduling; `graphics/assets` owns GPU residency through `GpuAssetCache`.
- `Asset.ImportRouter` already recognizes `KTX`, but the parity matrix still lists KTX decode as unproven.
- Reuse `Asset.ModelTextureIOBridge`, `Asset.ModelTexturePayload`, `Runtime.AssetModelTextureIO`, `Runtime.AssetModelTextureHandoff`, `RHI.TextureUpload`, and `Graphics.GpuAssetCache`.

## Value gate
- Current state: common raster texture formats are already routed through promoted model/texture bridges, and KTX currently has route-level recognition only.
- Improvement: KTX is worth retaining only if checked-in assets, renderer tests, or material workflows need compressed/mip/array texture payloads.
- Scope decision: first select a narrow supported subset or retire KTX as unsupported with deterministic diagnostics. Do not add a general KTX/Basis/transcoding pipeline by default.

## Required changes
- [ ] Inventory current checked-in assets, tests, and sample workflows for actual KTX/KTX2 use before accepting implementation.
- [ ] Define the supported KTX/KTX2 subset and fail-closed diagnostics before implementing decode.
- [ ] Extend promoted texture payload metadata only as needed for compressed/transcodable formats, mip levels, array layers, color space, and block-compression layout.
- [ ] Register runtime-owned KTX/KTX2 texture importer callbacks through `Asset.ModelTextureIOBridge`.
- [ ] Map decoded or transcodable payloads to `GpuAssetCache::RequestUpload` through the existing runtime handoff.
- [ ] Add fallback/error handling for unsupported KTX variants without changing existing PNG/JPEG/TGA/BMP/HDR behavior.

## Tests
- [ ] Add `unit;assets` route and payload-validation tests for supported and unsupported KTX variants.
- [ ] Add `contract;runtime` tests for KTX callback registration, decode diagnostics, and texture Ready-event upload requests.
- [ ] Add an opt-in `gpu;vulkan` smoke only if the selected KTX subset needs backend upload proof beyond CPU/null contracts.

## Docs
- [ ] Update `src/assets/README.md` and `src/graphics/assets/README.md` with the KTX ownership split.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` and `tasks/backlog/assets/README.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] KTX/KTX2 has an explicit retained subset or an explicit retirement decision based on current promoted workflow needs.
- [ ] KTX/KTX2 import requests no longer fall through as an unimplemented route when a retained supported subset is used.
- [ ] Unsupported KTX variants fail closed with deterministic diagnostics.
- [ ] Runtime upload handoff uses the same event-driven path as other promoted texture payloads.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Texture|Ktx|KTX' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing graphics/RHI/runtime from `src/assets`.
- Replacing the existing texture upload handoff with a parallel path.

## Maturity
- Target: `CPUContracted`; `Operational` GPU proof is required only if compressed upload behavior cannot be validated through existing RHI contracts.
