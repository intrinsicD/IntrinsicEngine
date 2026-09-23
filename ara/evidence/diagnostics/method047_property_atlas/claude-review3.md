# METHOD-047 independent review 3 — findings

- Reviewer: Claude Opus 5.5 (`claude-opus-5-5`). Read-only: no builds, test runs, source edits, commits or agents.
- Revision under review: `ad532bf392e0cc2bc26129dd77a074af9b6e6180`, worktree `/tmp/intrinsic-method047-review3` (clean).
  - Primary delta: `git diff d32aa779c ad532bf39`, 26 files, +1049/−107. It includes the atlas-metadata patch (N1/N3/N4/N7) and the root's N2/N5/N8/N9/N10 and ring-cache changes.
- Read:
  - review2-findings.md, atlas-metadata-handoff.md, and the whole delta;
  - the changed owners in context: `MeshUvAtlasExtent` and its helpers, and `PropertyRegistry` revision semantics (`Geometry.Properties.cpp:26-122`, `.cppm:116-130`);
  - the UV worker, commit, history apply and preview/apply admission;
  - TextureBake `Prepare`, `Schedule`, `RejectBeforePreparation`, `ClassifyGeneratedAsset` and `ReconcileSurfaceAppearance`;
  - the scene codec, the import publish sites and Construction;
  - `ResolveTexcoordBinding`, the parameterization UV-state owner, the ring walk/cache, the validator region union, the corpus script, and all new or changed tests.
- Logs consulted, not re-run:
  - `reconstruction-focused-2.log` records the old exact-iso fixture failing, which the root replaced.
  - `reconstruction-scale.gdb` / `reconstruction-scale-gdb2.log` show `atlasUnavailable = true` and 48 faces with a 1e8 x-scale injected into `SourceMatrix`. That is the same path the new fixture drives through `Transform::Component::Scale` (`Construction.cpp:97-108, 436`).

## Verdict

**Code-review clean pending execution, with one Medium correction recommended before merge (R3-1).** I found no blocker in state publication, rollback, lifetime, layering or the review2 dispositions.

- R3-1 is a history-consistency defect in the new extent owner. It is small to fix. It does not corrupt data, and the bake fails visibly, but it breaks the stated "undo restores the extent with its UVs" contract in a common editor sequence.
- Every gate remains **pending and owned by the root**: focused runs of the new fixtures, full CPU, ASan, UBSan and the opt-in Vulkan thin-chart readback. Nothing here is evidence that any of them passed.

## Review2 findings: disposition at `ad532bf39`

| # | Disposition | Evidence |
|---|---|---|
| N1 oversized snapshot | **Fixed.** | The floor check (`TextureBakeModule.cpp:1408-1418`, 16 B/triangle ≤ exact `SourceByteCount`, because `prepared.SurfaceIndices = geometry.SurfaceIndices` at `:1655`) and the exact check (`:2141-2146`) both return `BakeFailed`. `HasScheduleCapacity` is queue-only, and its unsigned subtraction is safe because the candidate is already ≤ budget. Appearance records the failure once, gated by the token. |
| N2 reconstruction | **Fixed.** | `Construction.cpp:455-462` uses `Optional`, and the message states that the atlas is unavailable. No geometry validator was weakened. The new fixture is logically sound and matches the root's gdb probe. Execution is pending. |
| N3 extent | **Fixed**, with the defect R3-1. | Appearance bakes at the exact recorded extent or at 1024 (`:3287-3290`), with no escalation. `matchesConfig` includes the extent (`:3293`), and the failure branch stores it (`:3364-3365`), so the retry gate is exact. Over 8192 gives `InvalidResolution` before any allocation (`:1353-1361`). |
| N4 failed asset | **Fixed.** | `Failed` is classified separately (`:1982-1990`). `AssetLoadFailed` comes both before preparation (`:2063-2067`) and in the pair classification (`:2253-2257`). Appearance gates it. Remove then rebake recovers. |
| N5 GPU thin chart | **Test added. Evidence pending.** | `Test.PropertyTextureBakeGpuSmoke.cpp:349-386` uses the real packer output, and its assertions are correct: ids 1 and 2 follow triangle order, and values are checked on every covered texel. Parity holds only on the tested device. |
| N7 corner preservation | **Fixed.** | See "Verified" below. |
| N8 docs | **Fixed.** | `runtime.md:864-872`, `README.md:1767-1782`, `property_guided_atlas.md:58-59, 126`. |
| N9 validator union | **Fixed.** | A sorted flat `(edge, region, face)` vector is equivalent: every face with the same `(edge, region)` is adjacent after the sort, and the chain union connects them all. |
| N10 corpus binding | **Tool fixed.** | `check_property_atlas.py:111-131` retains the tracked diff, untracked additions and `source-provenance.json`. The corpus has not been re-run at this revision. |
| L1 tab id nit | **Fixed.** | `"###bake" + tab.Name` (`MethodPanels.cpp:3616`). |

## New findings

### R3-1 (Medium). Appearance's lazy removal of a stale extent makes undo of any non-regeneration UV edit lose the extent permanently

**Anchor:** `src/runtime/GeometryIntegration/Runtime.MeshSurfaceTopology.cpp:838-842`. `RefreshMeshUvAtlasExtent` calls `registry.remove<MeshUvAtlasExtent>` as soon as the content fingerprint differs. It is called every frame for every appearance entity (`TextureBakeModule.cpp:3287-3288`).

**Reasoning:**
- Only the UV-regeneration history snapshot carries the extent (`Uv.cpp:369-371, 532-534, 614-615`).
- Other undoable UV writers restore their UVs bit-exactly but know nothing about the extent. One example is parameterization (`ParameterizationOperations.cpp:66-75, 781-795`). Its default config writes `v:texcoord` and retires `h:texcoord` (`FeatureConfigCodecs.Detail.cpp:3027`).
- The fingerprint design would revive the record after such an undo, because the content matches again. But appearance has already deleted the record.
- So the result depends on whether a per-frame producer ran between the edit and the undo. In the editor, with appearance on, it always has.

**Reproducer**, as an extension of `ParameterizationOperations.GeneratedAtlasExtentFollowsItsUvsThroughHistory` (`Test.ParameterizationOperations.cpp:1696-1698`):

```cpp
ASSERT_TRUE(Apply(harness, Runtime::ParameterizationStrategyKind::Lscm).Succeeded());
EXPECT_EQ(extent(), glm::uvec2(0u));
(void)Runtime::RefreshMeshUvAtlasExtent(raw, harness.Entity); // one appearance frame
ASSERT_TRUE(harness.History.Undo().Succeeded());               // UVs restored bit-exactly
EXPECT_EQ(extent(), glm::uvec2(512u));                         // fails today: record was removed
```

Without the `Refresh` line, the same test passes, because the fingerprint re-matches.

**User impact:**
1. Regenerate at 2048 with heat appearance enabled.
2. Run LSCM.
3. Undo.

Appearance now bakes the restored 2048 atlas at 1024. Sub-texel charts give a recorded `UnderresolvedAtlas`. A 512 atlas has its anchors on texel boundaries at 1024, which is review2's grid mismatch. So a bake that worked before the edit fails after the undo. A scene saved afterwards also drops `uvAtlasExtent`.

**Smallest correction.** Do not delete. Cache the verdict for the last observed stamps instead:
- Add a runtime-only `bool StampsMatchContent{true}` to `MeshUvAtlasExtent`.
- In `RefreshMeshUvAtlasExtent`:
  - If the binding is absent, return `nullopt` and keep the record. Optionally clear the stamps to `nullopt` so the next appearance of the UVs forces a hash.
  - If the stamps match, return the record when `StampsMatchContent` is set, otherwise `nullopt`.
  - Otherwise re-stamp, set `StampsMatchContent = (Fingerprint() == TexcoordFingerprint)`, and return according to that flag.
- In `FindCurrentMeshUvAtlasExtent`, apply the same rule without writing.
- `Publish` sets the flag to true.

This keeps the per-frame cost at O(1), because hashing happens once per UV revision change, the same as today. It also makes the extent a pure function of the UV content.

`Test.TextureBakeModule.cpp:1054` (`"the stale record is dropped"`) must then assert `!FindCurrentMeshUvAtlasExtent(...)` instead of component absence.

**Secondary (Low; same owner; optional in the same change).** `CanonicalTexcoordBinding::Fingerprint` (`MeshSurfaceTopology.cpp:702-720`) hashes `v:texcoord` even while `h:texcoord` exists. The bake binds only corner-over-vertex (`TextureBakeModule.cpp:459-472`).
- **Effect:** a write to a shadow `v:texcoord` that leaves `h:texcoord` in place invalidates an extent whose baked UVs did not change. Parameterization with `CornerTexcoordsToRetire` unset does exactly that. The 1024 default then applies to an atlas generated at another resolution.
- **Fix:** hash only the canonical property, with a domain tag, for example `hash(h) ^ kCornerTag` when `h` is complete, else `hash(v)`.
- The revision stamps can stay as they are. A shadow write then costs one re-hash and keeps the record current.

## Verified with no defect found

**Revision stamps.** `FindPropertyRevision` goes through `storage->Revision()`, which calls `Observe()`. That opens a new epoch, and the next write draws a fresh value from the global monotone counter (`Geometry.Properties.cpp:23-46`).
- A stamp taken in `Publish` after the writes therefore changes on any later write.
- A rebase on move gives new values. That is where the fingerprint path takes over. It is required, because entt pool growth moves every `PropertySet`, not only swap-and-pop.
- Stamps cannot collide across edits.

**`IsValidMeshUvAtlasExtent`.** It passes `Padding = 0`: `2·0 < resolution` holds, and every other default is valid (`Geometry.UvAtlas.cpp:2151-2196`). The bounds are [1, 16384].

**UV commit and history** (`Uv.cpp:1176-1218, 597-773`):
- `afterExtent` is the generated `AtlasWidth×AtlasHeight` for `Generated`. For fast-staged output that is the same `resolution` passed to `AlignChartTexelCenters`, at `Geometry.UvAtlas.cpp:1533, 1550`. xatlas gives `atlas->width/height`. In every other case `afterExtent` is the current extent.
- A result is `NoChange` only when both the UVs and the extent are the same. An extent-only change is committed.
- The before-state takes `FindCurrent…` at commit time, after the staleness guards. `ApplyUvPublishedProperties` re-stamps after the writes, in both the history and the history-less paths.
- The extent does not need to take part in the history validate lambda, because it derives from the checked UVs.

**N7 valid-corner NoChange path** (`Uv.cpp:967-1001`):
- The worker's early `NoChange` leaves `AfterMesh` empty, but `CommitUvRegenerationCpuJobResult` returns before touching it, because `Succeeded()` is `Applied` only (`ParameterizationOperations.cppm:136-139`). Nothing is published and no history entry is made.
- The async path returns `Err` from publish, exactly as the existing identical-output `NoChange` does. The sink still receives the truthful result.
- The corners are gathered on the main thread and validated on the worker from state the worker owns.
- `GatherSoupCornerTexcoords` fails closed on any ring/index mismatch or out-of-range halfedge. The de-indexed validation has `3F` positions for `3F` corners, so the counts match.
- A guided request regenerates, with the prefix explained. An invalid, collapsed face regenerates, with the status in the diagnostic. The diagnostic prefix is kept on both the success and the failure branches.
- Nit, not required: duplicate faces with an identical ordered vertex ring would pass the ring check even if the soup and corner-walk face order diverged. Comparing `Soup.SourceFaceForSoupFace[f]` with the walk's `faces[f]` would make the alignment exact.

**Automatic bake dimensions and retry gate:**
- Width and height come only from the entity's current record, or from `PropertyTextureBakeRequest{}` (1024, which equals the generator's default resolution). No config value is read.
- A successful record stores `prepared.Width`, which equals the request when `MaxAdaptiveExtent == 0`. A failed record stores the requested extent.
- A change to the extent alone rebakes. A new failure at the same extent and token is gated.
- Transient `JobSubmitFailed` still records nothing.

**Scene codec** (`SceneSerialization.cpp:1759-1767, 2085-2100`):
- The extent is saved only while current, and never its stamps.
- Load runs after the vertex, halfedge and face properties are applied. The const `operator[]` accesses are guarded by `contains`. Negative, missing, string, 0 and above-16384 values all end in a failed `Publish` → `false`.
- The fixture removes `texcoords` to prove that an extent needs UVs to describe.

**Imports.** They publish only for `GeneratedAtlas`, after `PopulateFromMesh`, on the main thread. Authored UVs clear the record.

**Ring preflight cache:**
- `HasNonTriangleFaces` is computed in the same walk: after `Skip`, so deleted faces are ignored, and independently of `requireTriangles`.
- It is stored and replayed with the cache entry, so it stays shared with the topology methods.
- Preview (cached) and Apply (uncached walk) produce the identical diagnostic string that `Test.SandboxEditorMeshMethods.cpp:1051-1056` compares.

**Tests, checked logically:**
- The TextureBake cases are consistent with the code:
  - two triangles give a 32 B floor, rejected by a 16 B budget;
  - at a 40 B budget the six split-slot UVs alone (48 B) exceed it, rejected before any record is created;
  - the `12288x12288` text appears in the `InvalidResolution` diagnostic;
  - the `ForceAssetState`/Remove recovery works.
- The parameterization cases rely on the command defaults `Preserve=false, Force=true` (`ParameterizationOperations.cppm:101-102`). With those defaults, the successive regenerations do not take the N7 path, and the undo counts match the sequence (1 at the end).
- The serialization case uses `v:texcoord` from the fixture.

**Resolved or out of scope:** layering, lifetime (no borrowed spans outlive the owner), and the tab id.

## Notes (informational; not defects)

1. **Byte-pressure retries.** Queue *byte* pressure is only decided after `Prepare` (`TextureBakeModule.cpp:2147`). So while other bakes retain bytes, appearance re-prepares (coverage included) every frame until those bakes finish, via `continue` at `:3343`. This is bounded by in-flight completion. The review2 recommendation limited `continue` to rejections before preparation. If it matters, estimate the candidate's bytes from counts inside `RejectBeforePreparation`.
2. **Fingerprint cost.** `HashString64` is byte-wise FNV-1a. After a pool reallocation rebases every mesh, each extent-bearing appearance entity pays one full UV hash; about 50 ms at around 6 M halfedges. It is a one-frame hitch, not per-frame work. A word-wise hash would reduce it.
3. **Failed-record extent.** The appearance failure branch now overwrites `Width/Height` while keeping the previous pair's asset ids. This is harmless because non-Fresh records never bind and the next success reloads at the new extent. But the tab's reported extent then describes the failed request, not the retained texture.
4. **Reconstruction extent.** Reconstruction publishes no extent. Its atlas uses the 1024 default, which equals the appearance default, so this is correct today. If reconstruction ever exposes a resolution, it must publish one.

## Remaining verification (root-owned; none run by me)

1. Focused runs of the new fixtures at `ad532bf39`:
   - `PointConstructionOperations.UnderResolvedAtlasDoesNotDiscardReconstructedSurface`;
   - the five new or renamed `RuntimeTextureBakeModule` cases;
   - the two `ParameterizationOperations` extent/preservation cases;
   - `RuntimeSceneSerialization.GeneratedAtlasExtentRoundTripsAndRebindsToLoadedUvs`;
   - the updated `SandboxEditorUi.MeshAdmissionKeepsPolygonAndUnusedSlotSemantics`;
   - the `RejectedAtlasKeepsDirectMeshRenderableAndSelectable` lifetime fix.
2. Full CPU, full ASan and full UBSan at the final revision.
3. The opt-in Vulkan run, including the new thin-chart readback. Its result bounds GPU parity to the tested device only.
4. A corpus re-run through the updated script, bound to the final revision.
5. After R3-1: the extended history reproducer above, plus the adjusted `Test.TextureBakeModule.cpp:1054` assertion.

Until those run, keep the maturity at CPU-contracted, and make no Operational, performance or universal-mesh claim.
