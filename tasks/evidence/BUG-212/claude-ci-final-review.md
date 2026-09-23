**Verdict: PASS.** Both repairs are correct and stay in scope, and neither weakens a check. The pending items at the end still need to finish, and remote CI has not been confirmed.

**BUG-211: `glslc` in pr-fast**
- The workflow change is one line: `glslc` added to the existing apt install list (`.github/workflows/pr-fast.yml:96`). The working tree has no engine source changes, only `pr-fast.yml`, `tests/CMakeLists.txt`, the regression script, `SESSION-BRIEF.md` and the new task and evidence files.
- The corrected command in the task file (`tasks/active/BUG-211-...md:37`) requires only `property_texture_bake.frag.spv`, `property_texture_bake.vert.spv` and `uv_view/background.frag.spv`. Those are the three modified shader entry points in `main...HEAD`. The deleted `property_texture_bake_dilate.frag` is gone from the command. `uv_view/common.glsl` is an include file with no output of its own, so leaving it out is correct.
- `shader-outputs.log` reads "Found 114 SPIR-V shader outputs". The checker only prints that after the `--require` check passes (`tools/repo/check_shader_outputs.py:37-42`). I also confirmed all three `.spv` files exist under `build/ci-fast/bin/shaders/`.
- Saved logs show `Test.TouchedScope.py` (37 tests), `Test_CiPrerequisiteGuards.py` (7) and `Test.WorkflowRouting.py` (11) all OK. `Test.WorkflowConcurrency.py` has its log under BUG-212.
- Minor: `shader-outputs.log` doesn't record which `--require` arguments were used, so on its own it doesn't prove which files were checked. The task file and the file check above cover that gap. Not blocking.

**BUG-212: CTest processor inventory**
- **The two tests predate this PR.** Commit `0396ac1cb` added `EveryPointDomainPreservesDeletedSlotsAndExactScalarUndo` and `UnrepresentableScalarLabelRejectsAllOutputPublication`. That commit is an ancestor of the PR parent `e156d3a15`, which is on `codex/proc-034-token-efficiency`, but it is not on `main`. The exact counts of 6 and 78 came from `62e232ce1`, which is on `main`. None of the METHOD-047 commits (`c97235568..HEAD`) touch `Test.ClusteringModule.cpp`, and it has no uncommitted changes.
  - Caveat: if PR #1044 targets `main`, these tests appear in the PR's diff because they come in with the stacked parent. "Unchanged from the PR parent branch" is accurate; "pre-existing on main" would not be.
- **Scheduler accounting:** both tests call `NullWindowHeadlessConfig()`, whose default is `workers = 2u` (`Test.ClusteringModule.cpp:51-58`). The scanner assigns budget 2 to that call (`Test.WorkflowConcurrency.py:240-244`). `_scheduler_peak_slots(2)` gives 2 workers + 1 = **3**, which matches both new entries.
- **Source counts:** `NullWindowHeadlessConfig(),` appears 8 times (lines 700, 747, 828, 875, 928, 1019, 1280, 1311), so changing 6 to 8 is exact. `(1u),` stays at 3. There are now 8 ClusteringModule entries in the table, one per call site.
- **Nothing is weakened:**
  - Set equality between the table and the scanned sources is unchanged.
  - The ambiguity and `hardware_concurrency` guards are intact.
  - The no-wildcard check and the FATAL_ERROR for undiscovered tests are intact.
  - The only changes are three exact numbers, each raised by 2: 78 → 80, 3-slot 51 → 53, and 6 → 8. The 2-, 4- and 8-slot counts stay at 3, 22 and 2.
- **Evidence:** the before-log shows exactly these two missing tuples and the 8 != 6 failure. The after-log shows 20/20 OK.
- **Generated metadata:** `build/ci/tests/IntrinsicTestPropertyFixups.cmake` (regenerated 04:48, matching `configure.log`) has 80 `PROCESSORS` entries: 3 at 2 slots, 53 at 3, 22 at 4 and 2 at 8. Lines 1099 and 1129 set `PROCESSORS 3` for the two tests.

**Pending (not claimed):**
- I found no `generated-processor-budgets.json` or `ctest.log`.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests -j2` is still running, and `BUG-212/build.log` stops at step 54/180.
- So `ctest --show-only=json-v1`, the undiscovered-test FATAL_ERROR check at CTest time, and the actual runs of the two tests are all still outstanding. The file listing discovered tests is from 02:23, before the rebuild.
- Remote CI hasn't run yet, including apt `glslc` on ubuntu-24.04. The acceptance checkboxes in both task files are still unchecked.
