# Bilateral point filtering

**View → Bilateral Point Filter**, also available from mesh, graph and point-cloud Processing menus, filters a named position property using a count-matched normal property on the same canonical domain. The default writes `filtered_positions`. **Write to input positions** selects the input property as output for an undoable geometry update. Both choices use the same validated config and execution path.

## Formulation

This preserves the engine's existing fixed-normal point filter. For each point, query `min(n,k+1)` candidates in squared-distance/source-ID order, then discard self by ID. Distinct coincident samples remain eligible, including boundary ties that omit self. The position update is simultaneous and along the normalized input normal, weighted by a spatial Gaussian and a Gaussian of `1-abs(dot(normal_i,normal_j))`. Normals remain fixed across passes. Neighborhoods are rebuilt after every position update.

Automatic spatial sigma is twice the sampled nearest-other spacing, using at most 500 samples at deterministic stride `floor(n/sampleCount)`, resolved once before filtering. Zero sampled spacing uses sigma 0.01 in input-property coordinate units. Normal sigma is floored to 1e-6. Normals shorter than 1e-8 leave their point unchanged; total weight <=1e-12 gives zero displacement. Diagnostics describe the final pass. Zero iterations copies the input and reports no filtered points.

[Fleishman, Drori and Cohen-Or (2003)](https://www.cs.tau.ac.il/~dcor/articles/2003/Bilateral-Mesh-Denoising.pdf) uses offset similarity; [Zheng et al. (2011)](https://doi.org/10.1109/TVCG.2010.264) filters face normals and reconstructs positions. This point-set normal-dot kernel is distinct from those complete methods and Framework24's mesh-normal filtering variants. Their convergence obligations remain open.

## Geometry and runtime

`BilateralFilter` accepts finite float3 position/normal spans and returns owned positions plus diagnostics. `BilateralFilterStepFromNeighbors` consumes one pass of packed candidate IDs and requires a positive resolved spatial sigma. Callers guarantee nearest membership; the reducer checks row width, IDs, deterministic order and finite arithmetic. The Cloud wrapper delegates and publishes only after all passes succeed. Invalid input or unrepresentable float weights/distances/updates return failure without partial mutation.

Runtime resolves all eight canonical domains, compacts live rows and uses paired edge deletion for halfedges. Existing deleted output rows are preserved; a newly named output initializes deleted rows from the input. Unrelated properties and topology remain intact. Original positions, normals, deletion and output revisions guard final publication. In-place undo/redo guards the new position revision while retaining normal/deletion guards; later user edits reject stale history.

- `cpu_octree` uses the reference octree and CPU updates.
- `cpu_lbvh` reuses the selected entity's first-pass snapshot and builds private CPU indices for later passes.
- `vulkan_lbvh` chains framed neighborhood jobs and CPU update jobs. Later passes use `SpatialIndexCache::CreateWorkspace` over private positions. The entity changes only at terminal publication. There is no silent backend fallback.

A working-set snapshot lease keeps its cache handle valid. Releasing the last caller lease expires the handle; pending batches retain GPU resources until safe completion. Workspaces use identity row IDs, while canonical entity indices retain original source IDs. This extends the existing runtime cache rather than adding another service or ECS component. It does not implement refitting or promise reuse of index allocations between passes.

GPU k is 0..63 (candidate capacity k+1), live count <=2^20 and batch size 1..16384. CPU LBVH supports <=2^24 live rows. LBVH coordinates stay within +/-1e18 on every pass. CPU supplied-neighbor memory is O(n min(n,k+1)); GPU transient query buffers are also bounded by the batch size. Large CPU k can consume substantial memory. Scale calculation and all weights/updates run on CPU.

## Config and diagnostics

Section `sandbox.bilateral_filter`, schema `intrinsic.runtime.sandbox.bilateral_filter`, version 1:

```json
{
  "entity": 0,
  "backend": "cpu_octree",
  "positions": {"domain":"unknown", "name":"v:position", "kind":"vec3"},
  "normals": {"domain":"unknown", "name":"v:normal", "kind":"vec3"},
  "output": {"domain":"unknown", "name":"filtered_positions", "kind":"vec3"},
  "k_neighbors": 15,
  "spatial_sigma": 0,
  "normal_sigma": 0.25,
  "iterations": 1,
  "gpu_query_batch_size": 4096
}
```

Iterations are bounded to 0..100 in runtime config. Unknown domain resolves to the primary point domain. Output may equal positions but cannot overwrite a distinct normal input, topology or deletion property. Existing outputs must be count-matched vec3 values. Input discovery lists compatible vec3 properties without requiring a default normal name; full execution preflight validates both selected slots.

`ApplyEditorBilateralFilterConfig` and `ApplyEditorConfiguredBilateralFilter` are shared by UI and config/agent callers. Results report requested/actual backend, live/total/written rows, completed passes, resolved sigma, final-pass displacement and degenerate-normal counts, first-index reuse, private builds, GPU batches and CPU/GPU elapsed times.

## Verification entry points

`Test.BilateralPointFilter.cpp` covers analytic simultaneous updates, independent exhaustive neighborhoods, multiple moving passes, ties, degenerate normals and invalid inputs. Runtime tests cover all domains, copy/in-place history, config and stale/cancelled jobs. `PointLBVHGpuSmoke.BilateralPublishesMovingPassesAcrossDomains` compares three actual Vulkan passes with the octree reference, including k=63 in-place publication, intermediate isolation and undo/redo. `PointLBVHGpuSmoke.BilateralRejectsStaleAndCancelledPasses` separately exercises cancellation, stale inputs and dense coincidences under the same per-test timeout.

`geometry.point_lbvh.bilateral_runtime_smoke` times eight-domain requests through publication. `INTRINSIC_BILATERAL_BENCHMARK_OUTPUT` writes raw JSON for the canonical result tooling. The small Debug fixture is diagnostic; summed per-request elapsed times overlap and no performance improvement is asserted. [RUNTIME-224](../../tasks/done/RUNTIME-224-bilateral-point-filter-spatial-backends.md) tracks verification and terminal review.
