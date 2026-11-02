# Bilateral point filtering

The filter is a plugin (`plugins/bilateral_filter`, module `Intrinsic.Plugin.BilateralFilter`, [ARCH-022](engine-and-feature-modules.md)). **View → Processing → Filtering & Smoothing → Bilateral Point Filter** filters a named position property using a count-matched normal property on the same canonical domain. The default writes `filtered_positions`; choosing the input positions as output is an undoable geometry update. The window and the MCP tool run the same applied config through the same path. The mesh, graph and point-cloud Processing menus no longer list it: their entries were Sandbox redirects, which a plugin cannot register.

## Formulation

This preserves the engine's existing fixed-normal point filter. For each point, query `min(n,k+1)` candidates in squared-distance/source-ID order, then discard self by ID. Distinct coincident samples remain eligible, including boundary ties that omit self. The position update is simultaneous and along the normalized input normal, weighted by a spatial Gaussian and a Gaussian of `1-abs(dot(normal_i,normal_j))`. Normals remain fixed across passes. Neighborhoods are rebuilt after every position update.

Automatic spatial sigma is twice the sampled nearest-other spacing, using at most 500 samples at deterministic stride `floor(n/sampleCount)`, resolved once before filtering. Zero sampled spacing uses sigma 0.01 in input-property coordinate units. Normal sigma is floored to 1e-6. Normals shorter than 1e-8 leave their point unchanged; total weight <=1e-12 gives zero displacement. Diagnostics describe the final pass. Zero iterations copies the input and reports no filtered points.

[Fleishman, Drori and Cohen-Or (2003)](https://www.cs.tau.ac.il/~dcor/articles/2003/Bilateral-Mesh-Denoising.pdf) uses offset similarity; [Zheng et al. (2011)](https://doi.org/10.1109/TVCG.2010.264) filters face normals and reconstructs positions. This point-set normal-dot kernel is distinct from those complete methods and Framework24's mesh-normal filtering variants. Their convergence obligations remain open.

## Geometry and runtime

`BilateralFilter` accepts finite float3 position/normal spans and returns owned positions plus diagnostics. `BilateralFilterStepFromNeighbors` consumes one pass of packed candidate IDs and requires a positive resolved spatial sigma. Callers guarantee nearest membership; the reducer checks row width, IDs, deterministic order and finite arithmetic. The Cloud wrapper delegates and publishes only after all passes succeed. Invalid input or unrepresentable float weights/distances/updates return failure without partial mutation.

The plugin owns the config section, the tools, the window and the run: CPU passes, or the Vulkan chain below, as plugin jobs (`SubmitPluginJob`) of the Sandbox shell's editor workspace, so every run is a cancellable row of its Jobs window. The host service `PointCloudEditingService` (`src/runtime/Services/Runtime.PointCloudEditing.cppm`) does what needs the editor: it resolves all eight canonical domains, compacts live rows (paired edge deletion for halfedges), hands out the cached CPU index and pages GPU kNN rows, and publishes. Existing deleted output rows are preserved; a newly named output initializes deleted rows from the input. Unrelated properties and topology remain intact. Original positions, normals, deletion and output revisions guard publication. The undo entry is host code holding host data, so undo and redo work after the plugin is unloaded or reloaded. The output is guarded by the history slot every author of that property shares (as a GPU position accept does), so successive runs, and other commands writing the same property, undo in turn; inputs keep their own revision guards, and later user edits reject stale history. Several runs may go at once on different outputs; the host refuses a second run on the same output. A finished run releases its copies, rows, index lease and GPU workspace.

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

The section keeps its name and schema id from the built-in filter, so saved configs load: the Sandbox stages the plugin before the config boot and moves its section into the boot registry (`--no-startup-plugins` skips it). Shared builds load `plugins/libIntrinsicBilateralFilterPlugin.so` beside the executable and can reload it; static builds link it and register it in-process. The window's **Apply settings** and `config_apply` write the section; **Run** and the MCP tool `bilateral_filter` run its applied settings. `bilateral_filter` waits for the run and answers once with `status` `applied`, `invalid`, `failed`, `stale` or `cancelled`; MCP cancellation cancels the run. `bilateral_filter_preview` answers whether a run can start, and why not. Results report requested/actual backend, live/total/written rows, completed passes, resolved sigma, final-pass displacement and degenerate-normal counts, first-index reuse, private builds, GPU batches and CPU/GPU elapsed times. The tool sends no progress notifications.

## Verification entry points

`Test.BilateralPointFilter.cpp` covers analytic simultaneous updates, independent exhaustive neighborhoods, multiple moving passes, ties, degenerate normals and invalid inputs. `Test.BilateralFilterPlugin.cpp` runs the plugin as the Sandbox composes it: the section, the MCP tool and its Jobs window row, all domains with copy/in-place history and agreeing CPU backends, stale and cancelled runs, preflight, and the window's Run button. `Test.PointCloudEditing.cpp` covers the host service. `BilateralFilterPluginGpuSmoke` (Vulkan) checks three passes against the CPU LBVH run with queries paged 16 at a time, the k<=63 refusal, inputs changed before GPU recording (stale) and a cancel between stages (cancelled), writing nothing in either. `plugins/bilateral_filter/live_driver.py` drives the plugin in the running Sandbox over MCP: an independent float64 reference of the kernel (one and three passes), the three backends against each other, Vulkan queries paged 64 at a time, MCP cancellation, and undo/redo while the plugin is unloaded and after it is loaded again. The built-in filter's eight-domain Vulkan smoke (k=63 in place) is not ported, so `geometry.point_lbvh.bilateral_runtime_smoke` and `INTRINSIC_BILATERAL_BENCHMARK_OUTPUT` have no producer until a plugin benchmark replaces it ([ARCH-022](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md)). [RUNTIME-224](../../tasks/done/RUNTIME-224-bilateral-point-filter-spatial-backends.md) tracks verification and terminal review.
