# GRAPHICS-026 completion prompt

Work in `/home/alex/Documents/IntrinsicEngine` using the repository’s agentic workflow.

Start by reading `/AGENTS.md`; it is authoritative. Then read only the relevant `docs/agent/*` files according to the routing table in `AGENTS.md`:
- Read `docs/agent/task-format.md` before creating, promoting, retiring, or materially updating task files.
- Read `docs/agent/review-checklist.md` before reporting completion.
- Read `docs/agent/architecture-review-checklist.md` when changing dependency boundaries, module ownership, source layout, runtime wiring, renderer/RHI/Vulkan behavior, or architecture docs.
- Read `docs/agent/docs-sync-policy.md` when changing public APIs/module surfaces, docs, task state, or generated inventories.
- Do not read specialized method/benchmark guides unless this task unexpectedly touches those areas.

Use the available planning subagent first to reassess the current `GRAPHICS-026` state and recommend the next smallest robust slice. Then proceed autonomously until `GRAPHICS-026` is either completely done and retired, or until a verified, committable slice is complete and further work is blocked by an explicitly documented repository/tooling blocker.

## Primary objective

Complete `tasks/active/GRAPHICS-026-vulkan-renderer-plumbing-followups.md` fully.

Treat `GRAPHICS-026` as the source of truth for scope. Items `2.1` and `5.1` may already be done; confirm from code/docs/task state before assuming. Complete or explicitly defer every remaining item in sections `1–5` with a concrete follow-up task ID, owner context, timeline, and blocking status where required.

Do not retire `GRAPHICS-026` until every acceptance criterion in the task is satisfied or has an approved, documented follow-up that meets repository policy.

## Repository invariants

Preserve all `AGENTS.md` invariants:

- Use C++23.
- Preserve buildability and testability.
- Preserve layer ownership:
  - `graphics/rhi` depends only on `core`.
  - `graphics/*` may depend on `core`, asset IDs, `graphics/rhi`, and geometry GPU views.
  - Renderer code must not special-case Vulkan.
  - Backend-specific logic belongs behind RHI/backend seams.
  - Runtime remains the composition/wiring owner.
- Do not introduce cross-layer convenience imports.
- Do not mix mechanical moves with semantic refactors.
- Keep each patch small and scoped to `GRAPHICS-026`.
- Add/update tests for behavior changes.
- Sync docs and task records in the same change set.
- Treat `Testing/Temporary/LastTestsFailed.log` as historical only; current pass/fail comes from the CTest command just run.
- For noisy commands, use `set -o pipefail`, `tee`, and bounded `tail`.

## GRAPHICS-026 non-goals and guardrails

Strictly preserve these constraints:

- No new renderer features.
- No Vulkan instance/swapchain bring-up; that remains owned by `GRAPHICS-018`.
- Keep Vulkan opt-in.
- Do not make `gpu|vulkan` tests mandatory in the default CPU gate.
- Preserve the CPU/null correctness path.
- Do not special-case Vulkan in renderer code.
- Prefer deterministic, CPU-testable contract coverage where default CI cannot build or run Vulkan paths.
- Do not add new RHI surface methods without corresponding docs and task entries.
- Do not expand `GRAPHICS-018`; only update it for status/cross-links when `GRAPHICS-026` requires it.

## Required work strategy

1. Inspect current git state:
   ```bash
   git status --short --branch
   git --no-pager log --oneline -8
   find tasks/active -maxdepth 1 -type f -printf '%f\n' | sort
   ```

2. Inspect `GRAPHICS-026` and identify remaining unchecked items.

3. Work in small grouped slices. Prefer this order unless the codebase clearly indicates a safer sequence:
   - Finish low-risk Vulkan device documentation/invariant items in section `2`.
   - Address renderer lifecycle correctness items in section `1`.
   - Replace source-grep contract tests with behavioral tests where feasible in section `3`.
   - Resolve documentation/task hygiene items in section `4`.
   - Re-run full verification and retire the task only when all criteria are met.

4. For each item:
   - Trace symbols to definitions/usages before editing.
   - Keep behavior changes minimal and testable.
   - Add or update focused tests.
   - Update relevant docs and the active task progress.
   - Run focused verification before broad verification.

## Specific `GRAPHICS-026` implementation guidance

### Renderer frame lifecycle items

For section `1`:

- `1.1`: Document `IRenderer::EndFrame()` in `Graphics.Renderer.cppm`; clarify returned value is the device’s post-`EndFrame` global frame counter, not the just-completed `frame.FrameIndex`.
- `1.2`: Do not overload `RenderGraphFrameStats.Diagnostic` for pre-graph lifecycle failures. Add a distinct status/diagnostic field or enum and update tests.
- `1.3`: Replace magic determinant tolerance in `IsInvertibleFiniteMatrix` with a named constant.
- `1.4`: Add defensive `Core::Log::Warn` when routed pass-name resolution fails during execute.
- `1.5`: Resolve culling/depth-prepass pipeline failure policy consistently. Prefer soft-fail end-to-end and surface through `RenderGraphFrameStats.CommandPassesSkippedUnavailable`; document the chosen policy in `docs/architecture/graphics.md`.
- `1.6`: Make the depth-prepass dependency on culling output explicit by renaming the guard or adding a clear comment.
- `1.7`: Stop setting the depth-prepass pipeline both at initialization and every record; choose one cache point.
- `1.8`: Export named constants for max indirect draw count and cull dispatch group sizing; update tests to use those constants.
- `1.9` and `1.10`: Split `RenderGraphFrameStats` into focused sub-structs and move per-pass counters to a name-keyed representation. This is likely a larger slice; do not combine with unrelated changes.
- `1.11`: Either implement a documented “device becomes operational” renderer reset hook or file/link a blocking `GRAPHICS-018X-operational-transition` follow-up.
- `1.12`: Add a comment in `Graphics.MaterialSystem.cpp` explaining the non-operational shortcut and referencing item `1.11` or its follow-up.

### Vulkan device surface items

For section `2`:

- Confirm `2.1` remains complete: no direct `stderr`/`std::cerr` diagnostics in `src/graphics/vulkan`.
- `2.2`: Document `BeginFrame`/`EndFrame` frame-slot rotation invariant in `Backends.Vulkan.Device.cpp`.
- `2.3`: Remove dead `m_NeedsResize` writes unless a real consumer exists now.
- `2.4`: Add a `// TODO(GRAPHICS-018):` comment block in `Backends.Vulkan.Device.cppm` listing missing bring-up helper surfaces.
- `2.5`: In `DeferDelete`, replace non-operational immediate-execution fallback with early return.
- `2.6`: Document `BeginOneShot`/`EndOneShot` as queue-stalling init-time-only helpers; runtime uploads must use `ITransferQueue`.
- `2.7`: Document shutdown/deferred-delete/resource-pool invariant and add a debug-only assertion if practical without overbuilding the slice.
- `2.8`: Comment intentional `m_SamplerAnisotropySupported = false` default and reference feature-negotiation follow-up.
- `2.9`: If `RHI::SamplerDesc::BorderColor` exists, honor it. Otherwise file/link `GRAPHICS-018X-sampler-border-color`.
- `2.10`: Document `DepthOrArrayLayers` interpretation for 2D array vs 3D vs cube.
- `2.11`: In `WriteTexture`, log/skip if destination usage lacks sampled bit before transitioning to shader-read layout.
- `2.12`: Tighten upload size checking to exact match or document why slack is allowed.
- `2.13`: Do not update `CurrentLayout` if one-shot submission can fail; either thread a status return or only update after verified submit.
- `2.14`: Properly handle or explicitly reject depth-stencil uploads with diagnostics.
- `2.15`: File/link a texture-upload batching follow-up and reference it from `GRAPHICS-018`.
- `2.16`: Make fallback bindless/transfer services reachable for behavioral testing and add debug breadcrumb/counter for fallback bindless allocation.

### Test methodology items

For section `3`:

- Replace source-grep assertions in `tests/contract/graphics/Test.RendererRhiBoundary.cpp` with behavioral assertions where possible.
- Keep minimal source-grep assertions only as symbol-definition/linkage guards.
- Ensure behavioral tests are labeled `contract;graphics`.
- Preserve default CPU gate compatibility.
- Add renderer tests for skipped unavailable command-pass accounting.

### Documentation/task hygiene items

For section `4`:

- Update `tasks/active/GRAPHICS-018-vulkan-renderer-integration.md` only as required for cross-links/status.
- Update `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md` with nonblocking clarifications rather than blocking implementation.
- If creating follow-up tasks, base them on `tasks/templates/`, place them under the appropriate backlog path, and include:
  - goal,
  - non-goals,
  - context,
  - required changes,
  - tests,
  - docs,
  - acceptance criteria,
  - verification,
  - forbidden changes.
- Record removal task IDs and timelines for all temporary fail-closed shims per `AGENTS.md §13`.

## Required docs sync

Update these when behavior/policy changes:

- Renderer-side behavior:
  - `docs/architecture/graphics.md`
- Vulkan backend behavior:
  - `src/graphics/vulkan/README.md`
- Current Vulkan integration slice/cross-links:
  - `tasks/active/GRAPHICS-018-vulkan-renderer-integration.md`
- Clarifications/follow-ups:
  - `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`
- Active task status:
  - `tasks/active/GRAPHICS-026-vulkan-renderer-plumbing-followups.md`

If module surface changes, refresh:
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Verification discipline

After each grouped patch, run the strongest relevant focused checks first.

Use configured presets and a C++23-capable compiler. If `clang++-20` / `clang-20` are unavailable, confirm an available compiler version before using it, for example:

```bash
/usr/bin/clang++ --version | head -n 1
```

Configure with the repository preset. If local dependency-cache overrides are needed, document the exact command used.

Recommended verification sequence:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R '^Renderer(RhiBoundary|FrameLifecycle)\.' --timeout 60
```

For Vulkan backend compile sanity, keep Vulkan opt-in and use a confirmed-current C++23 build tree:

```bash
set -o pipefail
cmake --build --preset ci --target ExtrinsicBackendsVulkan -j2 2>&1 \
  | tee /tmp/intrinsic-vulkan-backend-build.log | tail -n 160
```

Run structural checks:

```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

Before reporting completion, run the aggregate CPU gate:

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet
```

If a broad build/test fails due to an unrelated compiler/toolchain crash or known unrelated legacy issue:
- Capture the exact command and bounded log tail.
- Confirm focused touched-scope verification passes.
- Do not claim the broad gate passed unless it actually passed on retry/current output.
- Record the blocker in the task if it prevents retirement.

## Task retirement criteria

Only retire `GRAPHICS-026` when all are true:

- Every item in sections `1–5` is implemented or has a specific follow-up task ID with timeline and blocking/nonblocking status.
- All acceptance criteria in `GRAPHICS-026` are satisfied.
- Pipeline-failure policy is consistent and documented.
- Operational-transition gap is implemented or linked as a blocking follow-up.
- `Test.RendererRhiBoundary.cpp` no longer relies on full-signature source grep where behavioral coverage is possible.
- `RenderGraphFrameStats` is split and per-pass counters are name-keyed, or a specific accepted follow-up exists if this is intentionally deferred.
- No direct `stderr` diagnostics remain in `src/graphics/vulkan`.
- Default CPU correctness gate passes.
- Docs and task records are synchronized.
- Structural checks pass.
- No new layering violations.
- No new mandatory Vulkan/GPU tests in the default CPU gate.

When retiring:
1. Read `docs/agent/task-format.md` and `docs/agent/review-checklist.md`.
2. Move the task from `tasks/active/` to `tasks/done/`.
3. Add completion metadata to the done task:
   - completion date,
   - summary of completed items,
   - verification commands and outcomes,
   - follow-up task IDs,
   - any known nonblocking caveats.
4. Update active/backlog indexes if required by existing task conventions.
5. Re-run:
   ```bash
   python3 tools/agents/check_task_policy.py --root . --strict
   python3 tools/docs/check_doc_links.py --root .
   ```

## Final response requirements

When reporting back:
- Summarize changed files and completed `GRAPHICS-026` items.
- List verification commands and pass/fail results.
- Explicitly state whether `GRAPHICS-026` was retired or remains active.
- Mention any follow-up task IDs created.
- Mention any unrelated pre-existing/untracked files left untouched.
- Do not overclaim unrun verification.

