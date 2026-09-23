# METHOD-047 atlas-metadata handoff (N1, N3, N4, N7)

- Commit: `6526044366f1a9b4f6adae5e3ae967f1d42dd85d`, parent `1ae11a531`. Worktree `/tmp/intrinsic-method047-atlas-metadata`.
- Patch hash: `sha256(git diff --binary 1ae11a531 HEAD) = d75073ad4b5f88422a9195bbbe5e861435b8e797fbe3dbd00b0d5b85eaf4f22e`.
- Size: 11 files, +860/−48.
- **Nothing was built or run.** Compilation was checked by reading only. The only tools run were the static checks `tools/repo/check_layering.py` (no violations) and `tools/repo/check_test_layout.py` (0 findings).

## Source list (all in the owned set)

- `src/runtime/GeometryIntegration/Runtime.MeshSurfaceTopology.cppm` / `.cpp`: the shared data owner for the extent record.
- `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cppm` / `.cpp`
- `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Uv.cpp`
- `src/runtime/Scene/Runtime.SceneSerialization.cpp`
- `src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp`: one call after `PopulateFromMesh`, plus an import.
- `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp`: one call after `PopulateFromMesh`, plus an import.
- Tests:
  - `tests/contract/runtime/Test.TextureBakeModule.cpp`
  - `tests/contract/runtime/Test.ParameterizationOperations.cpp`
  - `tests/contract/runtime/Test.RuntimeSceneSerialization.cpp`

No other files were needed. I did not touch the PointProperties, MeshReadiness or MeshSupport sources, `Test.SandboxEditorMeshMethods`, Construction, the geometry solver or validator, graphics raster, docs or the app.

## Reuse decisions

- **Extent record:** `MeshUvAtlasExtent` plus helpers now live in `Runtime.MeshSurfaceTopology`, which UV publication and bake already share. It is a plain struct and free functions; no service was added.
- **Shared bounds:** `IsValidMeshUvAtlasExtent` reuses `Geometry::UvAtlas::ValidateUvAtlasOptions`, which caps at 16384, and additionally requires a nonzero extent. There is no new constant.
- **Corner-UV admission (N7):** reuses `Geometry::UvAtlas::ValidateAuthoredUvs`, the same rules as authored vertex UVs: finite, and no degenerate UV or position faces. It runs on a de-indexed copy, so each face owns its corners and seams are representable.
  - I rejected `ValidateUvAtlasCorners` for this. It is the strict generated-atlas acceptance test (overlap, bounds, distortion limits), and it would refuse legitimate artist layouts such as mirrored or overlapping islands.
  - The import path's `GatherAuthoredCornerTexcoords` is private and only checks finiteness.
- **Fingerprint:** `Core::Hash::HashString64` over the UV bytes. The bake module's private `ContentFingerprint` is not shared.

## N1: oversized snapshot is a permanent failure

- The budget is now an `Impl` member, `SourceSnapshotBudget`, still defaulting to 64 MiB.
- **Early admission:** `Prepare` rejects right after triangle resolution, before the UV loops, fingerprints, charts and coverage, when the conservative floor exceeds the budget. The floor is 16 bytes per triangle (three corner indices plus one chart id).
- **Exact admission:** `Schedule` checks `SourceByteCount(prepared) > budget` before looking at queue pressure. Both checks return `BakeFailed` with the diagnostic "…source snapshot needs [at least] N bytes, more than the B-byte bake budget…".
- **Transient versus permanent:** `HasScheduleCapacity` now covers queue pressure only; it still returns `JobSubmitFailed`, which stays transient. `RejectBeforePreparation`'s count and busy checks are unchanged.
- **Appearance:** it records `BakeFailed` once and gates it through the existing `RejectedRevisionToken`. `JobSubmitFailed` remains the only status it treats as transient.
- **Test seam:** `TextureBakeService::SetSourceSnapshotBudgetForTest(std::size_t)`. This is a new public method named in the existing `…ForTest` convention; no status enum was added.

## N4: a failed generated asset is permanent

- `ClassifyGeneratedAsset` returns a new private `Failed` for `AssetState::Failed`. `Busy` now covers only the other non-Ready states.
- Both `RejectBeforePreparation` and the pair classification in `Schedule` return `AssetLoadFailed` with "…failed to load and is not reused; remove this bake output, then bake again". Nothing is reloaded or created.
- I chose not to replace the failed asset in place: its id may still be bound by consumers. Remove, which already destroys both assets, followed by a rebake creates a fresh pair.

## N3: exact atlas extent metadata

**The component.** `MeshUvAtlasExtent{Width, Height, CornerTexcoordRevision?, VertexTexcoordRevision?, TexcoordFingerprint}`.
- It is bound to the canonical UVs, `h:texcoord` and `v:texcoord`, by presence and revision.
- **Important finding:** `PropertyRegistry`'s move constructor and move assignment call `RebaseRevisions()`. So an entt swap-and-pop, for example destroying another mesh entity, silently changes revisions without changing any UV.
  - Stamps alone would therefore drop exact extents spuriously. Stamps are the fast path, and a content fingerprint of both UV properties revalidates.
  - `RefreshMeshUvAtlasExtent`, used by per-frame appearance, re-stamps a record whose content is identical and removes a stale one, so neither is hashed again.
  - `FindCurrentMeshUvAtlasExtent` is the const query, used by serialization and history capture.

**Publication** (`PublishMeshUvAtlasExtent`): it stamps after the UV writes. An extent of 0, an out-of-bounds extent, or a mesh without canonical UVs removes the record.
- **UV regeneration:**
  - A generated result records `AtlasWidth`/`AtlasHeight`. A preserved authored vertex result keeps whatever extent is still current, because its content is unchanged.
  - The commit reports NoChange only when the UVs are identical **and** the extent is unchanged. A change that touches only the extent, with identical UVs, is committed as an undoable entry.
- **History:** `UvPublishedProperties` carries `AtlasExtent`. `ApplyUvPublishedProperties` re-publishes or removes it after writing the UVs, so undo and redo restore the extent together with its UVs.
- **Import:** the direct-mesh and model paths publish the extent only for `GeneratedAtlas` provenance. Authored UVs get no record, so the default of 1024 applies.
- **Scene codec:**
  - It writes `geometrySources.uvAtlasExtent {width, height}` only while the record is current. Stamps and fingerprints are never persisted.
  - On load it validates through the shared bounds and re-binds to the loaded UVs. A malformed extent, one out of bounds, or one without UVs makes the load fail with `InvalidFormat`.

**Appearance.**
- It bakes at the exact extent (`RefreshMeshUvAtlasExtent`) or the 1024 default, with `MaxAdaptiveExtent = 0`.
- `matchesConfig` now also compares `record.Width` and `record.Height`, so a change to the extent alone causes a rebake.
- The failure branch stores the requested width and height, so the retry gate matches.
- Above 8192, `Prepare` fails with `InvalidResolution` before allocating anything. Its diagnostic now names the requested `WxH`.
- It never reads config, so a config value cannot belong to a different mesh or a later draft.
- The explicit bake API keeps its opt-in `MaxAdaptiveExtent`.

## N7: preserving authored corner UVs

- **When it applies:** `PreserveValidAuthoredUvs=true`, `ForceRegenerate=false` and an existing `h:texcoord`.
- **Gathering:** the main thread collects three UVs per soup face through the canonical corner walk. The corner walk and the soup builder both start each face ring at `f:halfedge`, and I checked this in both implementations. Any mismatch leaves the list empty.
- **The check:** the worker, before any guide segmentation, runs:
  - a guide is set → regenerate, with the prefix "…cannot honor the requested region guide";
  - the corners do not map onto the soup → regenerate, with the prefix "…do not map onto the triangulated surface";
  - the corners are valid → `NoChange`, `AuthoredPreserved`, `ActualMethod=Authored`. Nothing is published and no undo entry is created, so seams, labels and the extent all stay.
  - the corners are invalid → regenerate, with the prefix "authored corner UVs were not preserved (<status>); ".
- **Unchanged cases:** shadow `v:texcoord` is never offered while corners exist, which is the M1 contract. The atlas action (`Preserve=false`, `Force=true`) still regenerates.

## Tests added or changed (none run)

**`Test.TextureBakeModule.cpp`**
- **Removed:** `AppearanceBakeAdoptsTheExtentThatResolvesEveryChart`. It asserted the old 1024→2048 escalation.
- `AppearanceBakeUsesTheRecordedAtlasExtentAndNeverEscalates`:
  - A chart that is sub-texel at 1024 with no record → Failed at 1024x1024, with no retry.
  - Publishing 2048 → one resubmission, Pending at 2048², then gated.
- `AppearanceBakeFollowsArbitraryRecordedExtentsOnlyWhileTheUvsAreCurrent`:
  - 1536 is followed.
  - Move-assigning the PropertySet (a revision rebase) keeps the record current, through the fingerprint.
  - Publishing only 512 → rebake at 512.
  - A manual `h:texcoord` edit → the record goes stale → rebake at 1024 and the component is removed.
  - It uses `ForceReady` before rebakes.
- `RecordedExtentAboveTheBakeCapFailsOnceWithoutAllocating`: a 12288 record → Failed, with the extent named in the diagnostic, no texture, the live asset count unchanged, and no retry.
- `SnapshotLargerThanTheWholeBudgetIsAPermanentFailure`:
  - Budget 16 → the floor check gives `BakeFailed`. Budget 40 → the exact check gives `BakeFailed`. No record is created.
  - Appearance: Failed once, and `BakeRequests` stays constant over 3 frames.
  - Restoring 64 MiB → Scheduled.
- `FailedGeneratedAssetIsAPermanentLoadFailureUntilRemoved`: a Failed value asset → `AssetLoadFailed` with a "remove" diagnostic, and the record and coverage ticket are unchanged. Remove then rebake → Scheduled with a new texture.

**`Test.ParameterizationOperations.cpp`**
- `GeneratedAtlasExtentFollowsItsUvsThroughHistory`:
  - 1536 then 512. Undo gives 1536 and redo gives 512.
  - Manually recording 700, then regenerating 512 with identical UVs → an Applied undo entry; undo gives 700.
  - A repeat → NoChange. LSCM then makes the extent stale.
  - Assumption: fast-staged output does not depend on the previous UVs. xatlas hints only apply to the xatlas backend.
- `PreservationKeepsValidCornerUvsAndRegeneratesOnlyWhenItMust`:
  - Valid seamed corners → NoChange/AuthoredPreserved, 0 undo entries, bit-identical corners, no `v:texcoord`, the 777 extent still current.
  - Guide → Applied, Generated, "region guide" in the diagnostic; undo restores the corners and the extent.
  - Force → Applied, Generated; undo.
  - Collapsed face-0 corners → Applied with "not preserved (".

**`Test.RuntimeSceneSerialization.cpp`**
- `GeneratedAtlasExtentRoundTripsAndRebindsToLoadedUvs`:
  - An exact `{width:1536,height:512}` on the wire, rebound current after load.
  - A UV edit → the record is stale and not saved.
  - 0, 20000, −1, a missing height, a string, and an extent on a mesh without UVs all fail closed.

## Limitations and unresolved contracts

1. **Not compiled.** Watch for:
   - designated `UvAtlasOptions{.Resolution, .Padding}`;
   - the `std::uint64_t` lambda return type in `Fingerprint`;
   - `std::optional<MeshUvAtlasExtent>` in the history snapshot;
   - the entt include added to `MeshSurfaceTopology.cppm`.
2. **Revision rebase on move** affects every revision-token consumer, not only this record: the bake's `ObservedRevisionToken`, `RejectedRevisionToken` and extraction tokens. After an unrelated entity is destroyed, those are re-evaluated. This is safe but causes extra work, and it predates this change.
3. The early floor covers only indices and chart ids, so meshes between the floor and the exact size still pay for coverage once. After that the failure is recorded and gated.
4. When a region guide is requested, N7 declines to preserve the authored corners. It does not check whether the authored charts happen to respect the guide regions, as the geometry vertex path does. This is conservative and truthful.
5. Any other UV writer (LSCM, manual edits, simplify, topology ops, entity copy) makes the record stale. Its content then no longer matches, so the 1024 default applies. There is no explicit clearing on those paths; appearance's refresh removes the record lazily.
6. **The direct-mesh import normal bake is unchanged.** It still uses `meshDiagnostics.AtlasWidth` directly. A generated atlas above 8192 fails there with `InvalidResolution` and logs a warning.
7. **Docs are yours (N8).** Two passages are now wrong:
   - `docs/architecture/runtime.md:864-869` still says "Automatic surface-appearance bakes start at 1024 and double the extent…". It should say: exact recorded generated-atlas extent, else 1024; no escalation; above 8192 fails as `InvalidResolution`; plus the persisted `uvAtlasExtent`, the budget as a permanent failure, and a Failed asset needing a remove.
   - `PropertyTextureBakeRequest::MaxAdaptiveExtent` is still documented as an opt-in API.
8. Evidence classes still owed by root: focused build and run of the three test files, full CPU, ASan/UBSan and Vulkan. No Operational or completion claim is made.
