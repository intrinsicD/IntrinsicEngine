# METHOD-047 bake fixes handoff (M3 + L5)

- Commit: `c658cce6345e1350433adf35a93c9ee4d8e6eb7d` (parent `c4e350612`, detached HEAD in `/tmp/intrinsic-method047-bake-fixes`).
- Files (all owned):
  - `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cppm`
  - `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp`
  - `tests/contract/runtime/Test.TextureBakeModule.cpp`
- Nothing was built or run, per instructions. Compilation was checked only by reading the code; details are under "Compile reasoning".

## Public API changes (cppm)

1. `kPropertyTextureBakeMaxExtent = 8192u` is now an exported constant.
   - Prepare uses it for the extent check and in its diagnostic.
   - It stays at 8192 on purpose, not 16384. At 16384², an RGBA8 value texture, an R32F coverage texture and a D32 depth texture together take about 3 GiB.
2. New field `PropertyTextureBakeRequest::MaxAdaptiveExtent{0}`, placed after `Height`.
   - 0 keeps the old behaviour: the bake uses exactly `Width x Height`.
   - Nonzero: while some chart covers no texel centre, both sides double (aspect ratio kept) up to this bound.
   - The record's `Width`/`Height` report the extent actually used.
   - A bound below `max(Width, Height)` or above the cap returns `InvalidResolution`.
3. New field `PropertyTextureBakeRecord::RejectedRevisionToken{0}`, placed last.
   - It is runtime-only.
   - It holds the revision token observed when an automatic producer's request was rejected before scheduling.
   - `RefreshFreshness` never touches it; it only rewrites `ObservedRevisionToken`, which is why this needed its own field.

No enum values were added. `VisualizationEditingOperations.Actions.cpp` has an exhaustive switch over `PropertyTextureBakeStatus`, so a new value would break it.

## M3: automatic appearance extent and retry

- **Prepare** (coverage section):
  - It measures coverage at the requested extent. When `MaxAdaptiveExtent != 0` and a chart is unresolved, or nothing is covered, it doubles the extent and measures again.
  - Overlap fails immediately, whatever the extent.
  - Strict all-chart coverage still holds: a failure at the largest reachable extent reports `UnderresolvedAtlas` or `ZeroCoverageBake` with `"WxH (largest adaptive extent within N)"`.
  - The adopted extent goes to `PreparedPropertyBake::Width/Height`. It is used for the metadata, the record and the `Work` extent. `Work.Request` keeps the original request.
- **ReconcileSurfaceAppearance:**
  - Appearance requests use `MaxAdaptiveExtent = kPropertyTextureBakeMaxExtent`, starting from the default 1024. An atlas that resolves at 1024 still allocates only 1024².
  - Config matching is factored into `matchesConfig`. It compares the same fields as before.
  - A matching `Fresh` record is skipped, as before; `Unknown` never counts as current.
  - A matching record that is `Failed` and has `RejectedRevisionToken == ComputePropertyTextureBakeRevisionToken(record, live lookup)` is skipped. The token check costs O(watched properties), so there is no per-frame Prepare or coverage run.
  - It retries when any of these changes:
    - the atlas: `h:texcoord`/`v:texcoord` revisions;
    - positions or topology;
    - the source property revision;
    - the config: source, kind, encoding, range policy/range, colormap.
  - Extent is not a separate input: it is derived from the atlas, so an atlas change covers it.
  - The failure branch:
    - stores `Storage = EncodedRgba` and `Texcoords = {}`;
    - clears the fingerprints, so `Freshness` becomes `Unknown` and the record never binds;
    - sets `RejectedRevisionToken`;
    - uses `AdvanceGeneration` on both the record and the catalog. The record bump supersedes older in-flight work for the slot.
  - `JobSubmitFailed` is treated as transient: it is not recorded and is retried next frame.
- **`RejectBeforePreparation`** is new and runs at the start of `Schedule`.
  - If the other queued bakes already number `kMaxActivePropertyTextureBakes` or more, it returns `JobSubmitFailed`.
  - If the target record's value or coverage asset is alive but not Ready (still reloading or loading), it returns `JobSubmitFailed` with a "still loading… previous bake is unchanged" diagnostic.
  - Both checks run before any preparation, so these per-frame retries are cheap. Only the byte-capacity saturation case still prepares before it is rejected.

## L5: paired asset transactionality

- `CreateOrReloadAsset` is replaced by:
  - `AssetAlive`;
  - `ClassifyGeneratedAsset`, which returns `Create`, `Reload`, `Busy` or `Foreign`. It predicts `AssetService::Reload`'s own preconditions: Ready state and the same `TypeIdOf<GeneratedPropertyTextureMetadata>`;
  - `ReloadGeneratedAsset`;
  - `CreateGeneratedAsset`.
- Order in `Schedule`:
  1. Classify both existing assets. `Foreign` returns `AssetLoadFailed`; `Busy` returns `JobSubmitFailed`. Nothing is touched.
  2. Create the missing assets: value first, then coverage. If creating coverage fails, the newly created value asset is destroyed.
  3. Reload the value asset in place. On failure, a newly created coverage asset is destroyed. `Reload` restores its own payload on failure, so the previous bake is unchanged.
  4. Reload the coverage asset in place. This can fail only through an internal asset-service error after the precheck passed. The value asset then already carries the new metadata, and its `Reloaded` event moves its GPU slot out of Ready. So the record is marked `Failed`: fingerprints cleared, `Unknown`, generation advanced, work cancelled. A mixed pair is never left looking bindable.
- Ids stay stable across successful rebakes: live assets are always reloaded in place.
- `AssetOwnedByAnotherRecord` now also matches `CoverageTexture`. This stops a caller passing another record's coverage texture as `ExistingGeneratedTexture`.
- **Remove:**
  - It cancels work and then attempts both destroys; the old `||` short-circuit is gone.
  - On partial failure it clears the id of whichever asset was destroyed, marks the record `Failed` with a diagnostic naming the survivor, advances the generations and returns `AssetDestroyFailed`. Calling Remove again retries only the surviving asset.
- **DrainCompletedTransfers:** the old single "completion became stale" message is replaced by specific diagnostics:
  - the source diagnostic;
  - "value output failed on the GPU / is no longer resident / was replaced by a newer GPU generation";
  - "coverage output …; the value texture is not published without it".

## Tests added or changed (`Test.TextureBakeModule.cpp`)

- **Harness:**
  - Captures only the TextureBake `Maintenance` hooks (the hook list is cleared before `TextureBake.OnRegister`).
  - `RunMaintenance()` builds a `RuntimeFrameHookContext`.
  - `Assets()` accessor.
  - New imports: `Extrinsic.Asset.Service`, `Extrinsic.Graphics.Colormap`, `Extrinsic.Graphics.Component.VisualizationConfig`, `Extrinsic.Runtime.FramePacingDiagnostics`.
  - Helpers: `SetTriangleCornerUvs`, `EnableHeatAppearance`, `ForceReady`.
- **`DefaultRequestUses…`:** additionally asserts `MaxAdaptiveExtent == 0`.
- **`AdaptiveExtentDoublesUntilEveryChartResolvesWithinItsBound`:**
  - A chart with no texel centre at 16 covers (24.5, 24.5) at 32.
  - Results: fixed → Underresolved; bound 16 → Underresolved with "within 16"; bound 64 → Scheduled at 32×32 with 2 charts.
  - A bound below the request, a bound above the cap, and an oversized Width all return `InvalidResolution`.
- **`AppearanceBakeAdoptsTheExtentThatResolvesEveryChart`:**
  - The canonical atlas has a chart that is sub-texel at 1024 and covers (1000.5, 1000.5) at 2048.
  - One maintenance pass gives a Pending, Fresh appearance record at 2048² with 2 charts.
  - A second pass submits nothing, checked through `Stats().BakeRequests`.
- **`RejectedAppearanceBakeRetriesOnlyAfterItsDependenciesOrConfigChange`:**
  - With an overlapping atlas: Failed, "non-overlapping", `Unknown`, not bindable.
  - No resubmission over 3 frames, or after an edit to an unwatched `v:unrelated`.
  - A colormap change resubmits once and is then gated again.
  - Republishing a valid atlas resubmits and schedules at 1024 with the new colormap.
- **`RebakeReloadsTheAssetPairInPlaceOrLeavesItUntouched`:**
  - With the coverage asset forced to `QueuedIO`: `JobSubmitFailed`; the record's ids, generation and padding are unchanged; the value payload ticket is unchanged (not reloaded); the live-asset count is unchanged.
  - With both Ready: Scheduled with the same ids; the generation advances; padding becomes 4; both payload tickets change (both reloaded); the live count is unchanged.
  - Remove: both assets are dead and the live count drops by 2.
- **`FailedCoverageCreationDestroysTheNewValueAsset`:**
  - A foreign `uint32_t` payload occupies the first pair's coverage path, serial 2.
  - Result: `AssetLoadFailed` mentioning "coverage", the live count is unchanged (the created value asset was destroyed), and no record is published.
  - This test depends on the deterministic per-service serial path scheme. The comment explains the seam.
- The existing "1 of 2 UV charts" diagnostic assertion still holds.

## Compile reasoning (not built)

- The designated-initializer order was checked against the struct orders, including `.MaxAdaptiveExtent` between `.EncodingColormap` and `.PaddingTexels` in the appearance request.
- The new record field is last, so existing designated initializers are unaffected.
- `Assets::AssetMeta` and `Assets::AssetState` are visible through the interface's `import Extrinsic.Asset.Registry`. `AssetService::TypeIdOf` is a public static template.
- `PayloadTicket` has a defaulted `<=>`, so `==` is available for `EXPECT_EQ`.
- The `ForceAssetState` and `LiveAssetCount` test seams are public on `AssetService`.
- `RuntimeFramePacingDiagnostics` and `EditorInputCaptureSnapshot` are default-constructible.
- Test assumption: generated assets can be forced Ready. `ForceReady` handles both the synchronous pipeline (no scheduler) and the asynchronous one.

## Limitations and mismatches for root

- **No per-entity atlas extent exists.** UV publication (`Runtime.GeometryProcessingOperations.Uv.cpp`, root-owned) records no atlas extent on the entity. So appearance adopts the smallest doubling of 1024 that resolves every chart, not the exact generated atlas resolution.
  - Consequence: an atlas generated at 2048 whose charts all resolve at 1024 bakes appearance at 1024. That is correct, but lower detail.
  - Recommendation: if root wants exact adoption, publish the atlas extent per entity. Appearance could then pass it as `Width`/`Height` and keep `MaxAdaptiveExtent` as a safety net.
- **Atlas and bake caps still disagree.** Geometry `kMaxResolution = 16384` (`Geometry.UvAtlas.cpp:39`) versus bake cap 8192. The Sandbox also silently clamps atlas-extent adoption to 8192 (`Sandbox.PanelSupport.cpp:1545-1547`, and bake width/height at `:752`, `:1027`).
  - Root/app should use `Runtime::kPropertyTextureBakeMaxExtent` there. Either cap the atlas config at it, or report "atlas extent exceeds bake cap" instead of clamping silently.
  - Import callers adopting a 16384 atlas extent get `InvalidResolution`. They could clamp to the cap with `MaxAdaptiveExtent = cap` and get a truthful `UnderresolvedAtlas` if the atlas really needs more.
- **A rebake is rejected while the previous one's assets are still reloading.** Rebaking the same output before the previous reload returns its assets to Ready now yields `JobSubmitFailed` ("still loading"). It used to be `AssetLoadFailed`, and still no mutation happened. The editor maps `JobSubmitFailed` to `InvalidVisualizationProperty`; the diagnostic text explains it.
- **Byte-capacity saturation is re-prepared per frame.** When the 64 MiB source-snapshot cap is saturated, appearance retries still run Prepare each frame until the queue drains, which takes a bounded number of frames. Count saturation and busy assets are rejected before preparation.
- **GPU-side failures are not retried until something changes.** A GPU-side failure (for example, output allocation at large adaptive extents) leaves a `Fresh` and `Failed` record. This is unchanged: it is not retried until a dependency or config changes, and it never binds.
- **Some paths have no automated test.**
  - Why: nothing can make `AssetService::Reload`/`Destroy` fail after the precheck, and GPU completion needs Vulkan.
  - Untested: the partial-reload branch, the partial-destroy branch of Remove, and the new completion diagnostics. Each was reviewed by reading only.
- **Docs are not updated** (root owns them). `docs/architecture/runtime.md` around lines 859-898 should say:
  - appearance bakes use adaptive extent from 1024 up to `kPropertyTextureBakeMaxExtent`;
  - rejected automatic bakes are gated by dependency revisions;
  - value/coverage pairs are admitted together.
