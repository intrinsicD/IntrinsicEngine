**Verdict: PASS** for the source delta at `ae8c1d41` and for the corrected numerical wording. Publication is still blocked on the GPU and visual gates below. I found no new reproducible defect. This was a read-only review: I built nothing, ran no tests, and edited nothing.

## Source delta, `b814eb87..ae8c1d41` (5 files, +72/−4)

### R4-1: closed
- **Capture (`Uv.cpp:620-623`).** When `FindCurrentMeshUvAtlasExtent` returns nothing but the component exists, the raw record is copied into `before.RetainedExtent`. After-states are built fresh by `MapUvPublishedProperties` and never set `RetainedExtent`, so the preserved v→h re-bind still behaves as before.
- **Apply (`Uv.cpp:535-539`).** The existing `Publish(0,0)` runs first. `RestoreMeshUvAtlasExtent` is then called only when `!AtlasExtent && RetainedExtent`.
- **The helper (`MeshSurfaceTopology.cpp:807-818`).** It checks the entity is valid, clears both revision stamps, sets `StampsMatchContent=false`, and keeps Width, Height and fingerprint.
  - Any complete binding carries exactly one engaged revision, so cleared stamps can never match it.
  - `Find` and `Refresh` therefore work out whether the record is current from the UV content, with a single hash. `Refresh` then caches the result, so the per-frame cost stays O(1).
  - Nothing is serialized, and stamp handling stays inside the owning module.
- **Test (`Test.ParameterizationOperations.cpp:1707-1715`).** I traced it step by step:
  1. Regenerate at 1536.
  2. Undo: the retained 512 record comes back with cleared stamps, but the LSCM `v:` fingerprint differs. So the extent is 0, and `Refresh` caches `false`.
  3. Undo the parameterization: `h:` comes back with a fresh revision, the stamps don't match, and the hash equals F0. So the extent is 512.
  4. Redo twice: the 1536 after-state publishes 1536.
- **The test discriminates.** Under `b814eb87`, the first undo removed the component, so step 3 returned 0.
- **No side effects:** the NoChange/`SameAtlasExtent` gate ignores `RetainedExtent`, scene save still uses `FindCurrent`, and no other callers are affected.

### GPU fixture: correct, and the old case is preserved
- `OpenAtlasWorkspace` only changes which window id is opened. `"mesh.uv_atlas_workspace"` matches `kAtlasWorkspaceWindowId` (`MethodPanels.cpp:569`).
- The body moved into `RunParameterizationUvViewRuntimePath(bool)`. The old test name still calls it with `false`, and a new test calls it with `true`. `GTEST_SKIP`/`ASSERT` inside the `void` helper return correctly.
- **Scene rectangle.** `ClaimSceneViewport(meshX, workPos.y, meshWidth, workSize.y)` uses the default `SplitRatio` 0.5 (`ParameterizationConfig.cppm:71`), and neither the fixture nor any config overrides it. The expected aspect is 0.5·W/H, with a small menu-bar bias that fits well inside the ±0.12 tolerance. A full-window rectangle would be about twice that and fail, so the check discriminates.
- The `Presented…` → `SceneViewport()` fallback covers either side of the per-frame exchange.

### Source binding
- The `ae8c1d41` tree is `e1e9866d…`, which equals `final_source_tree` in the binding file.
- The diff from the corpus tree `b78ecc75…` to `e1e9866d…` touches only `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` (+34/−4).
- The R4-1 fix is therefore already inside the corpus-3 tree.

## Evidence README corrections: all confirmed

| Correction | Status |
| --- | --- |
| Three rounding fixes | Correct. Recomputed from `atlas-corpus-3/summary.json`: None maximum conformal 9.987581 (fandisk). From `child-4096-final-audit.json`: child 1.578208 / 1.373161. The other seven table values also match. |
| Child 1024² rejection claim | Removed. Only the 4096² Both run remains (260 charts). |
| Tolerance (lines 43-45) | Now described as limit×(1+1e-6), with no claim that native and Python maxima agree within it. |
| None chart-count cost (lines 32-34) | Correct against `native.json`: 431–2,434 charts for None versus 18–40 for the optimized objectives on the four frozen surfaces. The budget sentence now reads as limits, not actual counts. |
| Backend wording (lines 14-16) | Now "runner-reported … reporting audit, not an independent execution trace". |
| Intermediate files | Renamed to `intermediate-corpus-summary.json` and `intermediate-child-audit.json`, and explained on lines 87-88. |
| Scope | Fixed region labels, with property binding and GMM left to the runtime tests (lines 16-18), as stated. |

The run summary also checks out: 35/35 runs pass with status success.

## Pending gates (not code findings)
1. **Full Vulkan run.** The FULL97 CTest run (`vulkan-final-tests-2.log`) must finish with zero failures, including both `ParameterizationUvViewWindow…` and `AtlasWorkspaceUsesOperationalGpuTargetAndSceneRectangle`. Until then, lines 58-60 and 85 of the README ("The final Vulkan run includes that test") are not yet backed by evidence.
2. **Missing files.** `verification.json`, `verification-runs.tar.gz` and `atlas-workspace.png` must exist and match README lines 52-56 and 65-68 before publication.

The METHOD-047 GPU/visual maturity claim should wait for those two gates. The source is final.
