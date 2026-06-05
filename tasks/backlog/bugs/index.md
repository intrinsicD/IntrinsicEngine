# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

- [`BUG-015` — ExtrinsicSandbox clustered Vulkan validation cascade](BUG-015-extrinsic-sandbox-clustered-vulkan-validation-cascade.md).
  Running the local debug `ExtrinsicSandbox` with promoted Vulkan reports
  compute pipeline-layout mismatches for clustered-lighting storage buffers,
  followed by queue-ownership, command-buffer-lifetime, and transient image
  usage/layout validation errors. The first validation class to fix is the
  cluster-grid/light-assignment compute descriptor layout mismatch; later
  validation errors must be rechecked as possible cascades.

---

## Verified / Closed

- Closed 2026-06-05: [`BUG-014` — ExtrinsicSandbox ImGui black window regression](../../done/BUG-014-extrinsic-sandbox-imgui-black-window.md). The black frame was caused by a Vulkan descriptor collision: framegraph bridge slots for DebugView/Present overlapped real bindless texture leases, so `Pass.Present` could overwrite the retained ImGui font-atlas slot. The promoted Vulkan bindless allocator now reserves slots 0..2 and starts real texture leases at slot 3; the app-default `gpu;vulkan` regression asserts recorded `Present`/`ImGuiPass` plus non-black backbuffer readback with validation enabled.

- Closed 2026-05-29: [`BUG-013` — Default-recipe + minimal-debug backbuffer readback contract tests SEGV under clang-20 modules](../../done/BUG-013-backbuffer-readback-contract-vtable-segv.md). **Not reproducible on a clean `ci` preset build.** In a freshly-cloned tree the two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass through the default CPU gate (CTest #25/#87, label `contract`; the full `IntrinsicGraphicsContractCpuTests` binary is 225/225). The single module-owned `ICommandContext` vtable shows no cross-TU divergence, and the exact crash site (`CopyTextureToBuffer` dispatched through a base `ICommandContext&` to a non-overriding `MockCommandContext`) executes correctly. The reported SEGV was a stale incremental module-BMI artifact after `cc06edef` added the inline-bodied `BindFrameSampledTexture` virtual — a known clang-20 / C++23-modules hazard that a clean preset rebuild (the authoritative verification per AGENTS.md §7) eliminates. Prevention documented in `src/graphics/rhi/README.md`; no engine/test source changed. Unblocks `GRAPHICS-076E` CPU contract closure.

- Closed 2026-05-17: [`BUG-011` — `docs-validation` rejects `ci-vulkan.yml` as an unexpected workflow file](../../done/BUG-011-ci-vulkan-workflow-allowlist.md). `tools/ci/check_workflow_names.py::ALLOWED_WORKFLOW_FILES` now includes `ci-vulkan.yml` (mirroring the `nightly-deep.yml` allowed-but-not-required precedent), and `docs/migration/target-repo-layout.md` lists the file in the canonical `.github/workflows/` layout. The GRAPHICS-080-introduced workflow now passes the `ci-docs` row's "Validate workflow file naming policy" step under both default and `--strict` modes.
- Closed 2026-05-14: [`BUG-010` — Minimal recipe present-pass barrier acceptance asserts wrong layout transition](../../done/BUG-010-minimal-recipe-present-barrier-contract.md). The acceptance test now scans for the first backbuffer barrier with `After == Present` and asserts the canonical `Undefined -> Present` shape, matching the framegraph's imported-backbuffer policy.
- Closed 2026-05-14: [`BUG-009` — Minimal recipe surface pass executes when culling output is unavailable](../../done/BUG-009-minimal-recipe-surface-pass-culling-prerequisite.md). `RecordMinimalDebugSurfacePass` now also gates on `m_CullingOutputAvailable` so a failed culling-pipeline create skips the recipe's `DrawIndexedIndirectCount` rather than recording against bucket buffers the culling dispatch never wrote.
- Closed 2026-05-14: [`BUG-008` — Vulkan `:Device` partition cannot name `VulkanOperationalInputs` under clang-20](../../done/BUG-008-vulkan-device-partition-operational-types.md). The operational-status surface is extracted into a `:OperationalStatus` partition that the umbrella and `:Device` partition both re-export; `EvaluateVulkanDeviceOperationalStatus` is a friend of `VulkanDevice` so it can read the private gate inputs without widening `IDevice`. `ExtrinsicBackendsVulkan` and `IntrinsicGraphicsVulkanContractTests` build cleanly under clang-20.
- Closed 2026-05-13: [`BUG-007` — GpuAssetCache uploads remain pending in default CPU gate](../../done/BUG-007-gpu-asset-cache-default-gate-failures.md). `RHI::ITransferQueue::UploadTextureFullChain(...)` now remains appended after the original upload/poll/collect virtuals, preserving the `IsComplete()` slot used by existing module consumers; the focused `GpuAssetCache`/material-system repro and default CPU CTest gate pass.
- Closed 2026-05-09: [`BUG-002` — CI full build compiles ImGuizmo upstream target without ImGui includes](../../done/BUG-002-ci-full-build-imguizmo-upstream-target.md). ImGuizmo is populated as source-only and repository consumers use `imguizmo_lib` with the ImGui dependency wired explicitly.
- Closed 2026-05-09: [`BUG-003` — FetchContent cache corruption breaks dependency checkouts during CI retries](../../done/BUG-003-fetchcontent-cache-corrupts-shared-dependency-checkouts.md). Dependency source trees are validated before reuse and incomplete online caches are removed before repopulation.
- Closed 2026-05-09: [`BUG-004` — Compile-hotspot gate baseline references stale runtime source paths](../../done/BUG-004-compile-hotspot-baseline-stale-runtime-paths.md). The baseline now uses current `src/geometry/` and `src/legacy/` paths.
- Closed 2026-05-09: [`BUG-005` — CI dependent steps report missing artifacts as primary failures](../../done/BUG-005-ci-dependent-steps-report-missing-artifacts-as-primary-failures.md). CI dependent steps now run explicit prerequisite guards and benchmark validation reports missing result roots as blocked prerequisites.
- Closed 2026-05-09: [`BUG-006` — Mesh-backed graph views abort ShortestPath tests on connectivity type collision](../../done/BUG-006-shortest-path-mesh-backed-graph-connectivity-view.md). Mesh-backed graph view construction now uses the correct property-set order and graph-specific compatibility connectivity until `GEOM-003` performs the semantic split.
- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
