# METHOD-047 independent review 4: R3-1 remediation findings

- Reviewer: Claude Opus 5.5 (`claude-opus-5-5`). Read-only: no builds, test runs, source edits, commits or agents.
- Revision under review: `b814eb8701492edf074164ef99900e4565741191` against `ad532bf392e0cc2bc26129dd77a074af9b6e6180`, in the worktree `/tmp/intrinsic-method047-review4`, which is clean at that HEAD.
- Delta: 5 files, +44/−23.
  - `Runtime.MeshSurfaceTopology.{cpp,cppm}`
  - the two runtime tests
  - `property_guided_atlas.md`
- Context read, not re-reviewed:
  - `review3-findings.md`;
  - `Geometry.Properties.{cpp,cppm}`, for revision, epoch, `Add` and rebase semantics;
  - `Uv.cpp:363-380, 505-538, 597-617, 1170-1218`;
  - `TextureBakeModule.cpp:440-478`, the default texcoord binding;
  - the doc wording at `runtime.md:864-868`.

## Verdict

**R3-1 is fixed correctly, and the shadow-UV secondary is fixed correctly.** I found no defect in either edit.
- One **Low** residual remains in the same class. The retained stale record is still discarded by the UV-regeneration history snapshot (R4-1 below). The fix is small and local. I recommend it before merge, because `runtime.md:866` and `property_guided_atlas.md:94` now state without qualification that the extent follows undo/redo.
- No test execution is claimed or implied here. The focused runs, full CPU, ASan/UBSan and final Vulkan at `b814eb87` all remain owned by the root.

## R3-1: verified

The cached-verdict design matches the review3 recommendation.
- **`Publish`.** It stamps after the writes, stores the fingerprint of the selected domain, and sets `StampsMatchContent = true` by default.
- **`Refresh`:**
  - If the binding is absent, it returns `nullopt` and leaves the record and its stamps unchanged.
  - If the stamps match, it returns the cached verdict in O(1).
  - Otherwise it re-stamps, re-hashes once, and caches the verdict.
  - It never removes the record.
- **`Find`.** It applies the same rule without writing.

**Why stale caching is sound.** A cached `false` can only be wrong if content equal to `TexcoordFingerprint` reappears under the same stamps. The property code rules that out:
- `FindPropertyRevision → storage->Revision()` calls `Observe()`, which closes the registry epoch. Any later mutable borrow, `Resize`, `PushBack` or `Swap` then draws a fresh value from the global monotone counter (`Geometry.Properties.cpp:26-46`).
- A new property also gets a fresh value, because `Add` calls `storage->MarkModified()` (`Geometry.Properties.cppm:681-686`).
- A move or rebase gets a fresh value too (`Geometry.Properties.cpp:92-105`).

**Keeping stamps while the binding is absent is also sound:**
- A return to the same stamp tuple means the same unmodified storage. The cached verdict still describes it.
- Removing and re-adding UVs gives a new revision, which forces a re-hash.
- A switch between corner and vertex domains changes the shape of the tuple (`{c,∅}` against `{∅,v}`), which also forces a re-hash.

**Per-frame cost.** Appearance cost stays O(1) per frame in both the stale and current states. There is one hash per change of UV revision.

**Consumers that see a stale record.** `FindCurrent` is `nullopt` for a stale record, so:
- scene save omits it;
- a UV commit before-state, the `NoChange` gate and `afterExtent` treat it as absent;
- appearance uses 1024.

These are the same outputs as before the fix. The only change is that the component still exists.

**The scenario in the new test** (`Test.ParameterizationOperations.cpp:1697-1705`) traces as follows:
1. LSCM retires `h:` and leaves the binding as `{∅, v1}`. The stamps differ, the hash of `v` is not F0, so the result is `nullopt`.
2. Two refreshes cache `false`.
3. Undo restores `h` with a fresh revision. The binding becomes `{c2, ∅}`. The stamps differ, `hash(h)·P` equals F0, so the extent is 512. Because `Publish` at the redo on line 1680 now computes F0 with the new single-domain formula, the comparison is like for like.
4. A refresh re-stamps with `true`.
5. Redo gives `{∅, v3}`, which re-hashes to `false`.

The test discriminates: under the old delete-on-stale code, step 3 would return 0.

**The adjusted bake test** (`Test.TextureBakeModule.cpp:1056-1061`) has the right shape: the component is present and `FindCurrent` is false.

**Nit, not required.** When `Find` succeeds through the fingerprint path after a stale verdict was cached, it returns a copy with `StampsMatchContent == false`. No caller reads the flag or the stamps:
- the history comment at `Uv.cpp:369-370` says stamps are re-taken;
- scene save reads only width and height.

So the copy is harmless. Setting `.StampsMatchContent = true` on the returned copy would make it self-consistent.

## Secondary (shadow `v:texcoord`): verified

`BindCanonicalTexcoords` now stamps and hashes exactly one domain. It prefers a complete `h:texcoord`, otherwise a complete `v:texcoord`. That is the same precedence as the bake's default binding (`TextureBakeModule.cpp:459-472`), including the "complete means size equals `set.Size()`" rule.

- **Domain tag.** The `corner·FNVprime ^ vertex` form, with the other term 0, tags the domain. Identical bytes in the two domains give `c·P` and `c`, which cannot be equal for odd `c`, and `c` is odd because of `| 1`.
- **Persistence.** No fingerprint or stamp is persisted: scene load calls `Publish`. The formula change therefore needs no migration.
- **Test.** The fixture (`MakeSeamedQuad`) carries both `h:` and `v:`. Before the fix, the new shadow write at `:1032-1036` would have made the record stale. It now stays current through both `Find` and `Refresh`, so the test discriminates.
- **Doc wording.** `runtime.md:867` says "UV edits invalidate it". Strictly, this is now "edits to the canonical (bound) UVs". The wording is acceptable, but it could say "canonical UV edits".

## R4-1 (Low): UV-regeneration history still discards a retained stale extent, so an undo past it cannot recover

**Anchor:**
- `Uv.cpp:615` takes `before.AtlasExtent = FindCurrentMeshUvAtlasExtent(...)`. For a stale record, that is `nullopt`.
- `Uv.cpp:532-534` then applies `Publish(0, 0)`, which removes the component (`MeshSurfaceTopology.cpp:790-793`).

**Sequence.** Regenerate at 512, then LSCM, then Regenerate at 1536, then Undo, then Undo.
- The retained 512 record is replaced at the second commit.
- Undoing that commit removes the record, because its before-state was `nullopt`.
- The next undo restores the 512 UVs bit-exactly, but no record remains.
- Appearance then bakes the 512 atlas at 1024, and save drops `uvAtlasExtent`.

This is the R3-1 symptom behind one more history step. Appearance does not need to run for it to happen.

**Reproducer.** Append after `Test.ParameterizationOperations.cpp:1705`. The state at that point is the LSCM UVs with a stale 512 record, and the command defaults `Preserve=false, Force=true` give a `Generated` commit:

```cpp
ASSERT_TRUE(regenerate(1536u).Succeeded());
ASSERT_TRUE(harness.History.Undo().Succeeded());   // back to LSCM UVs
EXPECT_EQ(extent(), glm::uvec2(0u));
ASSERT_TRUE(harness.History.Undo().Succeeded());   // parameterization restores the 512 UVs
EXPECT_EQ(extent(), glm::uvec2(512u));             // fails at b814eb87: component was removed
```

**Smallest fix.** Retain the raw record only in the *before* snapshot. After-states must keep today's `Publish` semantics, because a preserved v→h conversion intentionally re-binds the current extent to the converted UVs.

1. In `UvPublishedProperties`, add `std::optional<MeshUvAtlasExtent> RetainedExtent{};`, a stale record to restore verbatim.
2. At `Uv.cpp:615`, when `before.AtlasExtent` is empty and the component exists, set `before.RetainedExtent = *sourceRaw.try_get<MeshUvAtlasExtent>(*sourceEntity);`.
3. In `ApplyUvPublishedProperties`, after the existing `Publish` call, when `!state.AtlasExtent && state.RetainedExtent`, re-emplace `{Width, Height, TexcoordFingerprint}` with **both stamps cleared** (`nullopt`).
   - Put this in a small owner function in `MeshSurfaceTopology`, for example `RetainMeshUvAtlasExtent`, so that stamp semantics stay private to their owner.
   - A binding always carries exactly one engaged stamp. Cleared stamps therefore never match, and the next `Find`/`Refresh` re-derives currency from content with a single hash.

The result is that each history state restores the extent component exactly as it stood, and currency stays a pure function of UV content. There is no per-frame cost, no serialization change and no public-surface change beyond the optional owner helper. If the root prefers to defer this fix, the two doc sentences should say that the extent follows undo/redo of UV regeneration and of the first UV edit after it.

## Remaining verification (root-owned; none run by me)

1. Focused runs at `b814eb87`:
   - `ParameterizationOperations.GeneratedAtlasExtentFollowsItsUvsThroughHistory`;
   - `RuntimeTextureBakeModule.AppearanceBakeFollowsArbitraryRecordedExtentsOnlyWhileTheUvsAreCurrent`;
   - `RuntimeSceneSerialization.GeneratedAtlasExtentRoundTripsAndRebindsToLoadedUvs`, because the fingerprint formula changed;
   - the rest of the review3 list.
2. Full CPU, ASan and UBSan at the final revision.
3. Final Vulkan at the final revision. The earlier 44/44 run was at a prior source snapshot, so it is not evidence for `b814eb87`. The CPU-only extent-owner change makes a GPU regression unlikely, but the gate still has to run at the final revision.
4. If R4-1 is fixed: the reproducer above.

Maturity stays CPU-contracted until these run.
