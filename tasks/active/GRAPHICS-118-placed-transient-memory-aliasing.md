---
id: GRAPHICS-118
theme: B
depends_on: []
---
# GRAPHICS-118 — Placed transient resource allocation with real memory aliasing

## Status

- Status: in-progress.
- Owner/agent: Codex.
- Branch/PR: local `main` stack, PR not opened.
- Current slice: Slice D — renderer adoption of compiled transient placements.
- Last completed slice: Slice C — Vulkan placed allocation and binding behind
  the RHI contract; CPU/null and Vulkan fail-closed contracts verified.
- Next verification step: adopt compiled placements in renderer transient
  allocation, then run the opt-in GPU/Vulkan smoke with measured memory
  reduction.

## Slice plan

- **Slice A (this slice).** Compute deterministic placed-layout metadata from
  render-graph lifetime intervals, expose naive vs planned peak transient
  memory, and report alias-reuse hazards in the compiled barrier plan. This
  closes the CPU placement contract only and keeps renderer/RHI allocation
  behavior unchanged.
- **Slice B.** Add the RHI placed-memory contract and Null bookkeeping for
  memory block compatibility, placed texture creation, and placed buffer
  creation.
- **Slice C.** Implement Vulkan placed allocation and binding behind the RHI
  contract (VMA-backed memory blocks, placed buffer/image binds, ownership
  bookkeeping, and placement introspection). Renderer use of compiled
  alias-reuse barriers and layout state remains Slice D with adoption.
- **Slice D.** Adopt compiled placements in renderer transient allocation and
  cite the opt-in Vulkan smoke with measured memory reduction.

## Goal
- Make transient render-graph resources actually alias GPU memory: placed
  allocation (heap + offset) driven by the lifetime-interval analysis the
  compile path already performs, so peak transient memory approaches the
  max-live working set instead of today's
  `sum(all transients) × framesInFlight`.

## Non-goals
- No aliasing of imported/external resources — transients only.
- No change to barrier semantics beyond the aliasing hazards the plan
  requires (alias-reuse barriers are in scope; general barrier quality is
  `GRAPHICS-120`).
- No compile caching (`GRAPHICS-117`) — but the placement plan must be
  cache-friendly (deterministic for an unchanged graph).

## Context
- Owner/layer: `graphics/framegraph` (placement planning),
  `graphics/rhi` + `graphics/vulkan` (placed-resource creation),
  `graphics/renderer` (adoption).
- Today "transient aliasing" never reaches GPU memory: `TransientAllocator`
  pools virtual handles by exact desc match with no heap/offset placement
  (`src/graphics/framegraph/Graphics.RenderGraph.TransientAllocator.cpp:27-63`),
  and `AllocateFrameTransientResources` then creates/caches one real device
  texture/buffer per resource index per frame-in-flight slot
  (`src/graphics/renderer/Graphics.Renderer.cpp:6126-6219`), discarding the
  compiled handle aliasing. `SetTransientAliasingEnabled` only changes a
  CPU-side stat. The lifetime-interval sweep that placement needs already
  exists (`Graphics.RenderGraph.cpp:492-549`), and
  `TransientMemoryEstimateBytes` already reports an estimate (with a wrong
  BC-format size — fixed properly in `GRAPHICS-120`; placement must use RHI
  format sizing, not the local table).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R15.

## Backends
- Backend axis: Vulkan placed allocation (VMA-backed memory blocks +
  `vkBindImageMemory`/`vkBindBufferMemory` at offsets) behind an RHI
  contract; Null device implements the contract as bookkeeping so the CPU
  gate can prove planning. No `Vk*` types cross RHI/renderer APIs.

## Required changes
- [x] Slice A (planning, CPU-provable): compute a placed layout from the
      existing lifetime intervals — per-resource {block, offset, size,
      alignment} with non-overlapping live ranges, deterministic for a
      fixed graph; emit alias-reuse hazards (which pass first writes a
      reused range after its prior occupant's last read) into the barrier
      plan; expose planned peak bytes vs naive sum in stats.
- [x] Slice B: RHI contract for memory blocks + placed texture/buffer
      creation with alignment/`memoryTypeBits` compatibility queries; Null
      backend bookkeeping implementation.
- [x] Slice C: Vulkan implementation (VMA block allocation, placed binds,
      ownership/destruction bookkeeping, and placement introspection behind
      the RHI seam). Renderer use of compiled alias-reuse barriers and
      initial-layout handling remains Slice D with allocation adoption.
- [ ] Slice D: renderer adoption — `AllocateFrameTransientResources` binds
      compiled placements per frame-in-flight slot instead of per-resource
      allocations; `SetTransientAliasingEnabled` gates real behavior with
      the non-aliased path kept as the fallback/debug lane.

## Tests
- [x] CPU/null contract: placement never overlaps live ranges (property
      test over randomized graphs); deterministic layout for a fixed graph;
      alias hazards emitted exactly where reuse occurs.
- [x] CPU/null contract: planned peak bytes ≤ naive sum, and equals naive
      sum when aliasing is disabled.
- [x] CPU/null contract: Null RHI memory requirements, memory block creation,
      placed buffer/texture creation, placement introspection, compatibility
      rejection, alignment rejection, range rejection, and memory-block slot
      recycling.
- [x] Vulkan fail-closed/backend contract: Vulkan target builds under
      `ci-vulkan`; the unavailable-device constructor path rejects memory
      requirements, memory blocks, placed buffer/texture creation, and
      placement introspection without exposing Vulkan types through RHI.
- [ ] Opt-in `gpu;vulkan` smoke: default sandbox recipe renders correctly
      with aliasing on (image compare vs aliasing off), reported transient
      memory drops, and validation layers are clean (no hazard errors).

## Docs
- [x] Update `src/graphics/framegraph/README.md` and
      `docs/architecture/frame-graph.md` (what "transient aliasing" now
      means, fallback lane, stats).
- [x] Update `src/graphics/rhi/README.md` and
      `src/graphics/renderer/Backends/Null/README.md` for the Slice B
      placed-memory contract and Null bookkeeping state.
- [x] Update `src/graphics/rhi/README.md`,
      `src/graphics/vulkan/README.md`, and
      `src/graphics/renderer/Backends/Null/README.md` for Slice C placed
      memory-block alignment, Vulkan binding, and deferred renderer adoption.

## Acceptance criteria
- [ ] Real measured transient memory reduction on the default sandbox
      recipe recorded in this file (before/after bytes).
- [ ] Validation-layer-clean Vulkan smoke cited as actually run.
- [x] Aliasing-off fallback preserved and selectable; CPU gate green.

## Verification

Slice A local verification:

All commands below passed locally on 2026-07-04. `check_root_hygiene`
remains warning-mode and reported existing root entries `ara/` and
`imgui.ini`.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Graphics(RenderGraph|Contract)|TextureUpload' --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^RenderGraphDebugDump\.GoldenSmallRenderPassGraphIncludesAttachmentsAndResourceMaps$' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
```

Operational Vulkan smoke remains owned by the closing adoption slice.

Slice B local verification:

All commands below passed locally on 2026-07-04. The `ci` configure emitted the
existing promoted-Vulkan fallback warning. `check_root_hygiene` remains
warning-mode and reported existing root entries `ara/` and `imgui.ini`.
Clean-workshop manual rows: row 3 pass; rows 4-6 n/a; row 7 pass; row 8 pass;
no follow-up findings.

```bash
rm -rf build/ci
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RHI(ResourceSlotRecycling|PlacedMemoryContract)' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
tools/ci/run_clean_workshop_review.sh . --strict
```

Slice C local verification:

All commands below passed locally on 2026-07-04. The default `ci` configure
emitted the existing promoted-Vulkan fallback warning. `check_root_hygiene`
remains warning-mode and reported existing root entries `ara/` and
`imgui.ini`. Clean-workshop manual rows: row 3 pass; rows 4-6 n/a; row 7 pass;
row 8 pass; no follow-up findings. The `ci-vulkan` coverage below proves the
backend fail-closed/compile contract; the operational GPU/Vulkan smoke and
measured transient-memory reduction remain Slice D.

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RHI(ResourceSlotRecycling|PlacedMemoryContract)|RendererRhiBoundary' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'VulkanFailClosedContract' --timeout 60
rm -rf build/ci
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RHI(ResourceSlotRecycling|PlacedMemoryContract)|RendererRhiBoundary' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
tools/ci/run_clean_workshop_review.sh . --strict
```

## Forbidden changes
- Passing `Vk*` types through RHI/renderer/framegraph public APIs.
- Enabling aliasing by default before the Vulkan smoke is cited.
- Aliasing imported resources.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  placement planner everywhere. Slices A–B close `CPUContracted`; Slice C–D
  close `Operational` with the cited `gpu;vulkan` smoke. If sliced across
  PRs, the closing slice owns the smoke.
