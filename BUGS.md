# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

---

## Active — Core / Scheduler / Telemetry

### 1. Benchmark warmup frames are recorded off-by-one

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='Benchmark_Runner.WarmupFramesSkipped:Benchmark_Runner.CompletesAfterConfiguredFrames'`
  - `WarmupFramesSkipped` records `1` frame instead of `0`.
  - `CompletesAfterConfiguredFrames` records `FrameCount + 1` samples.
- **Affected symbols:**
  - `src/Core/Core.Benchmark.cpp`
  - `Core::Benchmark::BenchmarkRunner::RecordFrame()`
  - `Core::Benchmark::BenchmarkRunner::IsWarmingUp()`
- **Root-cause hypothesis:**
  - `RecordFrame()` increments `m_FrameIndex` before checking whether the current frame is still in the warmup window, so the last warmup frame is accidentally committed as a real sample.
- **Robust fix plan:**
  1. Treat the current frame index as a half-open interval: warmup frames are `[0, WarmupFrames)`, recorded frames are `[WarmupFrames, WarmupFrames + FrameCount)`.
  2. Reorder `RecordFrame()` so warmup classification is computed **before** incrementing the state used by `FramesRecorded()` / `IsComplete()`.
  3. Add boundary tests for:
	 - `WarmupFrames = 0`
	 - `WarmupFrames = 1`
	 - `FrameCount = 1`
	 - exact completion at `WarmupFrames + FrameCount`
  4. Keep JSON emission and percentile statistics based solely on `m_Snapshots.size()` so statistics cannot drift from the actual recorded sample count.

### 2. `CoreTasks.StaleWaitTokenUnparkDoesNotResumeNewWaiters` is flaky under full-suite pressure

- **Repro:**
  - Present in the full `IntrinsicTests` run.
  - Often passes when rerun in isolation.
- **Affected symbols:**
  - `src/Core/Core.Tasks.cpp`
  - `Core::Tasks::Scheduler::UnparkReady()`
  - `Core::Tasks::Scheduler::ReleaseWaitToken()`
  - `Core::Tasks::CounterEvent`
- **Root-cause hypothesis:**
  - A stale wait token generation is supposed to be rejected deterministically, but a timing-sensitive interaction between `ready`, queued continuations, and token reuse may still permit observable interference under contention.
- **Robust fix plan:**
  1. Audit the wait-token state machine explicitly: `Acquire -> park -> ready -> drain -> release -> generation++`.
  2. Add invariants asserting that `UnparkReady()` never touches continuations when `generation` mismatches.
  3. Add a randomized stress test that repeatedly recycles tokens across many threads and validates that stale tokens cannot wake a new waiter.
  4. If the race is confirmed, collapse token readiness and waiter list ownership behind a single generation-checked transition rather than loosely coupled flags.

---

## Active — Geometry Correctness

### 3. Mean curvature sign is inverted on convex closed meshes

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='Curvature_MeanCurvature.Tetrahedron_UniformPositiveCurvature:Curvature_Full.Icosahedron_SymmetricCurvatureValues'`
  - Convex closed meshes report negative mean curvature.
- **Affected symbols:**
  - `src/Runtime/Geometry/Geometry.Curvature.cpp`
  - `Geometry::Curvature::ComputeMeanCurvature()`
  - `Geometry::Curvature::ComputeCurvature()`
- **Root-cause hypothesis:**
  - The code correctly computes a cotan Laplace-Beltrami-like vector, but the sign convention is flipped when converting `Δx` into signed mean curvature against the outward vertex normal.
- **Robust fix plan:**
  1. Re-derive the discrete convention used in the implementation and document it inline using the current operator definition: either
	 - `Δx = -2 H n`, or
	 - `Δx = +2 H n`,
	 but not a mixture of both.
  2. Make both `ComputeMeanCurvature()` and `ComputeCurvature()` share a single helper for signed mean-curvature extraction so the convention cannot drift again.
  3. Extend tests beyond tetrahedron / icosahedron with:
	 - sphere-like closed meshes (positive mean curvature),
	 - planar interior vertices (near zero),
	 - flipped orientation meshes (sign should flip consistently if orientation is reversed).
  4. Verify principal curvature reconstruction still satisfies `H = (k1 + k2) / 2` after the sign correction.

### 4. GJK returns false negatives for overlapping convex pairs

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='GJK.*'`
  - Fails for overlapping sphere/sphere, touching sphere/sphere, concentric spheres, overlapping AABB/AABB, and simplex recovery.
- **Affected symbols:**
  - `src/Runtime/Geometry/Geometry.GJK.cppm`
  - `Geometry::Internal::GJK_Boolean()`
  - `Geometry::Internal::GJK_Intersection()`
  - `Geometry::Internal::NextSimplex()`
- **Root-cause hypothesis:**
  - The simplex evolution is not robust to degenerate directions / duplicate support points, causing the algorithm to exit with `false` before proving containment of the origin.
- **Robust fix plan:**
  1. Rework simplex handling around the canonical GJK invariant: after inserting the newest support point `A`, search only the Voronoi region that can still contain the origin.
  2. Detect no-progress states explicitly:
	 - duplicate support points,
	 - near-zero search direction,
	 - collinear / coplanar simplex collapse.
  3. Treat exact touching (`dot(support, direction) == 0` within epsilon) as overlap rather than immediate separation.
  4. Add shape-pair regression coverage for:
	 - overlap,
	 - touching,
	 - concentric containment,
	 - nearly coplanar / nearly collinear configurations,
	 - deterministic iteration-limit behavior.
  5. Once GJK is corrected, validate `Geometry::ContactManifold` callers that depend on `GJK_Intersection()` simplex recovery.

---

## Active — Test Expectations / Contract Drift

### 5. `HalfedgeMesh_EdgeExtraction.TwoTriangles_SharedEdgeAppearsOnce` no longer matches the builder it uses

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='HalfedgeMesh_EdgeExtraction.TwoTriangles_SharedEdgeAppearsOnce'`
  - Expected `5` unique edges, actual `7`.
- **Affected symbols:**
  - `tests/Test_HalfedgeMeshPropertyAccess.cpp`
  - `tests/TestMeshBuilders.h` (`MakeQuadPair()`)
- **Root-cause hypothesis:**
  - The test still assumes a two-triangle mesh, but `MakeQuadPair()` now builds two quads sharing one edge, which correctly has `7` unique edges.
- **Robust fix plan:**
  1. Rename or rewrite the test so the topology under test is explicit.
  2. If a two-triangle case is still desired, switch the test to `MakeTwoTriangleDiamond()`.
  3. Keep a separate quad-pair regression test to preserve polygon-face coverage.

### 6. Default pipeline recipe test is stale with current PrimitiveID MRT picking

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='RenderResources.DefaultPipelineRecipeAllocatesOnlyRequiredCanonicalTargets'`
  - Test expects `PrimitiveId == false`, code produces `true` when picking is enabled.
- **Affected symbols:**
  - `tests/Test_RuntimeGraphics.cpp`
  - `src/Runtime/Graphics/Pipelines/Graphics.Pipelines.cpp`
  - `BuildDefaultPipelineRecipe()`
- **Root-cause hypothesis:**
  - The test predates the current GPU picking contract where `EntityId` and `PrimitiveId` are emitted together via MRT.
- **Robust fix plan:**
  1. Update the test expectation to match the architecture contract.
  2. Add a nearby test that verifies `PrimitiveId` remains optional only when picking is disabled.
  3. Cross-check the recipe expectations against `CLAUDE.md` so documentation and tests stay synchronized.

### 7. Push-constant size tests are stale after adding per-face and per-vertex attribute pointers

- **Repro:**
  - `./cmake-build-debug/bin/IntrinsicTests --gtest_filter='BDA_PerFaceAttr.MeshPushConstantsSizeUnchanged:PerFaceAttr_*'`
  - Tests expect `sizeof(RHI::MeshPushConstants) == 104`, actual is `112`.
- **Affected symbols:**
  - `tests/Test_BDASharedBufferContract.cpp`
  - `tests/Test_PerFaceAttributes.cpp`
  - `src/Runtime/RHI/RHI.Types.cppm`
  - `RHI::MeshPushConstants`
- **Root-cause hypothesis:**
  - `PtrFaceAttr` and `PtrVertexAttr` extended the struct, but tests still enforce the older layout.
- **Robust fix plan:**
  1. Replace the stale magic number with assertions tied to the current contract:
	 - total size,
	 - alignment,
	 - offsets of critical fields if ABI-sensitive.
  2. Add a guard that the range still fits within Vulkan `maxPushConstantsSize` assumptions used by the pipelines.
  3. Prefer explicit `static_assert`s in engine code for ABI-sensitive layouts so tests fail at compile time if the contract changes again.

---

## Active — Test Harness / Robustness

### 8. ImGui unit-test fixtures abort because the font atlas is not built before `ImGui::NewFrame()`

- **Repro:**
  - Full `IntrinsicTests` run aborts in `imgui_draw.cpp` with
	`atlas->TexIsBuilt` assertion.
- **Affected symbols:**
  - `tests/Test_PanelRegistration.cpp`
  - `tests/Test_TransformGizmo.cpp`
  - `tests/Test_SelectionGizmoRegression.cpp`
- **Root-cause hypothesis:**
  - Test fixtures create an ImGui context and call `ImGui::NewFrame()` without first forcing the font atlas build in a backend-less test environment.
- **Robust fix plan:**
  1. Introduce a shared ImGui test fixture helper that:
	 - creates the context,
	 - sets `DisplaySize`,
	 - forces `io.Fonts->GetTexDataAsRGBA32(...)`,
	 - then begins the frame.
  2. Reuse that helper across all UI tests to eliminate duplicated fragile setup.
  3. Add a dedicated smoke test that ensures the helper can enter and exit a frame without a renderer backend.

### 9. Per-face color test helper is UB under NaN input

- **Repro:**
  - Full suite under UBSan reports:
	`nan is outside the range of representable values of type 'unsigned char'`
	from `tests/Test_PerFaceAttributes.cpp`.
- **Affected symbols:**
  - `tests/Test_PerFaceAttributes.cpp`
  - local `PackColorF()` helper in the test
- **Root-cause hypothesis:**
  - The helper clamps finite values to `[0, 1]` but does not sanitize `NaN` before casting to `uint8_t`.
- **Robust fix plan:**
  1. Sanitize non-finite inputs explicitly before conversion (`NaN`, `+/-inf` -> deterministic fallback).
  2. Align the helper with the engine-side GPU color packing policy so tests exercise the same contract.
  3. Add explicit NaN/Inf regression tests rather than relying on UBSan discovery.

### 10. Some geometry tests use brittle floating-point tolerances

- **Repro:**
  - `MeshUtils_CotanLaplacian.EquilateralTriangleSymmetry` fails by ~`6.7e-9` against a tolerance of `1e-10`.
  - `Subdivision.IcosahedronConvergesToSphere` misses the threshold by ~`0.0016`.
- **Affected symbols:**
  - `tests/Test_GeometryProcessing.cpp`
  - `tests/Test_Subdivision.cpp`
- **Root-cause hypothesis:**
  - These tests assert near machine-precision equality on float-based geometry operators whose results legitimately vary slightly with compiler, optimizer, and math library details.
- **Robust fix plan:**
  1. Relax tolerances to physically meaningful thresholds tied to algorithm scale rather than raw machine epsilon.
  2. Prefer relative error or symmetry-spread checks over exact pairwise equality for float-heavy operators.
  3. Keep one strict deterministic baseline only where the implementation is purely combinatorial.

---

## Verified / Closed

No active picker index bugs tracked.

- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
