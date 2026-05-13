# GRAPHICS-033B — Vulkan operational diagnostics snapshot and runtime breadcrumb

## Goal
- Add the diagnostics surface and runtime startup breadcrumb declared by `GRAPHICS-033`: a Vulkan-owned `VulkanOperationalDiagnosticsSnapshot` carrying process-monotonic counters (`VulkanFallbackToNullCount`, `VulkanInitFailureCount`, `VulkanValidationErrorCount`, `VulkanOperationalGateFailureCount`, `VulkanDeviceLostOperationalDropCount`) and a fixed-size reason histogram, plus a once-per-startup `VulkanRequestedButNotOperational` runtime breadcrumb whenever the runtime requested Vulkan but the resolved device is Null.

## Non-goals
- No Vulkan command-recording bodies (`GRAPHICS-033C`).
- No `gpu;vulkan` smoke (`GRAPHICS-033D`).
- No mutation of the runtime device-selection path (`Runtime.Engine.cpp:49–73` continues to consult the existing config flags); this task only adds visibility.

## Context
- Status: in-progress.
- Owner/agent: Claude on branch `claude/inspect-engine-state-Ava9i`.
- Owner/layer: `graphics/vulkan` for diagnostics; `runtime` for the breadcrumb call site.
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-B in the parent's Required changes.
- Upstream gate: `GRAPHICS-033A` (the evaluator + reason taxonomy must exist).
- Truth table for runtime reconciliation is locked in `src/graphics/vulkan/README.md:364–376`.

## Required changes
- [x] Add `VulkanOperationalDiagnosticsSnapshot` next to the existing `FallbackDiagnosticsSnapshot` in `Backends.Vulkan` umbrella, with the five process-monotonic counters and a `std::array<std::uint32_t, kVulkanReasonCount> ReasonHistogram` (one slot per `VulkanOperationalReason`).
- [x] Add `GetVulkanOperationalDiagnosticsSnapshot()` accessor; counters never reset across `Initialize`/`Shutdown` cycles.
- [x] Wire the counter increments in the appropriate transitions: every `RequestedButX` evaluator outcome bumps `VulkanFallbackToNullCount` plus the matching reason histogram bucket plus a path-specific counter (`VulkanInitFailureCount` for `RequestedButFailedInit`, etc.). `Operational → non-Operational` due to device-loss bumps `VulkanDeviceLostOperationalDropCount`.
- [x] Runtime breadcrumb: in `Runtime.Engine::Initialize()` (immediately after device construction), if the runtime requested Vulkan (`m_Config.Render.Backend == GraphicsBackend::Vulkan` && `m_Config.Render.EnablePromotedVulkanDevice`) but the resolved status is non-Operational, emit `Core::Log::Warn("[Runtime] VulkanRequestedButNotOperational status={...} reason={...}")` exactly once per startup. Use the evaluator status taxonomy from `GRAPHICS-033A`.
- [x] Runtime never aborts solely because requested Vulkan falls back to Null (truth-table column "Runtime result = continue").

## Tests
- [x] `contract;graphics` test: each truth-table row produces the recorded counter and reason-histogram bucket increments.
- [x] `contract;graphics` test: counters survive `Shutdown`/`Initialize` cycles (process-monotonic).
- [x] `contract;runtime` test: the `VulkanRequestedButNotOperational` warn breadcrumb fires exactly once per `Engine::Initialize()` when the truth-table preconditions are met, and zero times when Vulkan is not requested or is operational.
- [x] `contract;runtime` test: runtime initialization succeeds in every truth-table row (never aborts on fallback).
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/graphics/vulkan/README.md` to flip the planned `VulkanOperationalDiagnosticsSnapshot` and breadcrumb rows to current state.
- [x] Update `src/runtime/README.md` to document the breadcrumb call site.

## Acceptance criteria
- [x] All five counters and the reason histogram are observable through `GetVulkanOperationalDiagnosticsSnapshot()`.
- [x] The breadcrumb fires once per startup under the documented conditions.
- [x] No regression in existing `FallbackDiagnosticsSnapshot` behavior or other Vulkan diagnostics snapshots.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicBackendsVulkanContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding the breadcrumb to graphics layers (it is a runtime startup breadcrumb).
- Aborting startup on fallback.
- Mutating renderer pass routing (reserved for `GRAPHICS-033C`).

## Next verification step
- Project CI gate on the PR (`ci` preset, clang-20 toolchain) for the focused
  contract targets and the layering/doc-link/test-layout checks; the
  authoring container ships clang-18 only and cannot execute the build/CTest
  gate locally, matching the GRAPHICS-033A retirement note.
