# GRAPHICS-033 — Vulkan operational readiness and runtime fallback diagnostics (planning)

## Goal
Lock down the operational-readiness gate for the promoted Vulkan backend — the exact contract list, the single source of truth for "operational vs not", the runtime/CMake/device reconciliation matrix, the diagnostics surface, and the test split — before any code changes. The backend stays fail-closed in this slice; planning produces the gate definitions that GRAPHICS-018 / 018R / 026 implementation children consume.

## Non-goals
- No implementation, no Vulkan command-recording bodies, no CMake option additions in this slice.
- No new graphics passes, shaders, or materials (covered by GRAPHICS-031 / 032 and earlier).
- No optional Vulkan extension growth (mesh shaders, ray tracing, etc.) beyond the GRAPHICS-032 minimal recipe.
- No texture upload feature growth (GRAPHICS-018T / 026 own those).
- No live ECS access from `src/graphics/vulkan/*`.
- No bypass of fail-closed behavior; the backend remains fail-closed until contracts are demonstrably satisfied.
- No editor / ImGui present integration changes beyond what GRAPHICS-013CQ records.

## Context
- Owner layer: `graphics/vulkan` for the operational gates and recording bodies; `runtime` for reconciliation between reference engine config, CMake options, and device fallback.
- `src/runtime/Runtime.Engine.cpp` returns `Backends::Null::CreateNullDevice()` when promoted Vulkan is not both compiled and enabled. `src/runtime/Runtime.Engine.cppm` sets the reference render backend to Vulkan, but execution is gated by promoted-device configuration and CMake build options.
- `src/graphics/vulkan/README.md` documents the backend as fail-closed (`IsOperational() == false`) until canonical renderer pass command recording, synchronization/barrier validation, queue-family ownership, and service fallback reconciliation are completed.
- The 2026-05-08 review (sections "Exact missing pieces / 6" and "minimal milestone plan / 4") requires preserving fail-closed behavior until contracts are met and adding explicit diagnostics when null fallback is selected despite Vulkan being requested.
- GRAPHICS-018 / 018Q / 018R / 018S / 018T / 026 already establish integration scaffolding, sampler/border-color, texture-upload batching, and operational-transition planning.
- GRAPHICS-032 lands the minimal CPU-mock recipe that the operational Vulkan path must record against the real device once enabled.

## Design decisions to record
1. **Operational gate enumeration.** Lock the explicit, ordered checklist that flips `IsOperational()` to `true`:
   - Instance + selected `VkPhysicalDevice` + queue families (graphics + present + transfer) acquired and recorded.
   - Logical device + allocator (VMA or current promoted equivalent) initialized with documented heap budgets.
   - Swapchain creation, acquire, present, resize/recreate paths satisfied with explicit format/colorspace policy.
   - Command pool / buffer / synchronization (semaphores, fences, timeline if used) contracts satisfied for the GRAPHICS-032 minimal recipe.
   - Barrier and layout-transition validation satisfied for that recipe.
   - Validation-layer error policy and breadcrumb integration satisfied per the GRAPHICS-018Q per-call pre-bring-up pattern.
   - Service fallback reconciliation: a single function answers "operational?" without disagreement between subsystems.
   Each gate item has a named contract test in the implementation children.
2. **Single source of truth.** Decide the exact module + function signature that the rest of the engine queries (suggested `Backends::Vulkan::IsOperational(const VulkanDeviceContext&) -> OperationalStatus`) and the rule that no other module re-derives the answer from CMake options or runtime config.
3. **Operational-status shape.** Decide a small enum + reason codes (`OperationalStatus { NotCompiled, NotRequested, RequestedButUnsupported, RequestedButFailedInit, Operational }`) so diagnostics can name the precise reason. Forbid bool-only returns.
4. **Reconciliation matrix.** Lock the truth table over (CompiledIn, Requested, HostSupports, InitSucceeded) → effective device + diagnostic emission. Each row records: which device the runtime returns, which counter increments, whether the warn breadcrumb fires, and whether `Runtime.Engine` continues or aborts.
5. **Diagnostic counters and breadcrumbs.** Name explicitly: `VulkanFallbackToNullCount`, `VulkanInitFailureCount`, `VulkanValidationErrorCount` (with reason histogram), and a single `VulkanRequestedButNotOperational` warn breadcrumb that fires once at startup when Vulkan was requested but a null device is returned. Decide breadcrumb cadence policy per GRAPHICS-018Q rate-limited rules.
6. **Validation-layer policy.** Decide enable/disable rule by build configuration (debug vs release vs CI) and how validation errors interact with the fail-closed gate (recommend: any validation error during the operational-gate check forces `RequestedButFailedInit`).
7. **Required-vs-optional extension list.** Enumerate exactly which device/instance extensions and features the minimal recipe requires (surface, swapchain, dynamic rendering or render-pass form chosen by GRAPHICS-013CQ, descriptor indexing if used by SceneTableBDA, etc.). Probed-and-optional features (anisotropy per GRAPHICS-018Q) are listed separately. Forbid silent enablement of non-required extensions.
8. **Queue-family ownership rules.** Lock the rule for graphics/present/transfer queue selection and ownership transfer policy in the minimal recipe; align with GRAPHICS-018T transfer-queue contracts.
9. **Diagnostic surface placement.** Decide whether the diagnostics live on a new `VulkanOperationalDiagnostics` snapshot or on the existing renderer diagnostics surface; record the rule that runtime reads them at startup and after device reset.
10. **Hot-reload / swapchain recreation.** Decide the rule for transient operational transitions (swapchain lost, surface invalidated): the gate may flip to non-operational temporarily; the diagnostic counter increments; runtime must not silently fall back to null without surfacing the transition.
11. **Test split.** Enumerate the test categories:
    - `contract;graphics` reconciliation-matrix tests that exercise the truth table without a real device by stubbing `IsOperational` inputs.
    - `contract;graphics` validation-layer policy tests using mocks.
    - Opt-in `gpu;vulkan` smoke test that runs the GRAPHICS-032 minimal recipe against a real device when present, kept outside the default CPU gate per AGENTS.md §7.
12. **Performance characteristics.** Record: operational-gate evaluation runs once at init (and on documented resize/recreate events), not per frame. No per-frame string allocation for diagnostics; counters are atomic increments.
13. **Extensibility forecast.** Enumerate the next gates that will be added later (ray tracing, mesh shaders, multi-GPU): each gate appends to the ordered checklist and reason enum without rewriting the existing gates.
14. **Layering audit.** Confirm: `src/graphics/vulkan/*` does not access live ECS; `src/runtime/*` reconciles config but does not call `vk*` symbols directly; the operational decision is reachable from runtime through a graphics-public API.

## Required changes
- Capture all fourteen decisions as explicit recorded answers, including the truth table in decision (4) and the gate enumeration in decision (1).
- Cross-link with GRAPHICS-013CQ (present), GRAPHICS-018 / 018Q / 018R / 018S / 018T / 026, GRAPHICS-022 (rendergraph diagnostics), GRAPHICS-027 (post-shim cleanup), GRAPHICS-032 (minimal recipe consumer), and the 2026-05-08 review.
- Identify follow-up implementation children (do **not** open here):
  - **GRAPHICS-033-Impl-A** — `OperationalStatus` enum + `IsOperational` single-source function + reconciliation matrix scaffolding + `contract;graphics` reconciliation tests.
  - **GRAPHICS-033-Impl-B** — diagnostic counters + warn breadcrumb wiring + runtime emission tests.
  - **GRAPHICS-033-Impl-C** — Vulkan recording bodies for the GRAPHICS-032 minimal recipe (gated by GRAPHICS-018R operational-transition prerequisites).
  - **GRAPHICS-033-Impl-D** — opt-in `gpu;vulkan` smoke test fixture exercising one visible-triangle frame on hosts that support Vulkan.

## Tests
- Planning slice: validators only.
- Implementation children must add the matrix, diagnostic, and smoke tests as enumerated above.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- Optional GPU smoke gate (only on hosts with Vulkan):
  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
  ```

## Docs
- Update `src/graphics/vulkan/README.md` to record the gate enumeration, reason enum, and diagnostic surface.
- Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` with the operational-readiness gates and reconciliation rule.
- Update `docs/migration/nonlegacy-parity-matrix.md` rows for Vulkan operational status.
- Update `tasks/backlog/rendering/README.md` DAG after GRAPHICS-032.

## Acceptance criteria
- All fourteen decisions recorded with explicit answers and trade-off rationales; the gate checklist and the truth table are fully enumerated.
- Implementation children identified with scope and dependency gates but not opened.
- Backend remains fail-closed in this slice; no engine behavior changes land.
- Layering invariants hold.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No relaxation of fail-closed behavior before the contracts are satisfied.
- No optional Vulkan extension growth beyond the minimal recipe.
- No live ECS access from Vulkan backend code.
- No texture-upload feature growth in this slice.
- No bool-only `IsOperational()` shape; reasons must be enumerable.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
