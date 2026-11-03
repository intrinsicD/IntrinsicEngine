# ARCH-022 R7 baseline: main-thread stall of a GPU k-means run under a slider preview

Date: 2026-11-03. Task: `ARCH-022` (S3c-3). Status: measured under the full pre-flight (both reviewers PASS; one asked
that the operator's decision on the scope, MM1, be recorded before the start; it was not made, and the run measured
the reviewed scope, see "Not measured"). This is the GPU k-means row of R7 as a Release baseline on one host; the
other algorithms and the Debug configuration are not measured.

## Question
The operator's questions (verbatim):
- Q71: "Ja, bitte S3. Und was ist mit ponytail Vorschlag?" — S3's plan ends with "the main-thread stall during a
  slider preview is measured against the proposed 4 ms".
- Q73: "ja bitte, 1 - 3. ausserdem ist der bildschirm nun eingeschaltet. plugins/spike soll eigentlich verschwinden,
  von mir aus plugins/fixture" — item 2 is S3c, k-means as a step run and this measurement.
- Q46 accepted the target "R7 under 4 ms".

Rule R7 of `docs/architecture/engine-and-feature-modules.md` measures the longest main-thread stall caused by an
algorithm run.

## What ran
- Packet `tasks/evidence/ARCH-022/preflight-s3c3/`: revision 3, digest
  `7219b147e87ffc69f34108624437f4b655c9f8c8a1e9dc73b247ea291b51f464`, commit `3fb976384`. Claude and Codex each gave
  PASS on that digest after four rounds; the reviews and the start gate are in `reviews/README.md`.
- Run: `tasks/evidence/ARCH-022/kmeans_stall_run.sh tasks/evidence/ARCH-022/s3c3`, 2026-11-03 16:58 to 16:59 UTC,
  exit 0. A first attempt ended at once with exit 127 ("No such file or directory"), cause not traced; the script
  also lacked its execute bit, which was then set; nothing ran.
- Harness `IntrinsicClusteringStallMeasurement` (`tests/benchmark/Test.ClusteringStallMeasurement.cpp`): the
  production clustering, step-run and job modules on Vulkan, Release (`ci-vulkan-release`), VSync off; one point
  cloud of N points (not rendered); every request `RunKMeans` with K = 8, 50 iterations at most, random
  initialization, a preview every 5 iterations, `ReplaceRunning`.
- Arms per N, interleaved: `run` ×3 (600 frames with one replacing request each, then 20 requests each held until
  its run previewed, then one final request accepted and published: 621 requests), `null` ×3 (1000 idle frames),
  `controls` ×1 (seven injected delays, one per counted interval, a split and a GPU wait).
- Rule, per N: U is the largest main-thread frame total (`RuntimeFramePacingDiagnostics::TotalMicros`) over the run
  frames, B the largest null frame, L = U − B; `met` if U < 4 ms, `not met` if L ≥ 4 ms, else undecided. L is a lower
  bound if the run's worst frame carries no more background than the null arm's largest frame.

## Result
No kill condition fired: 14 processes exited 0 with their files; every run issued 621 requests in the frozen
cadence and ended with one Applied completion on `vulkan_compute`; all seven controls passed in both cells.

| Points | U | B | L | Verdict | processes ≥ 4 ms |
|---|---|---|---|---|---|
| 100 000 | 5.13 ms | 0.66 ms | 4.47 ms | **not met** | 3 of 3 |
| 1 048 576 | 62.0 ms | 0.65 ms | 61.3 ms | **not met** | 3 of 3 |

Where the time goes (the worst frame of each stage, its largest interval and the module path in it):
| Stage | 100 000 points | 1 048 576 points |
|---|---|---|
| every-frame slider | 1.7 ms per frame (median): a held request's capture and GPU start, median 1.4 ms, on 599 of 601 frames | 17 ms per frame (median): capture and start, median 16.5 ms |
| first request | 3.4 to 3.5 ms: capture and start 3.0 ms (its completion reports three pipelines and eight buffers created) | 33.7 ms: 32.7 ms |
| slider on each preview | 2.6 ms: a start 1.9 ms | up to 40.5 ms: a start 22 to 26 ms plus the renderer's execute 14 ms |
| accept | 4.8 to 5.1 ms: the Accept's publication 4.5 to 4.8 ms | 60.4 to 62.0 ms: 59.8 to 61.5 ms |

- The longest stall is the Accept's publication (label decode and `CommitKMeansOutputs`: labels, colors, scalar
  labels, history), at both sizes. Which of its parts dominates is not measured.
- Under an every-frame stream a replacing request starts on almost every frame: during the continuous stage, 598 held
  starts plus the first request's direct start put capture and start time in 599 of its 601 frames. Such a frame pays
  a full capture of the cloud and a GPU start. The plan predicted far fewer starts; that prediction was wrong.
- At 1 048 576 points the renderer's execute (`ExecuteFrame`, timed as a whole) takes 6.2 to 30.2 ms in 10 to 19 hold
  frames per process, against at most 0.49 ms in the null arm. The cause is not traced; a wait on the GPU's k-means
  work is a hypothesis.
- Peak resident set at most 455 MiB; GPU memory read between processes at most 1487 MiB.

Audit (recomputed from the raw files by an independent script; predictions against outcomes; surprises):
`tasks/evidence/ARCH-022/s3c3-audit.md`. Raw files, report and summary: `tasks/evidence/ARCH-022/s3c3/`.

## Not measured
- The render refresh of the cloud's channels after the accept: the harness's cloud is not rendered. Whether R7
  should cover it is the operator's decision; it would need a renderable cloud in every arm and a new packet.
- The Sandbox UI and draw cost; other K, preview intervals, the mesh domain; Debug; other hosts.
