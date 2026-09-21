# Independent diagnosis

Claude Sonnet 5, medium, read-only bounded source review.

**Verdict: the source supports an unmodified-test scheduling race. I found no path from GEOM-099 into this failure.**

1. **Preview and apply read job state independently.** Both go through `ValidateUvRegenerationRequest` (`Runtime.GeometryProcessingOperations.Uv.cpp:1020-1027`). That calls `FindActiveEditorJob`, which calls the harness `FindActive` (`SandboxEditorJobHarness.cpp:28-44`). Each call takes a fresh `Snapshot()`. Each call then builds its own message with `ToString(job.State)` (`MeshSupport.cpp:419-430`). The text is "active queued job" or "active running job" depending on the state at that instant.
2. **Nothing holds the worker back.** `JobService::DispatchJob` stores `Queued`, then immediately calls `Scheduler::Dispatch` (`Runtime.JobService.cpp:315-316`). The worker stores `Running` at line 341 with no gate or hook. `EditorJobHarness` defaults to 2 workers (`SandboxEditorJobHarness.hpp:17`), so the worker starts as soon as apply returns.
3. **The gap between the two observations is real.** The test does topology-corrupting writes and a `deleted.Vector().clear()` between apply (5557) and the busy preview (5570), and it takes the snapshot at 5560. The busy preview and duplicate apply then run on separate `Snapshot()` calls. A worker turning `Queued` into `Running` in that window gives exactly the logged mismatch: preview "queued", apply "running", both `job 0:1`.
4. **The evidence fits a race.** The token is identical in both messages, and only the state word differs. The repeat run was 3 pass and 4 fail, with 0.05 s runs and no sanitizer report.
5. **GEOM-099 does not reach this path.** The patch touches only `Geometry.HalfedgeMesh.{Parameterization,Utils}`, `Geometry.Parameterization.{Bff,Harmonic}`, geometry test support, and docs. No `src/runtime` file is in it. `Uv.cpp` has no calls into Bff, Harmonic, or the disk-topology helper. Its only "Parameteriz" match is the module declaration at line 29. The admission path never runs topology code, and the test's own comment at 5564 says pending requests skip it.
6. **The failed assertion is only the diagnostic text.** The failing check (5574) compares `busy.DisabledReason` with `duplicate.Diagnostic`. The Pending status, the "already has an active" text, "job 0:1", the single queue entry, and the delivery counts are checked separately in the same test, and nothing in the logs shows them failing.
7. **Uncertainty:** I have not rebuilt or run the baseline UBSan, so I can't say the flake reproduces there. UBSan's slowdown probably widens the window, but that is inference. The inference rests on the fact that no code on the admission or worker path depends on GEOM-099.
8. **Cheap way to confirm:** run the same test filter repeatedly on the baseline UBSan build (for example `--repeat until-fail:20`). A single baseline failure closes the question. Any change to the test is outside your authorization here, and this diagnosis doesn't need one.

## Subsequent local validation

After this review, the original topology implementation reproduced the same
queued/running mismatch on the first UBSan repetition. See
[baseline output](baseline-ubsan-repeat.log) and the source identities in the
[bug note](../../backlog/bugs/BUG-206-uv-duplicate-submit-phase-race.md).
