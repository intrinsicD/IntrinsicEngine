---
id: GRAPHICS-120
theme: B
depends_on: []
---
# GRAPHICS-120 — Framegraph compiler/executor efficiency and hygiene polish

## Status
- In progress on local `main`; PR not opened.
- Owner/agent: Codex.
- Current slices: Slice A, Slice B, Slice C, Slice D1, and Slice D2a
  completed locally.
  `TextureUsage::ColorAttachmentRead` now uses a read-only barrier state,
  transient texture memory estimates are pinned to RHI block-compressed storage
  sizing, and compiler validation diagnostics flow through explicit out-params
  instead of `thread_local` state. Barrier emission now uses shared sorted-packet
  range lookup plus one-time bounds validation in the executor and renderer
  multi-queue path, with PR-fast smoke benchmark evidence. Compiler barrier
  packet insertion now uses a pass/stage index, and duplicate typed pass-id
  validation uses a sorted scan that preserves earliest-duplicate diagnostics.
- Next implementation step: continue with Slice D2b (persistent compiler scratch
  and allocation-counter evidence).

## Goal
- Remove the per-compile/per-execute waste and small contract hazards inside
  `src/graphics/framegraph/`: allocation churn, quadratic scans, the
  `thread_local` validation side channel, the read-treated-as-write barrier
  state, and the duplicated/incorrect format-size table.

## Non-goals
- Compile caching (`GRAPHICS-117`), memory aliasing (`GRAPHICS-118`),
  parallel recording (`GRAPHICS-119`).
- No behavioral changes to compilation output except where a finding is a
  defect (the two hygiene items below, each pinned by a test).

## Context
- Owner/layer: `graphics/framegraph` (+ a small RHI format helper).
- Items (origin:
  `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R18):
  - Per-compile allocation churn: ~35 vectors + an `unordered_set` per
    `Compile`, every pass/resource name copied into fresh `std::string`s,
    per-`RenderPassRecord` access vectors reallocated on every rebuild
    (`Graphics.RenderGraph.Compiler.cpp:1363-1392, 1421, 2045-2079`;
    `Graphics.RenderGraph.cpp:712-714`); executor allocates
    `passNameByIndex`, queue wait/signal vectors, and two vectors per
    barrier packet per frame.
  - Quadratic scans: `emitBarriersForPass` rescans all `BarrierPackets`
    per pass × stage (`Graphics.RenderGraph.Executor.cpp:39-71`, duplicated
    in `Graphics.Renderer.cpp:2811-2845`); `FindOrCreateBarrierPacket`
    linear-scans on every insertion (`Compiler.cpp:816-833`); packets are
    already sorted (`SortBarrierPackets`, `Compiler.cpp:847`) so emission
    can be a linear cursor walk. Also O(n²) `ValidateUniquePassIds`
    (`Compiler.cpp:402-430`).
  - Hygiene: `thread_local RenderGraphValidationResult
    g_LastCompileValidationResult` global compile state and a
    misleadingly-`const` `Compile()` that mutates `m_Impl`
    (`Compiler.cpp:32`; `Graphics.RenderGraph.cpp:358, 442`).
  - Defect: `TextureUsage::ColorAttachmentRead` maps to
    `TextureBarrierState::ColorAttachmentWrite` (`Compiler.cpp:38-39`) —
    read-only color access over-synchronizes as a write state.
  - Defect target: framegraph transient texture byte sizing must stay routed
    through `RHI::EstimateTextureStorageBytes`; a local bytes-per-pixel table
    would overestimate BC-compressed formats 4–8×. A stale recipe reference
    also lives in framegraph doc text (`Compiler.cppm:98-101`).

## Slice plan
- **Slice A (this slice).** Add a read-only color attachment barrier state,
  map `TextureUsage::ColorAttachmentRead` to it, lower it to
  `ColorAttachment` layout + `ColorAttachmentRead` access in the renderer, and
  pin consecutive read-only color attachment accesses with a CPU contract
  regression. Defers compiler scratch reuse, linear barrier emission,
  validation-result plumbing, format sizing, and benchmark evidence to later
  slices.
- **Slice B.** Confirm framegraph transient format sizing uses the RHI format
  helper and pin BC block-compressed estimates.
- **Slice C.** Replace the `thread_local` validation side channel with an
  explicit out-param path, make stateful graph compilation non-`const`, and
  update docs.
- **Slice D1.** Route executor and renderer multi-queue barrier emission through
  a shared indexed range lookup over sorted packets, add one-time packet bounds
  validation, and record PR-fast smoke benchmark evidence.
- **Slice D2a.** Replace compiler barrier packet insertion scans with an
  indexed pass/stage builder, replace duplicate pass-id O(n^2) validation with
  sorted duplicate detection, and record PR-fast smoke benchmark evidence.
- **Slice D2b.** Tackle persistent compiler scratch and remaining allocation
  churn with allocator/counter evidence.

## Required changes
- [ ] Persistent compiler scratch: reuse compile-scoped containers across
      compiles (member scratch or arena); intern names as
      `string_view`s into graph-owned storage; keep `RenderPassRecord`
      access vectors' capacity across `Reset()`.
- [x] Slice D1: shared indexed barrier emission over sorted packet ranges in
      both the executor and renderer multi-queue path; one-time packet bounds
      validation preserves fail-closed behavior.
- [x] Slice D2a: make compiler packet insertion indexed by pass/stage and make
      `ValidateUniquePassIds` sort-based while preserving deterministic
      diagnostics.
- [ ] Slice D2b: persistent compiler scratch; finish compile/emit allocation
      reduction with counter evidence.
- [x] Slice C: replace the `thread_local` validation side channel: return the
      validation result in the `Compile()` `Expected` payload (or a caller
      out-param); make `Compile` non-`const` or move its mutations out.
- [x] Slice A: map `ColorAttachmentRead` to a read barrier state with read-read
      transition skipping; pin with a barrier-plan test.
- [x] Slice B: pin transient sizing to the RHI helper with correct
      block-compressed math; verify no local table remains; scrub the stale
      recipe reference from framegraph comments.

## Tests
- [ ] Golden: compiled output (edges, order, barriers) unchanged for a
      fixture graph across the churn/scan changes.
- [x] Slice A contract: `ColorAttachmentRead` no longer emits a write-state
      transition between consecutive readers.
- [x] Slice B contract: BC-format transient estimate matches RHI block math.
- [x] Slice C contract: static compiler diagnostics publish through explicit
      `RenderGraphValidationResult` out-params on success and failure.
- [x] Existing framegraph suites stay green.
- [x] Slice D1 PR-fast benchmark: barrier emission CPU traversal before/after on
      a pass-heavy synthetic graph.
- [x] Slice D2a PR-fast benchmark: duplicate pass-id validation and compiler
      barrier packet insertion baseline/probe on synthetic compiler work.
- [ ] Slice D2b PR-fast benchmark: compile + allocation counter evidence on a
      pass-heavy synthetic graph.

## Docs
- [x] Slice A: update `src/graphics/framegraph/README.md` for the read-only
      color attachment barrier state.
- [x] Slice B: document `RHI::EstimateTextureStorageBytes` as the framegraph
      transient texture sizing source.
- [x] Slice C: document explicit compile validation result plumbing and
      non-`const` stateful graph compilation.
- [x] Slice D1: document sorted barrier packet traversal and indexed range
      lookup contract.
- [x] Slice D2a: document compiler indexing smoke benchmark scope.

## Acceptance criteria
- [x] Zero `thread_local` compile state; validation results flow through
      explicit out-param paths.
- [x] Barrier-emission and compiler-indexing benchmark evidence recorded with
      baseline comparison.
- [ ] Steady-state per-frame allocations in
      compile/emit measurably reduced (counter or allocator probe).
- [x] Both defect fixes pinned by tests; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

Slice A verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation.ColorAttachmentReadUsesReadStateAndSkipsConsecutiveReadBarrier' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|FrameRecipeContract.DefaultRecipeBarriersRespectTextureUsageCapabilities|FrameRecipeContract.DependencyDrivenDefaultRecipeKeepsBarrierPacketsTopological' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|FrameRecipeContract|RendererFrameLifecycle|OwnershipTransferBarriers|CrossQueueTimeline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice B verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation.TransientMemoryEstimateUsesRhiBlockCompressedStorageSize' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|TextureUpload' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice C verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|CrossQueueTimeline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R '^GraphicsRenderGraph.DependencyCycleReportsPassNamesInDiagnostic$' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|CrossQueueTimeline|FrameRecipeContract|RendererFrameLifecycle|PostProcessChainContract|DebugViewContract|ImGuiPresentContract|OwnershipTransferBarriers|QueueAffinity' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice D1 verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target ExtrinsicGraphicsRenderGraph
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
build/ci/bin/IntrinsicBenchmarkSmoke build/ci/benchmark-graphics120
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-graphics120 --strict
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'OwnershipTransferBarriers|RendererFrameLifecycle|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke|RenderGraphValidation|CrossQueueTimeline|FrameRecipeContract|RendererFrameLifecycle|OwnershipTransferBarriers|QueueAffinity' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice D1 benchmark result (`rendering.framegraph_barrier_emission.smoke`,
`cpu_reference`, `builtin.synthetic_framegraph_barriers.256_passes`):
`legacy_full_scan_ms=2.252798`, `indexed_range_ms=1.806836`,
`legacy_packet_comparisons=175959`, `indexed_range_packet_visits=343`,
`quality_error_l2=0.0`.

Slice D2a verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target ExtrinsicGraphicsRenderGraph
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
build/ci/bin/IntrinsicBenchmarkSmoke build/ci/benchmark-graphics120-d2
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-graphics120-d2 --strict
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract.DuplicateTypedPassIds|IntrinsicBenchmarkSmoke|RenderGraphValidation|OwnershipTransferBarriers|CrossQueueTimeline|FrameRecipeContract.DependencyDrivenDefaultRecipeKeepsBarrierPacketsTopological' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke|RenderGraphValidation|CrossQueueTimeline|FrameRecipeContract|RendererFrameLifecycle|OwnershipTransferBarriers|QueueAffinity' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

Slice D2a benchmark result
(`rendering.framegraph_compiler_indexing.smoke`, `cpu_reference`,
`builtin.synthetic_framegraph_compiler_indexing.512_passes`):
`legacy_scan_ms=18.246658`, `indexed_ms=6.443927`,
`legacy_pass_id_comparisons=130816`, `indexed_pass_id_comparisons=511`,
`legacy_packet_comparisons=433998`, `indexed_packet_lookups=1270`,
`quality_error_l2=0.0`.

## Forbidden changes
- Changing compiled-graph semantics beyond the two pinned defect fixes.
- Perf claims without benchmark numbers.
- Introducing renderer/recipe knowledge into the framegraph module.

## Maturity
- Target: `CPUContracted` for the two defect fixes and hygiene refactors, with
  PR-fast benchmark evidence before the task retires; no `Operational`
  follow-up is owed because this framegraph/compiler slice is backend-neutral
  and lowers through existing renderer lifecycle coverage.
- Slice A closes only the color-attachment-read defect at `CPUContracted`; no
  performance claim is made in this slice.
- Slice B closes the block-compressed transient-estimate defect at
  `CPUContracted`; no performance claim is made in this slice.
- Slice C closes the compile-validation side-channel hygiene item at
  `CPUContracted`; no performance claim is made in this slice.
- Slice D1 closes the duplicated barrier-emission scan item at `CPUContracted`
  with a PR-fast smoke benchmark baseline/probe; persistent compiler scratch and
  allocation-counter evidence remain in Slice D2.
- Slice D2a closes the compiler packet-insertion and duplicate-pass-id scan
  items at `CPUContracted` with PR-fast synthetic compiler indexing evidence;
  persistent compiler scratch and allocation-counter evidence remain in D2b.
