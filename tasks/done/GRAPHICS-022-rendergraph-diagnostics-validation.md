# GRAPHICS-022 — Rendergraph diagnostics and validation

- Status: complete (Slices A/B/C implemented; diagnostic string shim removed by GRAPHICS-027)
- Owner / agent: graphics — `src/graphics/framegraph` + `src/graphics/renderer`
- Branch: `claude/plan-rendering-tasks-ORYwq` (planning); implementation lands on a follow-up branch.
- PR: TBD.
- Follow-up: string shim removal completed in `tasks/done/GRAPHICS-027-remove-rendergraph-diagnostic-shim.md`.

## Goal

Define and implement a deterministic, CPU-testable rendergraph diagnostics
surface that validates pass/resource correctness before Vulkan execution.
Replace the current best-effort thread-local `g_LastCompileDiagnostic` string
with a structured, severity-tagged `RenderGraphValidationResult` so the
behaviors documented in `docs/architecture/rendering-three-pass.md` §
"Validation / Audit Expectations" become enforceable contracts.

## Non-goals

- No new rendering features, shading models, or pass visuals.
- No Vulkan-only validation path as the sole correctness mechanism.
- No replacement of existing framegraph ownership boundaries between runtime
  and graphics.
- No editor/UI diagnostics surface (graph inspector tooling) in this task.
- No live-reload diagnostics (owned by `GRAPHICS-023`).

## Context

The rendering architecture expects explicit graph validation and inspectable
diagnostics, but no backlog task owned the scope until now. The architecture
doc (`docs/architecture/rendering-three-pass.md` lines ~294–303) declares:

- per-pass attachment metadata in introspection output;
- per-resource first/last read and write pass indices;
- audit logging of pass order, resource creation, transitions, and formats;
- warning for `LOAD` without a guaranteed earlier write;
- a `ValidateCompiledGraph()` returning `RenderGraphValidationResult` with
  error/warning severity;
- missing required resources and transient resources without producers as
  validation errors (not warnings);
- imported-resource write policy (`ImportedResourceWritePolicy`) with
  unauthorized writes as errors;
- default policy: only `Present.LDR` may write to the imported `Backbuffer`.

Today, the compiler in `src/graphics/framegraph/Graphics.RenderGraph.Compiler.{cppm,cpp}`
already returns `Core::Expected<CompiledRenderGraph>` and surfaces the first
hard error through a thread-local `g_LastCompileDiagnostic` string. It does
**not** report multiple findings, severity, transient-without-producer,
LOAD-without-writer, or imported-resource write authorization. The existing
debug dump (`BuildRenderGraphDebugDump`) only formats pass order and lifetime
indices and lacks attachment metadata, imported flags, and per-resource
producer/consumer maps.

`Extrinsic.Graphics.FrameRecipe` (`src/graphics/renderer/Graphics.FrameRecipe.cppm`)
already publishes a typed `FrameRecipeIntrospection` with pass and resource
declarations including the imported-write-allowed flag for `Backbuffer`.
The validator should consume `FrameRecipeIntrospection` (when supplied) so it
can correlate compiled-graph passes/resources with the typed recipe contract,
but it must remain usable on a bare `CompiledRenderGraph` (recipe-less tests).

This work is foundational for GRAPHICS-018 final operational promotion, the
upcoming Vulkan smoke-test taxonomy in GRAPHICS-018Q, and downstream tasks
(GRAPHICS-013A/B/C diagnostics, GRAPHICS-024 overlay/present packet authoring).
Owner layer: `graphics/framegraph` for compiler-side validation; thin
re-export from `graphics/renderer` only if needed for recipe-aware policy.

## Required changes

### 1. Public types (CPU-only, in `Extrinsic.Graphics.RenderGraph:Compiler`)

Add to `Graphics.RenderGraph.Compiler.cppm`:

- [x] `enum class RenderGraphValidationSeverity : std::uint8_t { Info = 0, Warning, Error };`
- [x] `enum class RenderGraphValidationCode : std::uint16_t { ... }` covering at
  minimum:
  - [x] `MissingTextureProducer`,
  - [x] `MissingBufferProducer`,
  - [x] `TransientTextureWithoutProducer`,
  - [x] `TransientBufferWithoutProducer`,
  - [x] `LoadWithoutGuaranteedWriter`,
  - [x] `UnauthorizedImportedTextureWrite`,
  - [x] `UnauthorizedImportedBufferWrite`,
  - [x] `BackbufferWrittenByNonFinalizer`,
  - [x] `ImportedTextureFinalStateMismatch`,
  - [x] `RenderPassColorWriteMissing` (already errors today — rewrap),
  - [x] `RenderPassDepthAccessMissing` (already errors today — rewrap),
  - [x] `CycleDetected` (already errors today — rewrap),
  - [x] `InvalidExplicitDependency`,
  - [x] `InvalidTextureAccess`,
  - [x] `InvalidBufferAccess`.
- [x] `struct RenderGraphValidationFinding { RenderGraphValidationSeverity Severity; RenderGraphValidationCode Code; std::string Message; std::uint32_t PassIndex; std::string PassName; std::uint32_t ResourceIndex; bool IsTextureResource; std::string ResourceName; };`
- [x] `struct RenderGraphValidationResult { std::vector<RenderGraphValidationFinding> Findings; bool HasErrors() const; bool HasWarnings() const; std::size_t CountBySeverity(RenderGraphValidationSeverity) const; };`
- [x] `enum class ImportedResourceWritePolicy : std::uint8_t { Disallow = 0, AllowFinalizerOnly, AllowAny };`
- [x] `struct ImportedResourceAuthorization { std::uint32_t ResourceIndex; bool IsTexture; ImportedResourceWritePolicy Policy; std::vector<std::string> AuthorizedWriterPassNames; };`

### 2. New validation entry point

Add to the same module:

```
[[nodiscard]] RenderGraphValidationResult ValidateCompiledGraph(
    const CompiledRenderGraph& compiled,
    std::span<const ImportedResourceAuthorization> authorizations = {});
```

Implementation lives in `Graphics.RenderGraph.Compiler.cpp`:

- [x] Build a per-resource map of (firstWritePass, lastReadPass) using the existing
  `TopologicalOrder` and `PassDeclarations` data, **without** changing the
  compile-time semantics. Producers are passes that write a resource; transient
  resources without any writer in `TopologicalOrder` produce an error finding.
- [x] LOAD-without-writer: detect attachments tagged with `LoadOp::Load` (already
  surfaced via `RHI::RenderPassDesc` color/depth target load ops on
  `RenderPassRecord::RenderPass`) where the corresponding resource has no
  earlier writer in topological order. For imported resources whose
  `InitialState` is a "well-defined" prior-state value (e.g. `Present`), demote
  to `Info`. Otherwise emit `Warning`.
- [x] Imported write authorization: for each resource flagged
  `CompiledRenderGraph::TextureImported[i]` (or `BufferImported[i]`), gather
  the set of passes that write it; if no `ImportedResourceAuthorization` is
  supplied for that resource, fall back to a default policy that only allows
  side-effect writes (`SideEffect == true`), and only writes that include
  `TextureUsage::Present`. Otherwise enforce the supplied policy.
- [x] Backbuffer-only-finalizer: `Backbuffer` is the imported resource whose
  recipe declaration carries `Backbuffer = true`. If the validator is given a
  `FrameRecipeIntrospection` reference (separate convenience overload, see §3),
  cross-check that only the finalizing pass writes the backbuffer.
- [x] Each finding records pass and resource indices/names so test assertions can
  match by code without comparing free-form strings.

### 3. Recipe-aware overload (in `graphics/renderer`)

Add a thin recipe-aware helper in
`src/graphics/renderer/Graphics.FrameRecipe.cppm`:

```
[[nodiscard]] RenderGraphValidationResult ValidateRecipeCompiledGraph(
    const FrameRecipeIntrospection& recipe,
    const CompiledRenderGraph& compiled);
```

This builds the `ImportedResourceAuthorization` list from the recipe (only the
pass with `FinalizesBackbuffer = true` may write resources flagged as
`ImportedWriteAllowed = true`) and forwards to the compiler-side validator.
This keeps the framegraph layer free of recipe knowledge while giving the
recipe path a one-call ergonomic check.

### 4. Compile-side hardening

Adapt `RenderGraphCompiler::Compile`:

- [x] Continue returning `Core::Expected<CompiledRenderGraph>` for hard
  pre-execution errors that prevent producing a graph at all (cycle, invalid
  refs, missing required render-pass usages — same as today). Convert each
  existing diagnostic emission into a typed `RenderGraphValidationFinding`
  stored on a new `CompiledRenderGraph::ValidationFindings` field so callers
  can read structured failure data even when `Compile()` returned an error.
- [x] Preserve the old `g_LastCompileDiagnostic` thread-local for one release as a
  compatibility shim (kept truthful: it mirrors the first error finding's
  message). Schedule its removal in this task's "Forbidden changes" cleanup
  follow-up.

### 5. Deterministic debug dump

Extend `BuildRenderGraphDebugDump(const CompiledRenderGraph&)` in
`Graphics.RenderGraph.Compiler.cpp`:

- [x] Emit per-resource: name (if available), `imported`, `final_state`, `first_write_pass`,
  `last_read_pass`, `producer_count`, `consumer_count`.
- [x] Emit per-pass: `queue`, `side_effect`, color targets with load/store ops,
  depth target with load/store ops, attachment formats from
  `RHI::RenderPassDesc`.
- [x] Format must be stable byte-for-byte across runs given identical input
  (sorted, no pointer addresses, no timestamps). The existing test
  `tests/integration/graphics/Test_RenderGraphPackets.cpp` will pin the new
  format with golden-string assertions; new contract tests in §6 cover the
  validation findings shape directly.

### 6. New tests

Under `tests/contract/graphics/`:

- [x] `Test.RenderGraphValidation.cpp` (label `contract;graphics`):
  - [x] Empty graph → no findings.
  - [x] Pass with `LoadOp::Load` on a transient texture and no earlier writer →
    one `LoadWithoutGuaranteedWriter` warning.
  - [x] Pass declaring only a read on a transient buffer (no writer anywhere) →
    one `TransientBufferWithoutProducer` error.
  - [x] Two passes both writing imported `Backbuffer`, only one with side-effect
    finalizer → one `BackbufferWrittenByNonFinalizer` error citing the
    offending pass index.
  - [x] Authorized writer list: imported texture written by listed pass → no
    finding; written by unlisted pass → `UnauthorizedImportedTextureWrite`.
  - [x] Cycle case is preserved (still `CycleDetected` error in
    `RenderGraphValidationResult` and still `Core::Err(InvalidState)` from
    `Compile`).
  - [x] `RenderGraphValidationFinding` ordering is stable (sorted by severity,
    then code, then pass index, then resource index).

Under `tests/unit/graphics/`:

- [x] `Test.RenderGraphDebugDump.cpp` (label `unit;graphics`):
  - [x] Golden-string test for a small recipe-driven graph (depth + scene color +
    backbuffer present). Confirms the new attachment/load-op fields and
    sorted resource ordering.

Under `tests/contract/graphics/`:

- [x] Augment `Test.FrameRecipeContract.cpp` with one test that runs
  `ValidateRecipeCompiledGraph` against the canonical default recipe and
  asserts zero errors and zero warnings.

### 7. CMake / module surface

- [x] Add no new modules. Both new symbols live in existing `Extrinsic.Graphics.RenderGraph`
  (compiler partition) and `Extrinsic.Graphics.FrameRecipe`. Update
  `src/graphics/framegraph/CMakeLists.txt` only if new `.cpp` files are
  introduced (unlikely; the new code can extend `Graphics.RenderGraph.Compiler.cpp`).
- [x] Update `src/graphics/renderer/CMakeLists.txt` only if a new translation
  unit is added for the recipe-aware overload. Prefer keeping the helper
  inline in `Graphics.FrameRecipe.cppm` to avoid CMake churn.
- [x] Re-run module inventory generation (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`)
  if any new exported symbols change the surface.

## Tests

- [x] Build target: `IntrinsicTests`.
- [x] Default CPU gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] New labels:
  - [x] `Test.RenderGraphValidation.cpp` → `contract;graphics`.
  - [x] `Test.RenderGraphDebugDump.cpp` → `unit;graphics`.
- [x] Existing `Test_RenderGraphPackets.cpp` and `Test.FrameRecipeContract.cpp`
  must continue to pass; update their golden strings only when the new
  format requires it, and document the change in the commit message.
- [x] Vulkan/GPU execution remains optional and must not be required to evaluate
  any new finding code path.

## Docs

- [x] Update `docs/architecture/rendering-three-pass.md` § "Validation / Audit
  Expectations" to reference the new `RenderGraphValidationResult`,
  `RenderGraphValidationCode`, and `ImportedResourceWritePolicy` types and
  the recipe-aware helper. Replace the prose mention of
  `ValidateCompiledGraph()` with the canonical signature.
- [x] Update `src/graphics/renderer/README.md` § "Renderer and graph" with a
  paragraph describing the diagnostics surface and the recipe-aware overload.
- [x] Optional: cross-link this task from
  `docs/migration/nonlegacy-parity-matrix.md` once landed (rendergraph
  diagnostics row).

## Acceptance criteria

- [x] `RenderGraphValidationResult` is the single structured surface for graph
  diagnostics; the thread-local `g_LastCompileDiagnostic` string is either
  removed or downgraded to a documented compatibility shim with a removal
  follow-up.
- [x] All new validation codes are exercised by at least one CPU contract test
  with a deterministic finding match (severity + code + pass/resource index).
- [x] The default CPU CTest gate stays green; no Vulkan/GPU label is required by
  any new test.
- [x] The debug dump is byte-for-byte deterministic for fixed input and includes
  attachment metadata, load/store ops, imported flags, and producer/consumer
  counts.
- [x] Imported-backbuffer write policy is enforced or explicitly diagnosed; the
  default recipe does not produce any finding.
- [x] Architecture and renderer-README docs reference the implemented types
  rather than aspirational ones.

## Progress

- 2026-05-06: Slice A landed the exported validation types,
  `ValidateCompiledGraph(...)`, minimal compiled metadata for recipe-less
  validation, and `tests/contract/graphics/Test.RenderGraphValidation.cpp`.
  Focused `RenderGraphValidation` CTest coverage passes. Remaining work stays
  scoped to Slice B/C below.
- 2026-05-06: Slice B expanded `BuildRenderGraphDebugDump` with stable pass
  queue/side-effect, render-pass attachment, and per-resource
  producer/consumer metadata. Added
  `tests/unit/graphics/Test.RenderGraphDebugDump.cpp` golden coverage and
  refreshed `docs/api/generated/module_inventory.md`. Remaining work stays
  scoped to Slice C below.
- 2026-05-06: Slice C sub-slice added
  `ValidateRecipeCompiledGraph(...)`, frame-recipe contract coverage proving the
  canonical default recipe validates cleanly, architecture/renderer README docs,
  and refreshed `docs/api/generated/module_inventory.md`. Remaining Slice C
  work is compile-side typed findings plus the `g_LastCompileDiagnostic`
  removal follow-up.
- 2026-05-06: Slice C completed compile-side structured findings via
  `CompiledRenderGraph::ValidationFindings`,
  `RenderGraphCompiler::GetLastCompileValidationResult()`, and
  `RenderGraph::GetLastCompileValidationResult()`. Hard compile failures now
  mirror structured findings while `GetLastCompileDiagnostic()` remains a
  documented compatibility shim. Added CPU contract coverage for the remaining
  validation codes and created
  `tasks/done/GRAPHICS-027-remove-rendergraph-diagnostic-shim.md`
  as the removal follow-up (completed 2026-05-07).

## Verification

```bash
# 1. Task and doc-link policy (planning + every implementation slice).
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# 2. Build and run the default CPU correctness gate.
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# 3. After module surface changes, refresh the module inventory.
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md

# 4. Targeted runs while iterating.
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|RenderGraphDebugDump|FrameRecipeContract' --timeout 60
```

## Forbidden changes

- No renderer feature implementation unrelated to diagnostics/validation.
- No Vulkan-only mandatory tests.
- No shader/pass behavior changes for visual output.
- No copy from `src/legacy` graphics modules.
- No expansion of the framegraph layer surface with recipe-specific knowledge
  beyond the documented `FrameRecipeIntrospection` consumer in §3.
- No silent removal of `g_LastCompileDiagnostic` without a documented
  removal follow-up; either keep it as a documented shim or land its removal
  alongside this task's commits.

## Implementation slice plan

Land in three small PRs to keep mechanical and semantic edits separated:

1. **Slice A — complete.** Types + recipe-less validator (no behavior change to
   `Compile`). Add `RenderGraphValidationFinding`/`Result`/`Code`,
   `ImportedResourceAuthorization`, and `ValidateCompiledGraph(...)`. Add
   `Test.RenderGraphValidation.cpp` covering the codes that can be exercised
   on a hand-built `CompiledRenderGraph` fixture.
2. **Slice B — complete.** Debug dump expansion + golden tests. Extend
   `BuildRenderGraphDebugDump` and add `Test.RenderGraphDebugDump.cpp`. Pin
   the new format with golden assertions. Update
   `Test_RenderGraphPackets.cpp` golden strings if needed.
3. **Slice C — complete.** Recipe-aware helper, compile-side findings, docs,
   tests, and the `g_LastCompileDiagnostic` compatibility-shim removal follow-up
   are implemented/tracked.

Each slice should run all four verification commands above before merge.
