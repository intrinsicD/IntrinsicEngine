**Verdict: not a PASS yet.** The main counts, identities and conclusions hold up, and none of the findings below overturns a conclusion. Three numbers are wrong, and one claim about the child mesh has no support in the final evidence. The rest are small wording fixes, listed smallest correction first.

## What checks out
- **Runs:** 35/35 runs pass and succeeded (`atlas-corpus-3/summary.json`). That is 3 procedural controls plus 4 frozen METHOD-045 surfaces, each run with native None/Angle/Area/Both and xatlas Angle. No run has zero region crossings missing.
- **No fallback:** `backend-audit.json` shows every run used the requested method and objective, with `used_fallback:false`.
- **Parameters:** every campaign used identical settings: 1024, padding 2, limits 10/10, `max_charts` 16384, `max_iterations` 40. That was true for controls-2 and corpus-1/2/3, so the "not tightened or relaxed" statement holds.
- **Evidence copies:** the evidence-directory copies (`final-corpus-summary.json`, `backend-audit.json`, `final-child-audit.json`, `source-provenance.json`) are byte-identical to the campaign outputs.
- **Hashes:**
  - `source.patch` sha256 is `8474c5f3…`, matching the provenance file and the `diff_sha256` in all 35 sealed results.
  - The runner hash `fedc4d1a…` matches the current `build/ci/bin/IntrinsicUvAtlasMeshDiagnostic`.
  - The build is `CMAKE_BUILD_TYPE=Debug`.
  - The patch includes the new files: ChartSolve, Validation, the reference manifest, `check_property_atlas.py` and `controls.py`.
  - Both archive hashes match `archive-hashes.json`.
- **Sealed results:** all 35 are `claim_eligible:false`, `dirty_worktree`, `timing_claim:false`.
- **Table:** 10 of the 12 corpus-table values match the independent audit rounded to 6 decimals.
- **Child mesh:** 100,000 faces, 4096², Both, 260 charts, success.
- **Density:** the audit uses one global density, `sqrt(ΣUV area/Σ3D area)`.
- **Docs:** the method doc (`docs/methods/property_guided_atlas.md:34-45,61-63`) correctly separates None (Tutte, no optimization), Area (a defined area-priority energy, explicitly "not AMIPS"), Both (SLIM on symmetric Dirichlet) and xatlas (Angle only).
- **Not independently checked:** the git-tree hashes in `post-corpus-source-binding.json`.

## Findings

1. **Three numbers don't match either data source (draft lines 21 and 32).**
   - The None max conformal ratio is 9.98758144 in the audit (fandisk-none), so it should read **9.987581**, not 9.987582.
   - The child max conformal is 1.5782082037, so **1.578208**, not 1.578209.
   - The child max area is 1.3731611762, so **1.373161**, not 1.373162.
   - The native values don't round to the draft's figures either (9.98758288, 1.57821154, 1.37316025).
   - Fix: use the audit maxima rounded to 6 decimals.

2. **Nothing in the final evidence shows the child 1024² rejection (line 33).** That rejection ("3 charts cover less than 1 texel") appears only in development probes that ran before the final binary: `geometry-logs/probe-2.log:9-11`, `probe-3.log:3` and `probe-dbg.log:325`. None of these are in `final-numerical-runs.tar.gz`. Also, `child-4096.log` and `child-4096-result.json` show that a pre-fix binary returned `under_resolved` at 4096² too; that run is kept in the intermediate archive. Suggested wording: "Earlier development probes rejected 1024² for sub-texel charts; the final binary was run only at 4096²."

3. **The tolerance is described inaccurately (lines 37-38).** The 1e-6 figure is only used for pass/fail against the limit: `max ≤ 10·(1+1e-6)` (`check_property_atlas.py`, the `passed` expression). The native and Python maxima are never compared with each other, and they differ by more than 1e-6: up to 1.41e-6 relative (fandisk-none max area) and 2.11e-6 (child conformal). Suggested wording: "The audit accepts recomputed maxima up to the limit ×(1+1e-6); native and independent maxima agree within 2.2e-6 relative on these runs."

4. **The None row needs context, and one parameter sentence is ambiguous (lines 13-14, 26-29).**
   - None's maxima (9.93–9.99) sit just under the limit of 10 because refinement keeps splitting charts until the limit is met. None used 431–2,434 charts on the frozen surfaces, versus 18–40 for native Angle/Area/Both (from `*/native.json` `chart_count`). Add one sentence saying so.
   - "16,384 charts and 40 optimization iterations" reads as if those were the actual counts. Change it to "a 16,384-chart limit and a 40-iteration budget".

5. **Two stale intermediate files sit unlabeled in the evidence directory.**
   - `corpus-summary.json` is identical to `atlas-corpus-2/summary.json`.
   - `child-independent-audit.json` is the audit of the earlier `child-4096-fixed-result.json`.
   - Both are already in the intermediate archive. Fix: delete them, or name them as intermediate in the README.

6. **"Verifies" overstates `backend-audit.json` (line 15).** It reads back what the runner itself reported in `native.json` (`actual_method`, `used_fallback`); it is not an independent check. Change "verifies" to "records that the runner-reported method/objective matched each request, without fallback".

7. **Publication gate (not a defect in the text):** `verification.json`, `verification-runs.tar.gz` and `atlas-workspace.png` don't exist yet. Lines 45, 58 and 78 ("The final Vulkan run includes that test") describe the pending GPU gate and screenshot in the present tense. Don't publish the report until those exist and match the text.

I found no unit or accounting errors beyond these, and no claim of universal reliability or speed. One related note: the child input has a single region, so its zero region crossings say nothing about the region guidance. The draft makes no such claim.
