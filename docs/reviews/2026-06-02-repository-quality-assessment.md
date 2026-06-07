# IntrinsicEngine Repository Quality Assessment

Date: 2026-06-02

Scope: WIP C++23/Vulkan research engine assessment, focused on implementation
quality, architecture, style, testability, maintainability, and WIP risk.

This review treats IntrinsicEngine as an active migration/research codebase, not
as a finished product.

## Overall Scores

| Category | Score | Assessment |
| --- | ---: | --- |
| WIP quality | 8 / 10 | Strong process, clear migration discipline, and extensive CPU/null contract coverage. |
| Production readiness | 4 / 10 | Significant legacy mass, sparse GPU coverage, large orchestration hubs, and migration allowlist debt remain. |

## Area Scores

| Area | Score | Notes |
| --- | ---: | --- |
| Architecture | 8 / 10 | Strict layering check passes, runtime owns composition, and RHI avoids Vulkan type leakage. The score is capped by 81 layering allowlist entries and 1,340 allowlisted references. |
| Implementation | 6.5 / 10 | Fail-closed behavior and diagnostics are strong. Renderer/runtime orchestration is large and manually coordinated. |
| Style | 7 / 10 | Explicit, traceable, and task-driven. Comments are often useful, but some files carry dense task-history prose. |
| Testing | 8 / 10 WIP, 5 / 10 production | Strong unit/contract/integration footprint for CPU/null paths. GPU coverage is small and opt-in. |
| Docs/process | 8 / 10 WIP, 6 / 10 production | Task and migration docs are unusually disciplined. A confirmed doc-link checker blind spot hides stale links. |
| Maintainability | 5.5 / 10 | The main risk is large orchestration files, especially the 7,005-line renderer implementation. |
| Build/CI | 7 / 10 | Requested structural validators pass. A full configure/build/CTest gate was not part of this audit. |

## Commands And Results

### Repository State

| Command | Result |
| --- | --- |
| `git status --short --branch` | `## main...origin/main` at audit time. |
| `git log --oneline -10` | Recent history is active task-slice work, ending at PR #964 merge and `RUNTIME-090` retirement. |

### Requested Validators

| Command | Result |
| --- | --- |
| `python3 tools/repo/check_layering.py --root src --strict` | Passed. Scanned 812 files, 6,805 import/include refs, 134 CMake links. Reported 81 allowlist entries and 1,340 allowlisted violations. |
| `python3 tools/repo/check_test_layout.py --root . --strict` | Passed, `findings=0`. |
| `python3 tools/agents/check_task_policy.py --root . --strict` | Passed, 286 task files validated. |
| `python3 tools/docs/check_doc_links.py --root .` | Passed in warning mode, 571 relative links checked. |
| `python3 tools/agents/validate_method_manifests.py` | Passed for 2 method manifests. |
| `python3 tools/benchmark/validate_benchmark_manifests.py` | Passed for 2 benchmark manifests. |
| `python3 tools/benchmark/validate_benchmark_results.py --strict` | Passed for 2 result JSON files. |

### Additional Validator Observation

`python3 tools/docs/check_doc_links.py --root . --strict` also reported no
broken links. However, the script removes inline-code spans before link parsing
at [tools/docs/check_doc_links.py](../../tools/docs/check_doc_links.py#L75).
That causes links whose labels are formatted as inline code, such as
`[GRAPHICS-077](...)` with a code-formatted label in source markdown, to be
missed. A separate read-only scan that did not strip inline-code labels found
71 missing targets, including stale `tasks/active` links in
[tasks/backlog/README.md](../../tasks/backlog/README.md#L81) and
[tasks/backlog/README.md](../../tasks/backlog/README.md#L92).

## Objective Metrics

### Source Files By Top-Level `src/` Directory

| Directory | Files |
| --- | ---: |
| `src/legacy` | 340 |
| `src/graphics` | 183 |
| `src/geometry` | 137 |
| `src/core` | 62 |
| `src/ecs` | 43 |
| `src/runtime` | 35 |
| `src/assets` | 15 |
| `src/platform` | 9 |
| `src/app` | 6 |

### C++ File Counts And LOC

| Scope | C++ files | LOC |
| --- | ---: | ---: |
| `src/legacy` | 330 | 75,150 |
| non-legacy `src` | 453 | 103,291 |

Legacy code is still approximately 42% of `src` C++ LOC.

### Largest Source Files

| File | LOC |
| --- | ---: |
| [src/graphics/renderer/Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp) | 7,005 |
| [tests/unit/geometry/Test.GeometryIO.cpp](../../tests/unit/geometry/Test.GeometryIO.cpp) | 5,295 |
| [src/graphics/vulkan/Backends.Vulkan.Device.cpp](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp) | 3,973 |
| [tests/contract/graphics/Test.RendererFrameLifecycle.cpp](../../tests/contract/graphics/Test.RendererFrameLifecycle.cpp) | 3,868 |
| `src/legacy/EditorUI/Runtime.EditorUI.Widgets.cpp` (retired 2026-06-07 by `LEGACY-007`) | 3,770 |

### Largest `.cppm` Module Interfaces

| File | LOC |
| --- | ---: |
| `src/legacy/EditorUI/Runtime.EditorUI.cppm` (retired 2026-06-07 by `LEGACY-007`) | 860 |
| [src/geometry/Geometry.Properties.cppm](../../src/geometry/Geometry.Properties.cppm) | 842 |
| [src/legacy/Graphics/Graphics.RenderPipeline.cppm](../../src/legacy/Graphics/Graphics.RenderPipeline.cppm) | 836 |
| [src/legacy/Asset/Asset.Manager.cppm](../../src/legacy/Asset/Asset.Manager.cppm) | 727 |
| [src/graphics/renderer/Graphics.Renderer.cppm](../../src/graphics/renderer/Graphics.Renderer.cppm) | 680 |

### Test Distribution

| Directory | Test files |
| --- | ---: |
| `tests/unit` | 140 |
| `tests/contract` | 70 |
| `tests/integration` | 46 |
| `tests/gpu` | 2 |
| `tests/benchmark` | 2 |

### Task File Counts

| Directory | Task files excluding README/index docs |
| --- | ---: |
| `tasks/done` | 225 |
| `tasks/backlog` | 79 |

## Positive Recurring Patterns

Confirmed:

- Strong layer contract. `runtime` composes lower layers, and RHI remains
  platform-neutral; runtime fills `RHI::DeviceCreateDesc` from the live window in
  [src/runtime/Runtime.Engine.cpp](../../src/runtime/Runtime.Engine.cpp#L228).
- Graphics avoids live ECS mutation. Runtime drains selection picks and documents
  graphics as reporting-only in
  [src/runtime/Runtime.Engine.cpp](../../src/runtime/Runtime.Engine.cpp#L733).
- Fail-closed renderer behavior is deliberate. Missing pass routes become
  `SkippedNonOperational` or `SkippedUnavailable`, not silent success, in
  [src/graphics/renderer/Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp#L2254).
- Vulkan stays behind the RHI boundary. `VulkanCommandContext` maps RHI
  handles/enums to Vulkan calls inside `graphics/vulkan`, including Sync2 barrier
  emission in
  [src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp#L537).
- Tests target abstractions. `Test.RHI.CommandContextBarriers.cpp` defines a fake
  command context and verifies fallback barrier routing in
  [tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp](../../tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp#L14).
- Migration docs are honest about WIP status. The parity matrix explicitly says
  it is not claiming legacy retirement is complete in
  [docs/migration/nonlegacy-parity-matrix.md](../migration/nonlegacy-parity-matrix.md#L5).

## Negative Recurring Patterns

Confirmed:

- Legacy remains large: 75,150 C++ LOC in `src/legacy`.
- The renderer is a central bottleneck. A large `NullRenderer` begins at
  [src/graphics/renderer/Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp#L271),
  and pass execution later dispatches 20 named pass cases.
- Pass routing is manually synchronized with recipe names. The branch chain
  begins at
  [src/graphics/renderer/Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp#L1626).
- Layering allowlist metadata is known debt. `HARDEN-069` says every allowlist
  row points at retired `HARDEN-010`, contrary to the intended current-task rule,
  in
  [tasks/done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md](../../tasks/done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md).
- Documentation checks miss stale links when link labels are inline-code
  formatted. See
  [tools/docs/check_doc_links.py](../../tools/docs/check_doc_links.py#L75).
- Production evidence is GPU-light: 2 GPU test files compared with 216
  unit/contract/integration test files.
- Several recently retired tasks are explicitly `CPUContracted`, not
  `Operational`; for example, ImGui producer work defers renderer-side
  `Pass.ImGui` to `GRAPHICS-079` in
  [tasks/active/README.md](../../tasks/active/README.md#L43).

## Greatest Strength

The task-contract-migration discipline is the strongest asset. The repository is
clear about what is scaffolded, CPU-contracted, operational, or blocked, and the
structural validators reinforce that discipline.

## Greatest Weakness

The green process can mask unfinished runtime reality. A strict layering pass
still depends on 1,340 allowlisted references, doc-link validation misses stale
code-labeled links, and GPU/Vulkan evidence remains opt-in and sparse.

## Risks Hidden By Good Process Discipline

Confirmed:

- The doc-link checker can pass while missing stale task links whose labels are
  inline-code formatted.
- The layering allowlist quality checker passes even though backlog records that
  rows point at a retired task.
- The renderer's size and string-routing are measurable maintainability risks.

Hypotheses:

- Some source comments may be stale because many encode historical task slices,
  call indices, and old gate names.
- CPU/null contract success may overstate Vulkan readiness until GPU/Vulkan
  gates run routinely on representative hardware.
- Repeated default no-op virtuals in RHI may reduce test friction but can hide
  missing backend behavior unless each new seam gets explicit contract tests.

## Focus Review: `RHI.CommandContext.cppm`

Reviewed file:
[src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm)

### API Cleanliness

The API is clean for a backend-neutral command-recording seam. It exports
`ICommandContext` at
[src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L134),
uses typed handles, and exposes no `Vk*` types.

The command surface is broad but understandable: lifecycle, render pass,
dynamic state, pipeline binding, draw/dispatch, barriers, copies, and optional
sampled-framegraph hooks are all represented directly.

### Module-Interface Hygiene

The interface is compact compared with other `.cppm` files, but it still carries
non-trivial default virtual bodies:

- `BindFrameSampledTexture(...)` forwards to `BindFrameSampledTextureAt(...)` at
  [src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L162).
- `SubmitBarriers(...)` performs alignment checks and loops over spans in
  [src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L237).
- `CopyTextureToBuffer(...)` has a default no-op body at
  [src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L312).
- `BindFrameSampledTextureAt(...)` has a default no-op body at
  [src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L340).

The most questionable item is `SubmitBarriers(...)`: it is useful for mocks and
older contexts, but its control flow belongs in an implementation unit under the
repository's module-interface hygiene rule unless the default body is considered
part of the public compatibility contract.

### Backend Abstraction

The abstraction boundary is mostly strong. `VulkanCommandContext` implements the
interface behind the Vulkan module partition and maps barrier packets to Sync2 in
[src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp#L537).

The main WIP abstraction smell is `BindFrameSampledTextureAt(...)`. The Vulkan
implementation calls it part of a temporary sampled-present bridge in
[src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp#L114).
That is pragmatic for bring-up, but it exposes renderer/backend descriptor-slot
strategy through RHI.

### Testability

Testability is strong. The command context supports simple fake implementations,
and [tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp](../../tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp#L72)
verifies fallback barrier routing. The same file checks that `MemoryAccess`
combines attachment bits without truncation at
[tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp](../../tests/contract/graphics/Test.RHI.CommandContextBarriers.cpp#L116).

### ABI And Vtable Risk

The risk is high. `ICommandContext` has 28 virtual declarations, and many
backends/test doubles implement it. The RHI README explicitly calls it an
exported polymorphic interface whose vtable is cross-TU ABI in
[src/graphics/rhi/README.md](../../src/graphics/rhi/README.md#L28).

The README also documents stale module-BMI/vtable SEGV risk in
[src/graphics/rhi/README.md](../../src/graphics/rhi/README.md#L36). The
interface itself documents a Clang 20 modules/default-argument vtable issue in
[src/graphics/rhi/RHI.CommandContext.cppm](../../src/graphics/rhi/RHI.CommandContext.cppm#L302).

This is not a reason to avoid the interface, but every virtual change should be
treated as ABI-sensitive and verified with a clean preset rebuild.

## Confirmed Findings

- `check_layering.py --strict` passes but depends on 81 allowlist entries and
  1,340 allowlisted references.
- `tools/repo/layering_allowlist.yaml` still references `task: "HARDEN-010"` in
  rows inspected during the audit, while `HARDEN-069` records that this should be
  rebound to active/backlog removal tasks.
- `check_doc_links.py` misses stale links when labels are inline-code formatted.
- `Graphics.Renderer.cpp` is the largest source file in the inspected set and
  contains a 20-branch string-routed pass executor.
- `RHI.CommandContext.cppm` is backend-clean but ABI-sensitive.

## Hypotheses To Validate

- Renderer pass routing may be safer as a generated or registered dispatch table
  once the default recipe stabilizes.
- Some task-history comments may be stale relative to current backlog state.
- Vulkan operational readiness may be overestimated if assessed primarily from
  CPU/null contracts.
- The default no-op RHI virtuals may hide missing backend behavior unless every
  optional seam has explicit contract tests.

## Follow-Up Audit Questions

1. Which 81 layering allowlist rows are still justified by open removal tasks
   after `HARDEN-069`, and which are stale?
2. Should `check_doc_links.py` parse links before stripping inline-code spans?
3. Which `CPUContracted` tasks are blocking the first real `Operational` sandbox
   run?
4. How many pass-routing branches in `Graphics.Renderer.cpp` can move to a
   registry/table without changing behavior?
5. What GPU/Vulkan tests run in CI versus local or nightly, and what hardware
   coverage is assumed?

## Highest-Leverage Next Review Targets

1. [src/graphics/renderer/Graphics.Renderer.cpp](../../src/graphics/renderer/Graphics.Renderer.cpp)
   for pass routing, pass-resource lifetime, and decomposition risk.
2. [tools/docs/check_doc_links.py](../../tools/docs/check_doc_links.py) plus
   [HARDEN-069](../../tasks/done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
   for process checks that currently pass while hiding stale links or allowlist
   metadata debt.
3. [src/graphics/vulkan/Backends.Vulkan.Device.cpp](../../src/graphics/vulkan/Backends.Vulkan.Device.cpp)
   and
   [src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp](../../src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp)
   for operational Vulkan readiness, descriptor bridges, queue ownership, and
   GPU evidence.
