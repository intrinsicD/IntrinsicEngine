---
id: BUG-255
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and the Vulkan validation runs
contract_schema: 1
contracts: []
contract_review: no catalog contract covers render-graph synchronization; the barrier rules live in the framegraph compiler and docs/architecture/rendering-three-pass.md
---
# BUG-255 — Default-recipe frames have synchronization hazards on SceneDepth

## Goal
- Frames of the default recipe, including surfaces, edge lines, points and picking, are free of
  Vulkan synchronization hazards: Khronos synchronization validation reports none in the
  validation smokes, so their correctness no longer rests on one driver serializing passes.

## Context
- Opened 2026-10-26 from BUG-254 slice C: its plan and code reviews (Codex) found that the render
  graph emits a texture barrier only when the state changes
  (`Graphics.RenderGraph.Compiler.cpp:2289`), that `DepthWrite` maps to write access only
  (`ToMemoryAccess`, `Graphics.Renderer.cpp:913`), and that the backend begins every rendering
  with `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (`Backends.Vulkan.CommandContext.cpp:305`) while a
  `DepthRead` pass's depth was transitioned to `DEPTH_STENCIL_READ_ONLY_OPTIMAL`
  (`Graphics.Renderer.cpp:892`).
- Spike 2026-10-26 (host `hochvogel`, NVIDIA GeForce RTX 4090, driver 580.159.04, `b40b6a381`,
  `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`, duplicate
  message limit off):
  - `DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation` (reference
    triangle) reports 36 hazards and fails:
    - 12 `SYNC-HAZARD-WRITE-AFTER-WRITE`: a layout transition of `SceneDepth` after a render pass
      stored depth (prior `LATE_FRAGMENT_TESTS` write from `vkCmdEndRendering`, no write barrier).
      Passes that only read depth (`DepthRead`) still store it with `StoreOp::Store`, which is a
      write, and the next barrier's source access is read-only.
    - 8 `SYNC-HAZARD-READ-AFTER-WRITE`: `vkCmdBeginRendering` loads `SceneDepth` after a transition
      into `DepthWrite` whose destination access is write-only.
    - 8 `SYNC-HAZARD-WRITE-AFTER-WRITE` in `vkCmdExecuteCommands` (parallel recording): a depth
      store after a transition made visible to reads only - the first cause again.
    - 8 `SYNC-HAZARD-WRITE-AFTER-READ` at `vkQueueSubmit`: the swapchain image's first layout
      transition (source stage `NONE` for `Undefined`) is not chained to the acquire semaphore,
      which the single-submit path waits at `COLOR_ATTACHMENT_OUTPUT`
      (`Backends.Vulkan.Device.cpp:3155`); the batched path waits at `ALL_COMMANDS` (line 3033).
  - `RuntimeSandboxAcceptanceGpuSmoke.DrawnLinesOccludeWhatTheClickPassesOver` (surfaces, lines,
    points, picking; validation forced on locally, not committed) reports 21 + 21 hazards of the
    first two classes on `SceneDepth`, nothing else, and no core-validation error.
- Same-state writes: `LinePass` → `PointPass` keep `SceneDepth` in `DepthWrite` and
  `SceneColorHDR` in `ColorAttachmentReadWrite`, so no barrier separates the two render passes.
  The Vulkan synchronization examples require one between render pass instances that reuse an
  attachment, but synchronization validation reported no hazard for these pairs in either run
  (reason not established).
- The depth-tested transient debug sphere pipeline writes depth (`Graphics.Renderer.cpp:5643`)
  inside `TransientDebugSurfacePass`, which declares `DepthRead`.
- The validation smokes run core validation only; `RuntimeSandboxAcceptanceGpuSmoke` runs without
  validation (`EnableValidation = false` in its bootstrap).

## Operator questions (verbatim, 2026-10-26)
1. (Asked whether to take on the missing render-graph barriers as their own bug, plan with review
   first) "Ja, leg die fehlenden Barrieren im Render-Graph als eigenen Bug an und mach erst Plan
   mit Review."

Goal item served: Framework24 W5 "Inspect rendering and properties" (the BUG-254 picking and line
depth rely on these depth dependencies); this is the shortest path because synchronization
validation already names every hazard and its cause, and each cause is one rule in the graph,
its executor or the backend.

## Slice plan
Scope is what synchronization validation reports in the two scenes above, plus the same-state rule
(the Vulkan synchronization examples require it although the validator is silent on those pairs);
a hazard class found beyond them during implementation is reported to the operator before it is
fixed (drift budget). Synchronization across the graph boundary (imported initial and final
states) and across queues (the existing timeline dependencies and ownership transfers) stays as it
is. Slices A and B land together: the zero-hazard check needs both.

- **Slice A — depth synchronization.**
  1. Read-only depth attachments do not write. The compiler marks a pass's depth attachment
     `DepthReadOnly` when the pass's access to that bound depth texture is `DepthRead` (other
     `DepthRead`s, such as `SurfacePass`'s shadow-atlas read, do not count) and rejects a read-only
     depth attachment with `LoadOp::Clear`; the executor (`BuildActiveRenderPassDesc`,
     `Graphics.Renderer.cpp`) passes it on as `RHI::DepthAttachment::ReadOnly`; the Vulkan backend
     then begins rendering with `DEPTH_STENCIL_READ_ONLY_OPTIMAL` and `VK_ATTACHMENT_STORE_OP_NONE`
     (core in Vulkan 1.3; `DONT_CARE` would not preserve the depth). This removes the store write and
     the layout mismatch for `SurfacePass` (prepass on), the outline-only `PickingPass`, the overlays
     and the selection outline (all with depth writes off), without touching the recipe's per-pass
     descriptions.
  2. `TransientDebugSurfacePass` declares `Write(SceneDepth, DepthWrite)` in the graph builder and
     lists `SceneDepth` among its writes in the introspection: its depth-tested spheres write depth
     (sphere occlusion), which a read-only attachment would forbid.
  3. `DepthWrite` access is `DepthStencilRead | DepthStencilWrite`: a pass in that state loads and
     tests depth as well as writing it (the stage mask already covers early and late tests).
  4. Same-state write barriers: the compiler's existing per-resource last-accessor tracking emits a
     barrier (same layout) when a pass accesses a resource in an unchanged write state
     (`ColorAttachmentWrite`, `ColorAttachmentReadWrite`, `DepthWrite`, `ShaderWrite`,
     `TransferDst`; buffers `ShaderWrite`, `TransferDst`) that another pass of this graph accessed
     last on the same queue. Read-only repeats and repeated accesses inside one pass emit none; the
     exclusive ownership-transfer branch stays as it is.
  5. Tests, red first:
     - compiler (`IntrinsicGraphicsContractCpuTests`), table-driven over the write states: two
       passes writing one texture or buffer get a same-state barrier before the second pass (red
       before step 4); two readers and a pass with two accesses get none (guards, already green);
       the ownership-transfer and concurrent cross-queue cases keep their current packets; a
       `DepthRead` bound depth attachment compiles read-only, a `DepthWrite` one does not, and a
       read-only clear is rejected;
     - recipe (`FrameRecipeContract`): `PointPass` gets the `DepthWrite` → `DepthWrite` barrier on
       `SceneDepth` after `LinePass`; `TransientDebugSurfacePass` writes `SceneDepth` in the builder
       and the introspection; the depth attachments of `SurfacePass` (prepass on), the outline-only
       `PickingPass` and `SelectionOutlinePass` are read-only;
     - renderer: `MockRHI` already records barrier batches; it also records the `BeginRenderPass`
       depth attachment, so a read-only pass begins with `ReadOnly`, and a barrier into
       `DepthWrite` carries read and write access.
- **Slice B — swapchain acquire.** The single-submit path (`Backends.Vulkan.Device.cpp:3155`) waits
  for the acquire semaphore at `ALL_COMMANDS`, like the batched path, so the backbuffer's first
  transition is ordered after the acquire.
- **`gpu;vulkan`, after A and B:** synchronization validation on (set through `VK_LAYER_ENABLES` by
  each smoke before bootstrap; ctest runs each case in its own process) in the two graphics
  `...WithValidation` smokes, in a sandbox case on the BUG-254 line/point/pick scene run with
  validation, and in a transient-debug case with a depth-tested sphere under validation; each
  asserts the engine's validation error count stays 0 (today: 36 and 42 hazards). Mutations against
  the finished fix, each predicted to bring back its class: step 1 undone (transition
  `WRITE-AFTER-WRITE` and the secondary-command store hazard); step 3 undone (load
  `READ-AFTER-WRITE`); step 2 undone with step 1 in place (core validation: the sphere writes a
  read-only attachment); slice B undone (`WRITE-AFTER-READ` at `vkQueueSubmit`). Step 4 has no GPU
  signal; its CPU tests carry it.
- Docs: the barrier rules, their boundaries and the read-only depth attachment in
  `docs/architecture/rendering-three-pass.md`; the BUG-254 note's "not covered" line points here.

## Acceptance criteria
- [x] Synchronization validation reports no hazard in the graphics validation smokes and in the
      line/point/pick sandbox scene, with the runs cited (host GPU, driver) and each fix of slice A
      steps 1-3 and slice B mutated.
- [x] Read-only depth passes neither store depth nor render in a layout other than the one the
      graph transitioned to; same-state writes between passes get a barrier (CPU tests).

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j 6
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(RenderGraph[A-Za-z]*|GraphicsOwnershipTransferBarriers|OwnershipTransferBarriers|FrameRecipeContract|RendererFrameLifecycle)\.'
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 6
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 300 \
  -R '^(DefaultRecipeSurfaceGpuSmoke\.[A-Za-z]*WithValidation|TransientDebugSurfaceGpuSmoke\.DepthTestedSphereWritesDepthWithoutValidationErrors|RuntimeSandboxAcceptanceGpuSmoke\.DrawnLinesOccludeWhatTheClickPassesOver)$'
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-26: Opened from BUG-254 slice C; spike above (synchronization validation, not committed).
- 2026-10-26: Plan review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a1756d-6f38-79a3-892c-f1b025fafef3`;
  recursion protection instruction-only), on the plan with sha256 `4746f87d…`. Verdict verbatim
  "revise" ("Keep A1–A4, the acquire fix, and documentation"; "The scope is appropriate
  **including the same-state rule**"). Blocking, folded in: zero hazards needs slice B too (both
  land before the GPU check); step 2 also updates the introspection; step 4 states its boundaries
  (ownership transfers, cross-queue timeline dependencies and the graph boundary unchanged) and
  is tested table-driven with the ownership suites in the verification. Optional, followed: the
  read-only flag comes from the bound depth texture's access and a read-only clear is rejected;
  red/green predictions per case; an explicit depth-tested sphere fixture under validation; named
  GPU cases instead of whole suites.
- 2026-10-26: Slices A and B implemented.
  - Compiler (`Graphics.RenderGraph.Compiler.cpp`): `IsWriteState` for texture and buffer barrier
    states; the plain-barrier branch also fires for an unchanged write state last accessed by
    another pass of the same queue (ownership-transfer branch unchanged). A bound depth attachment
    whose pass has no `DepthWrite` access to it compiles `DepthReadOnly`; `DepthRead` with
    `LoadOp::Clear` is rejected (`RenderGraphValidationCode::RenderPassReadOnlyDepthClear`, appended).
  - Executor and RHI: `RHI::DepthAttachment::ReadOnly` (from `DepthReadOnly`); `ToMemoryAccess(DepthWrite)`
    is `DepthStencilRead | DepthStencilWrite`. Vulkan: a read-only depth begins rendering in
    `DEPTH_STENCIL_READ_ONLY_OPTIMAL` with `VK_ATTACHMENT_STORE_OP_NONE`; the single-submit path waits
    for the acquire at `ALL_COMMANDS`. Recipe: `TransientDebugSurfacePass` writes `SceneDepth` in the
    builder and the introspection.
  - CPU tests, red before the change (5) and green after: `RenderGraphValidation.SameStateWritesInTwoPassesGetABarrier`
    (four texture usages, loaded colour, two buffer usages), `.DepthReadAttachmentCompilesReadOnly`
    (read-only reader, writable writers, read-only clear rejected), `FrameRecipeContract.LineAndPointPassesWriteTheSceneDepthTheyLoad`
    (`PointPass` same-state barrier), `.DepthReadingPassesBindTheSceneDepthReadOnly`,
    `RendererFrameLifecycle.DepthReadingPassesBeginReadOnlyAndDepthWritesSeeTheirReads` (`MockRHI`
    now records begun depth attachments); the guard `.RepeatedReadsAndCrossQueueWritesGetNoSameStateBarrier`
    was green before, as predicted. `IntrinsicGraphicsContractCpuTests` 586/586,
    `IntrinsicGraphicsIntegrationCpuTests` 75/75, `IntrinsicGraphicsContractTests` 71/71.
  - `gpu;vulkan` with synchronization validation (`OperationalCounterStability::EnableSynchronizationValidation`,
    set before bootstrap): `DefaultRecipeSurfaceGpuSmoke.ParallelRecording...WithValidation` (both),
    the new `TransientDebugSurfaceGpuSmoke.DepthTestedSphereWritesDepthWithoutValidationErrors` and
    `RuntimeSandboxAcceptanceGpuSmoke.DrawnLinesOccludeWhatTheClickPassesOver` (now asserts no
    validation error) report no hazard (spike: 36 and 42).
  - Mutations against the finished fix (host `hochvogel`, RTX 4090, driver 580.159.04, X11 `:1`):
    read-only depth undone - all four fail with `SYNC-HAZARD-WRITE-AFTER-WRITE` on `SceneDepth`
    (12 transitions + 4 secondary-command stores per graphics case, 21 sandbox, 5 transient), as
    predicted; `DepthWrite` read access undone - all four fail with `SYNC-HAZARD-READ-AFTER-WRITE`
    at the depth load (8, 8, 21, 5), as predicted; acquire wait undone - the graphics case
    fails with 8 `SYNC-HAZARD-WRITE-AFTER-READ` at `vkQueueSubmit` as predicted, but the async-compute
    case passes (prediction wrong: it submits through the batched path, which waits at `ALL_COMMANDS`);
    same-state barriers undone - all pass, as predicted (no GPU signal; the CPU tests carry it);
    transient pass back to `Read(DepthRead)` - all pass, against the prediction: a local probe showed
    the depth-tested sphere does draw with depth writes inside a render pass begun read-only, but
    this validation layer (1.3.296, which lists `VUID-vkCmdDraw-None-06886`) reported nothing; step
    2 rests on the CPU recipe test and the Vulkan rule. The first version of the sphere fixture drew
    a flat sprite (`Sphere` unset, no camera), which writes no depth; fixed before this run.
  - Full runs on the final sources (before formatting-only edits to the compiler and two CPU test
    files, after which the graphics contract suites passed again, 213/213): default CPU gate
    (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) 5898/5898, run because two widely imported
    module interfaces changed (`RHI.CommandContext.cppm`, `Graphics.RenderGraph.Compiler.cppm`);
    every `gpu` test in `build/ci-vulkan` 166/166 (`ExtrinsicSandbox.VulkanShutdownLsanContract`
    needs `VK_DRIVER_FILES` naming one ICD and passed with the NVIDIA one); the four
    synchronization-validation cases passed three times.
  - jscpd: no new clone in production code; the compiler tests repeat their two-pass setup for
    textures and buffers (12 and 6 lines).
  - ponytail-review (author, staged diff): no finding; the texture and buffer same-state conditions
    are near-copies kept beside their existing per-type branches.
- 2026-10-26: Code review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a175c7-9f7c-7c03-846a-6592eb9494ce`;
  recursion protection instruction-only), on the staged diff sha256 `3f182512…`. Verdict verbatim
  "approve" ("No blocking production correctness finding"; the mutations "provide **no GPU
  discrimination** for same-state barriers or the transient declaration; those rely on CPU coverage
  and Vulkan requirements"). Optional findings: the sandbox case now asserts zero validation errors
  only when the validation layer and debug-utils are active (the three click smokes passed again);
  the sphere fixture's `PointRecordsRecorded` would also count the flat-sprite fallback - not
  changed: no existing counter tells the sphere path apart, and the local probe above showed the
  sphere path runs on this host.

## Completion — 2026-10-26
Completed 2026-10-26 on `main`. Commit: note `61624ee0d`, fix `08ce3f169`. Maturity: Operational on host
`hochvogel` (NVIDIA GeForce RTX 4090, driver 580.159.04, Khronos validation layer 1.3.296):
synchronization validation reports no hazard in the four validation cases. The default CPU gate
(5898/5898) and every `gpu` test (166/166) ran before the formatting-only edits and the
`validationActive` guard; after them the touched graphics contract suites (213/213) and the three
click smokes passed again. Hosted `ci-docs` passed on `08ce3f169`; `ci-linux-clang` was still
running when this was written.

Reviewer decisions: final retirement review (Claude coordinator → Codex reviewer, fresh session,
`codex exec`, requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread
`01a175db-6f74-7951-b39b-38acd5716b39`; recursion protection instruction-only), on `08ce3f169`
plus the retirement drafts. Verdict verbatim "approve retirement". "met — “Synchronization
validation reports no hazard”" ("The transient-depth mutation did **not** fail; same-state
barriers also lack GPU discrimination. This limits the evidence but does not block wording
requiring mutations to be performed."); "met — “Read-only depth passes neither store depth”".
It approved the post-review `validationActive` change ("a passing result without validation must
not count as synchronization evidence"). Both boxes are ticked from these decisions; its optional
findings are applied in the retirement commit.

Limits:
- Synchronization validation (layer 1.3.296) gives no signal for the same-state barriers or for
  depth writes into a read-only depth attachment; those rules rest on CPU barrier and recipe tests
  and the Vulkan requirements.
- Without an active validation layer the sandbox line-depth case still runs its clicks and pixels
  but is no synchronization evidence.
- The graph boundary (imported initial and final states) and cross-queue synchronization were
  left as they were.
- One host, driver and validation-layer version.

Evidence: no files under `tasks/evidence/BUG-255/`; nothing to delete.
