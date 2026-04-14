# `src_new` Boundary Review (2026-04-14)

## Executive summary

The high-level folder boundaries are **directionally good** (`Core`, `Platform`, `RHI`, `Graphics`, `Runtime`, `App`), but the concrete module/API boundaries are **not stable yet**. You should do a small reorganization **now** to prevent cross-layer leakage and expensive refactors once implementations fill in.

## What is already good

- Clear top-level split by concern (`Core`, `Platform`, `ECS`, `Assets`, `Graphics`, `Runtime`, `App`).
- Runtime acts as composition root (`Runtime` links Core/Platform/Graphics/ECS/Assets).
- `RHI::IDevice` and `Platform::IWindow` indicate intent to abstract backend/platform details.

## Boundary risks found

### 1) Module naming contract is inconsistent

- `src_new/Graphics/RHI/RHI.Device.cppm` declares `export module RHI.Device;`.
- Most users import `Extrinsic.RHI.Device`.

This breaks the "one canonical module namespace" boundary and can produce fragile build behavior depending on toolchain/module mapper behavior.

### 2) Factory placement creates layer inversion pressure

- `RHI.BackendFactory.cpp` imports `Extrinsic.Backends.Vulkan` directly.

That means backend selection logic currently lives in RHI while depending on a concrete backend module. This is manageable for one backend, but it tends to grow into a compile-time coupling knot when DX12/Metal arrive.

### 3) Config object is too globally visible

- `Core.Config` is imported by Platform, RHI, Vulkan backend, Runtime, and app entry.

This often becomes a hidden "god DTO" where unrelated settings leak across layers. Better split into narrow config views per subsystem.

### 4) Interface + implementation colocation is incomplete

- Several ECS modules are currently empty stubs.
- Folder names imply stable boundaries, but contract-level boundaries are not yet encoded as dependency rules.

If you wait, accidental imports will accumulate and force broad rewires later.

## Recommended reorganization (do now)

### A. Freeze module namespace policy

Adopt one namespace prefix for all exported modules, e.g. `Extrinsic.*` only.

### B. Enforce dependency direction

Target DAG:

- `Core` (no engine deps)
- `Platform` -> `Core`
- `RHI` -> `Core`, `Platform` (interfaces only)
- `Backends/*` -> `RHI`, `Platform`, `Core`
- `Graphics` -> `RHI`
- `ECS` -> `Core` (+ optional data-only bridges)
- `Runtime` -> compose everything
- `App` -> `Runtime`

Critically: RHI interface module should not know specific backend modules.

### C. Isolate backend registry/composition root

Move backend registration/selection into a dedicated composition layer (`Runtime` or a `GraphicsBootstrap` unit), so RHI remains a pure abstraction boundary.

### D. Split config by bounded context

Replace monolithic config exposure with narrow structs:

- `PlatformWindowConfig`
- `RhiDeviceConfig`
- `RendererConfig`
- `RuntimeLoopConfig`

### E. Add CI guardrails now

- Simple import-lint script (forbidden edge list).
- CMake target-level dependency assertions.
- "No concrete backend include/import from RHI interface layer" rule.

## Effort / impact

- **Now:** low-to-medium effort (mostly naming/dependency cleanup), high future savings.
- **Later:** medium-to-high effort due to transitive import proliferation and API churn.

## Decision

You should reorganize **now (lightweight pass)**, not later.

## Reorganization status (applied 2026-04-14)

Completed in-code:

- Module naming normalized for device interface (`export module Extrinsic.RHI.Device`).
- Backend selection moved out of RHI into Runtime composition path (`Runtime.Engine.cpp` local factory).
- RHI backend-factory module files removed from build and source tree.

Still recommended:

- Split `Core.Config` into narrower subsystem configs.
- Add dependency-lint/CI forbidden-edge checks.
