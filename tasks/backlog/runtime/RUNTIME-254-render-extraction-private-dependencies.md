---
id: RUNTIME-254
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
contract_review: "Reviewed the catalog: this removes unnecessary private compile dependencies without changing an exported module, ownership, method/data/control contract or reusable workflow. Existing renderer-borrow architecture and compiler-boundary checks remain authoritative."
---
# RUNTIME-254 — Narrow render-extraction private dependencies

## Goal
Remove unnecessary renderer and full EnTT dependencies from the declaration-only
render-extraction private implementation partition, preserving all behavior.

## Non-goals
No API/State layout/algorithm/backend change, new production file, Pimpl, helper,
partition split or timing claim. No ECS Registry redesign. MaterialSystem remains
transitively required by the owned MaterialInstance lease; do not remove it there.

## Context
Operator-directed cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. BUG-134 is retired in
`3e2dc448e`, with evidence sealed in `8ba6221a4`. Claim clean main first.
Standing Claude source-sharing authorization applies.

Source discovery and Claude's bounded design assessment identify three preamble
changes in `src/runtime/Rendering/Runtime.RenderExtraction.Internal.cpp`:
- Replace `entt/entity/entity.hpp` and `entt/entity/registry.hpp` with
  `entt/entity/fwd.hpp`; only entity handles and borrowed registry references occur.
- Remove `import Extrinsic.Graphics.Renderer`; every IRenderer use is a reference,
  and the primary interface already declares the globally attached IRenderer.
- Remove the redundant direct MaterialSystem import. Actual graph discovery shows
  `Internal -> Graphics.Component.Material -> Graphics.MaterialSystem`, required
  by `MaterialInstance::Lease`. Do not claim that module leaves the closure.

All three executing siblings (`Runtime.RenderExtraction.cpp`, `.Geometry.cpp`,
`.Recipes.cpp`) already directly include full EnTT headers and import Renderer
and MaterialSystem. The private partition owns State layout once; keep it.
Reuse/right-sizing decision: narrow dependency edges at the present owner.
Keep lease lifetime and the single shared State definition. Reinstate a full
include/import only when a real definition or inline operation requires it.
No missing helper or duplicate algorithm warrants new infrastructure.
Claude approved the fixed-source plan on 2026-09-15. Its only caveat was resolved:
`MaterialTextureAssetBindings` is exported by `Graphics.Material.cppm:354`, already
imported directly, so removing MaterialSystem does not lose that name's visibility.
The fixed source packet and review are retained temporarily under
`/tmp/intrinsic-overnight-20260915/bug134/claude-next-plan.txt`; this task owns scope.

## Slice plan
1. Save exact source/body hashes and the configured compiler dependency map.
2. Claude implements only the three preamble changes as sole source writer.
3. Root compares all declarations/State bytes below imports and builds existing
   targets. Add a direct sibling include only if compilation proves it required;
   any declaration/layout/behavior change is outside this slice.
4. Claude reviews the fixed final diff; run final gates, update the existing runtime
   architecture paragraph, retire and seal evidence. No measured timing claim.

## Required changes
- [ ] Private partition borrows Renderer/EnTT types with only required declarations.
- [ ] State/layout/declarations and all implementation bodies are byte-identical.
- [ ] No new production file or abstraction; real material lease ownership retained.

## Tests
- [ ] Baseline/final compiler maps show Renderer leaves the private closure; record counts honestly.
- [ ] Existing extraction/frame tests and full CPU gate pass; focused ASan/UBSan pass.
- [ ] Promoted-Vulkan runtime compilation passes; no GPU execution change or capability claim.

## Docs
- [ ] Clarify the private declaration partition in the existing runtime architecture paragraph.
- [ ] Record reviewed source/counts/gates and retire the note with sealed evidence.

## Acceptance criteria
- [ ] Required dependency edges narrowed with unchanged declarations and behavior.
- [ ] Claude-reviewed final source passes compiler-boundary and existing verification.
- [ ] Completed slice locally committed, retired and sealed without a speedup claim.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --check-source src/runtime/Rendering/Runtime.RenderExtraction.Internal.cpp --forbid-module Extrinsic.Graphics.Renderer
ctest --test-dir build/ci --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(RenderExtraction|RuntimeRenderExtraction|RuntimeFrameLoop|RenderWorldPool|RuntimeDeviceSelection)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-vulkan --target ExtrinsicRuntime -j2
python3 tools/repo/check_layering.py --root src --strict
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Use supported existing Clang 23 preset trees and run variants sequentially.
No new tree under current disk limits; keep cache environment consistent.
A failed baseline boundary command is optional expected evidence, never a final
gate. Keep existing primary `RenderCompilationLocality.Extraction` unchanged;
this private include cleanup needs no new permanent behavioral/source-shape test.
BUILD-007/C92 retain matched compile timing. No benchmark or GPU capability claim.

## Forbidden changes
- Weakening existing compiler guards, tests, labels or genuine material ownership.
- Public/global type identity, State layout, method semantics or rendering changes.
- New wrappers, Pimpl, source files, broad test splits or compatibility machinery.
