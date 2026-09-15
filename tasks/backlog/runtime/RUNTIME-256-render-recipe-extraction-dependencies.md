---
id: RUNTIME-256
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog: narrow implementation-only includes/imports with exact unchanged bodies; no public surface, ownership, method, data, control or workflow contract changes. Existing render-extraction and compiler-boundary contracts remain authoritative."
---
# RUNTIME-256 — Narrow visualization-recipe extraction dependencies

## Goal
Remove the copied, unused renderer/ECS preamble from the existing recipe
extraction implementation unit; preserve all declarations and executing bodies.

## Non-goals
No public interface, algorithm, ownership, backend, new file/helper/module,
State layout, other extraction sibling or compatibility change. No timing claim.

## Context
Operator-authorized overnight cleanup with Claude until 2026-09-15 08:00
Europe/Berlin; stop new implementation by 07:15, local commits only. Claim only
after RUNTIME-255 is retired/sealed and its writer and builds have stopped.
Standing source-sharing authorization applies. BUILD-007/C92 own matched timing.

Claude's read-only discovery identified copied import blocks in the extraction
siblings. Root selected only `src/runtime/Rendering/Runtime.RenderExtraction.Recipes.cpp`
after reading its full body. All graphics names belong to VisualizationPackets;
other named types/functions belong to VisualizationRecipes/GeometryAvailability
and the existing State. The broader Geometry.cpp/editor-import lists were based
on incomplete symbol inventories and are deferred, not pre-approved removals.

Keep `import :Internal`, Graphics.VisualizationPackets,
Runtime.GeometryAvailability and Runtime.VisualizationRecipes. Remove the other
20 copied imports. Include the standard types directly used by the body:
cstddef, cstdint, cstring, limits, optional, span, string, utility and vector.
Remove unused memory/unordered_map/unordered_set, EnTT and GLM includes. The
proposed file is 313 to 288 lines; establish exact current counts at claim.
Every byte from `namespace Extrinsic::Runtime` onward must stay identical.

Claude approved the bounded plan. Its concern about unqualified runtime types
was checked against the three dropped Runtime interface type sets and the full
recipe body. Compilation remains the arbiter. State still owns its genuine
transitive dependencies; record the actual compiler closure rather than
predicting a dependency count from deleted import lines.

Reuse/right-sizing: keep the single existing State definition and compiled
recipe owner; narrow imports without another header, abstraction or test double.

## Required changes
- [ ] Keep only actually required direct declarations/includes in Recipes.cpp.
- [ ] Preserve all declarations/bodies and all other production files byte-for-byte.
- [ ] Record exact source and compiler dependency changes without a speedup claim.

## Tests
- [ ] Baseline/final source hashes and compiler map demonstrate unchanged bodies and removal of Renderer from this TU's closure.
- [ ] Focused extraction/visualization and full CPU gates pass; focused ASan/UBSan pass.
- [ ] Promoted-Vulkan runtime target compiles; no new GPU execution claim.

## Docs
- [ ] Update the existing extraction architecture paragraph only as necessary.
- [ ] Record fixed-source Claude review, passing receipts, retirement and exact source seal.

## Acceptance criteria
- [ ] One implementation preamble is narrowed without body or public-surface changes.
- [ ] Fixed-source review and relevant native/sanitizer/build gates pass.
- [ ] Completed slice is locally committed, retired and sealed; no new production file or timing claim.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --check-source src/runtime/Rendering/Runtime.RenderExtraction.Recipes.cpp --forbid-module Extrinsic.Graphics.Renderer
ctest --test-dir build/ci --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-vulkan --target ExtrinsicRuntime -j2
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Keep existing supported Clang 23 preset trees; no new build tree under BUG-195's
headroom limit. Run variants sequentially and keep cache disabled consistently
for BUG-178. An optional expected baseline forbidden-Renderer failure is evidence,
never a final gate. No new test is necessary for a strictly preamble-only change.

## Forbidden changes
- Editing State/declarations/bodies, primary module or other extraction siblings.
- Removing a required import to satisfy a count, or weakening any compiler/test gate.
- New wrappers, interface aliases, compatibility machinery or performance claims.
