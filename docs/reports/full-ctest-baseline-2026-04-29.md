# Full CTest Baseline — 2026-04-29

## Scope

This report records the post-reorganization full-CTest baseline for HARDEN-002. It is linked from the archived hardening tracker: [`tasks/done/0001-post-reorganization-hardening-tracker.md`](../../tasks/done/0001-post-reorganization-hardening-tracker.md).

Legacy retirement is not in scope for this baseline.

## Environment

- **Host OS:** Linux.
- **Preset:** `ci` from `CMakePresets.json`.
- **Preset compiler request:** `clang-20` / `clang++-20`.
- **Available compiler used for adjusted baseline:** `/usr/bin/clang` / `/usr/bin/clang++`.
- **Adjusted compiler version reported by CMake:** Clang 22.0.0.
- **Build tree:** `build/ci`.
- **Sanitizers:** enabled by the CI preset for Debug/RelWithDebInfo builds.
- **Vulkan runtime observed:** Vulkan 1.3.296 found; headless tests selected `NVIDIA GeForce RTX 4090`; `libvulkan_virtio.so` ICD returned `-3` and was skipped by Vulkan loader.

## Command log

### Requested configure command

```bash
cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON
```

**Result:** failed on this machine because `clang-20` and `clang++-20` are not in `PATH`.

Failure excerpt:

```text
The CMAKE_CXX_COMPILER:

  clang++-20

is not a full path and was not found in the PATH.

The CMAKE_C_COMPILER:

  clang-20

is not a full path and was not found in the PATH.
```

**Owner task:** HARDEN-050 / HARDEN-006 documentation and CI consistency should decide whether `clang-20` is mandatory or whether developer docs should show an explicit compiler override.

### Adjusted configure command used to capture the baseline

```bash
cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
```

**Result:** passed.

### Build command

```bash
cmake --build --preset ci --target IntrinsicTests
```

**Result:** passed.

### Requested full CTest command

```bash
ctest --test-dir build/ci --output-on-failure --timeout 60
```

**Result:** started and reached the known failure region, then the terminal execution was interrupted after 300 seconds while running test `1432`. Before interruption, the run reproduced the VMA unmap failures and the two runtime selection expectation failures.

The generated CTest inventory contains 3514 tests. A focused check also found five generated `_NOT_BUILT` tests at the end of the suite that fail immediately because their executables do not exist.

### Requested GPU label command

```bash
ctest --test-dir build/ci --output-on-failure -L gpu --timeout 60
```

**Result:** no tests found.

**Interpretation:** no generated CTest entry currently has a standalone `gpu` label. This is a HARDEN-005 test taxonomy issue.

### Requested runtime label command

```bash
ctest --test-dir build/ci --output-on-failure -L runtime --timeout 60
```

**Result:** no tests found.

**Interpretation:** generated aggregate labels appear as a single space-separated label string on `IntrinsicTests`-expanded tests, so `ctest -L runtime` does not match them as intended. This is a HARDEN-005 label wiring issue.

### Requested graphics label command

```bash
ctest --test-dir build/ci --output-on-failure -L graphics --timeout 60
```

**Result:** no tests found.

**Interpretation:** same label wiring issue as the runtime command; additionally no standalone GPU/Vulkan taxonomy labels exist yet.

## Determinism checks

Focused command run twice:

```bash
ctest --test-dir build/ci --output-on-failure -R 'TransferTest|GraphicsBackendHeadlessTest|AssetPipelineHeadlessTest|RuntimeSelection.ResolveGpuSubElementPick' --timeout 60
```

Both runs produced the same 23 failures out of 34 selected tests:

- 9 `TransferTest.*` VMA unmap assertion aborts.
- 2 `RuntimeSelection.ResolveGpuSubElementPick_*` deterministic expectation failures.
- 3 `GraphicsBackendHeadlessTest.*` VMA unmap assertion aborts.
- 9 `AssetPipelineHeadlessTest.*` VMA unmap assertion aborts.

Additional command:

```bash
ctest --test-dir build/ci --output-on-failure -R 'NOT_BUILT|Extrinsic' --timeout 60
```

Result: 5 deterministic `Not Run` failures due missing placeholder executables.

## Failure groups by root cause

### Group 1 — VMA unmap assertion in transfer/headless Vulkan teardown

**Failed executable:** `build/ci/bin/IntrinsicTests`.

**Failure type:** assertion / subprocess abort in VMA unmap.

**Requires GPU / Vulkan / window system:** requires Vulkan-capable headless device; no window system was required by the focused failures.

**Deterministic across two runs:** yes for the focused set.

**Representative assertion messages:**

```text
vk_mem_alloc.h:10956: void VmaAllocation_T::DedicatedAllocUnmap(VmaAllocator): Assertion `0 && "Unmapping dedicated allocation not previously mapped."' failed.
vk_mem_alloc.h:10897: void VmaAllocation_T::BlockAllocUnmap(): Assertion `0 && "Unmapping allocation not previously mapped."' failed.
```

**Tests observed:**

| Test name | Failure type | Notes | Owner task |
|---|---|---|---|
| `TransferTest.TimelineValue_ConcurrentSafeDestroy` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.AsyncBufferUpload` | VMA assertion / abort | Block allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.StagingBeltManySmallUploads` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.UploadBufferHelper` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.UploadBufferBatchHelper` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.UploadBuffer_NullDst_ReturnsInvalidToken` | VMA assertion / abort | Dedicated allocation unmap not previously mapped even on invalid-token path. | HARDEN-003 |
| `TransferTest.UploadBuffer_EmptySrc_ReturnsInvalidToken` | VMA assertion / abort | Dedicated allocation unmap not previously mapped even on invalid-token path. | HARDEN-003 |
| `TransferTest.FreeCommandBuffer_ReleasesWithoutSubmit` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `TransferTest.FreeCommandBuffer_NullIsNoOp` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `GraphicsBackendHeadlessTest.DescriptorSubsystemsCreatable` | Vulkan validation error plus VMA assertion / abort | Also reports dynamic UBO descriptor combined with update-after-bind flag. | HARDEN-003 |
| `GraphicsBackendHeadlessTest.DestructionOrderSafe` | Vulkan validation error plus VMA assertion / abort | Also reports dynamic UBO descriptor combined with update-after-bind flag. | HARDEN-003 |
| `GraphicsBackendHeadlessTest.TransferManagerOperational` | VMA assertion / abort | Dedicated allocation unmap not previously mapped. | HARDEN-003 |
| `AssetPipelineHeadlessTest.AssetManagerAccessible` | VMA assertion / abort | Asset pipeline initializes/shuts down before transfer teardown abort. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.ProcessMainThreadQueueExecutesTasks` | VMA assertion / abort | Same transfer teardown abort. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.RunOnMainThreadIsThreadSafe` | VMA assertion / abort | Same transfer teardown abort. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.TrackMaterialAddsToList` | VMA assertion / abort | Same transfer teardown abort. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.RegisterAssetLoadAndProcessUploads` | VMA assertion / abort | Same transfer teardown abort after upload finalization. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.RegisterAssetLoadWithCompletionCallback` | VMA assertion / abort | Same transfer teardown abort after callback finalization. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.AssetStreamingCompletionSeparatesQueuedUploadedAndFinalizedStages` | VMA assertion / abort | Same transfer teardown abort after streaming finalization. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.ProcessUploadsNoOpWhenEmpty` | VMA assertion / abort | Same transfer teardown abort. | HARDEN-003 / HARDEN-005 |
| `AssetPipelineHeadlessTest.ProcessMainThreadQueueNoOpWhenEmpty` | VMA assertion / abort | Same transfer teardown abort. | HARDEN-003 / HARDEN-005 |

**Likely root cause category:** real implementation or fixture teardown issue involving mapped-state ownership; not classified as purely environment-dependent because it reproduces deterministically on a valid Vulkan device.

### Group 2 — Runtime selection sub-element expectation mismatch

**Failed executable:** `build/ci/bin/IntrinsicTests`.

**Failure type:** deterministic GoogleTest expectation failure.

**Requires GPU / Vulkan / window system:** no direct Vulkan device required by the focused tests; these are CPU-side resolution tests for GPU pick IDs.

**Deterministic across two runs:** yes.

| Test name | Failure details | Owner task |
|---|---|---|
| `RuntimeSelection.ResolveGpuSubElementPick_MeshSurfacePrimitiveDoesNotFallbackToWholeMeshRaycast` | `picked.entity.face_idx` is `0`, expected `1`; `picked.entity.vertex_idx` is `4294967295`, expected `<= 5`. | HARDEN-004 |
| `RuntimeSelection.ResolveGpuSubElementPick_MeshLinePrimitiveRefinesNearestEndpointVertex` | `picked.entity.vertex_idx` is `4294967295`, expected `2`; world `x` is `0.95`, expected `1.0`. | HARDEN-004 |

**Likely root cause category:** selection ID contract or CPU/GPU selection refinement mismatch, not an environment issue.

### Group 3 — Generated `_NOT_BUILT` CTest placeholders

**Failed executable:** missing placeholder executable names.

**Failure type:** CTest `Not Run` / missing executable.

**Requires GPU / Vulkan / window system:** no.

**Deterministic across two runs:** deterministic by construction; directly reproduced once.

| Test name | Missing executable | Owner task |
|---|---|---|
| `ExtrinsicAssetTests_NOT_BUILT` | `ExtrinsicAssetTests_NOT_BUILT` | HARDEN-005 / HARDEN-041 |
| `ExtrinsicCoreTests_NOT_BUILT` | `ExtrinsicCoreTests_NOT_BUILT` | HARDEN-005 / HARDEN-041 |
| `ExtrinsicECSTests_NOT_BUILT` | `ExtrinsicECSTests_NOT_BUILT` | HARDEN-005 / HARDEN-041 |
| `ExtrinsicGraphicsTests_NOT_BUILT` | `ExtrinsicGraphicsTests_NOT_BUILT` | HARDEN-005 / HARDEN-041 |
| `ExtrinsicRuntimeTests_NOT_BUILT` | `ExtrinsicRuntimeTests_NOT_BUILT` | HARDEN-005 / HARDEN-041 |

**Likely root cause category:** stale CMake/test registration, not runtime logic.

### Group 4 — Label taxonomy commands do not select expected tests

**Failed executable:** none; CTest selected zero tests.

**Failure type:** test taxonomy / generated CTest label mismatch.

**Requires GPU / Vulkan / window system:** no.

**Deterministic across two runs:** not repeated; direct observation is sufficient for taxonomy triage.

| Command | Result | Owner task |
|---|---|---|
| `ctest --test-dir build/ci --output-on-failure -L gpu --timeout 60` | `No tests were found!!!` | HARDEN-005 |
| `ctest --test-dir build/ci --output-on-failure -L runtime --timeout 60` | `No tests were found!!!` | HARDEN-005 |
| `ctest --test-dir build/ci --output-on-failure -L graphics --timeout 60` | `No tests were found!!!` | HARDEN-005 |

**Likely root cause category:** labels are absent (`gpu`) or generated as a single space-separated label value rather than independent CTest labels (`runtime`, `graphics` on aggregate-expanded tests).

## Summary

Known failing full-CTest cases from this baseline are grouped as:

1. HARDEN-003: deterministic VMA unmap assertion aborts in transfer/headless Vulkan/asset-pipeline paths.
2. HARDEN-004: deterministic runtime selection sub-element expectation failures.
3. HARDEN-005 / HARDEN-041: generated `_NOT_BUILT` placeholder CTest entries.
4. HARDEN-005: label taxonomy commands currently do not select the intended suites.

The baseline could not complete the exact requested single-threaded full `ctest` command within the terminal limit, and the exact requested configure command is blocked locally by unavailable `clang-20` compiler names. The focused reproductions above are sufficient to make the known failure groups actionable and deterministic for the next hardening tasks.

