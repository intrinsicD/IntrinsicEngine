# ARCH-022 Q83: from the K-Means request to the first progress, on the Armadillo

Date: 2026-11-05. Task: `ARCH-022` (Q81–Q85). Status: measured under the full pre-flight. A descriptive measurement
of the operator's case and two comparison cells on one host.

## Question
The operator's questions (verbatim):
- Q81: "Also was genau ist da jetzt los? Außerdem gibt es noch den Punkt dass wenn ich k means auf dem armadillo mesh
  machen (als Beispiel) dauert es für meinen Geschmack nach dem Klick auf den run k means Button erstaunlich lange bis
  die Methode startet. Ich frage mich was da intern alles erst passiert bevor es los geht?"
- Q82: "Backend war gpu. Bitte mach A. Die Punkte b-d überlegen wir uns nach dem nächsten Test"
- Q83: "Ja, dann 2." (option B: click to first preview on the Armadillo with the GPU backend, split by frames)
- Q84 (multiple choice): "Dropbox obj, 173k Vertices"; "build/dev (Debug)"; "Standard (beides an)"; "Etwas später";
  "Du darfst xset benutzen (Empfohlen)"; "Fortschritt im Panel".
- Q85 (multiple choice, the measurement boundary): "Ja, so messen (Empfohlen)".

What happens between the K-Means request (the button's `SubmitKMeansRun`) and the panel's first moved progress (its
"Iterations: N" line), how long, and which parts — in the operator's setup and against Release with and without
validation?

## What ran
- Packet `tasks/evidence/ARCH-022/preflight-q83/`: revision 4, digest
  `3c2df2815a379619b538a93e0fb1117173c70ae8f6b2e011d51902d22573f45c`, commit `fea0153ef`. Codex and Claude each gave PASS
  on that digest after four rounds (`reviews/README.md`).
- Run: `tasks/evidence/ARCH-022/q83_run.sh tasks/evidence/ARCH-022/q83`, 2026-11-05, exit 0, 9 of 9 processes valid.
- The real `ExtrinsicSandbox` with the opt-in `--kmeans-trace` (per-frame clustering counters and pacing), driven through
  its agent socket: import the Armadillo (`~/Dropbox/Work/Datasets/obj/armadillo.obj`, 172 974 vertices) as File >
  Import does, wait for the import's jobs, select it, apply the clustering section with the GPU backend (8 clusters, 32
  iterations, Hierarchical seeding, preview every 5 iterations), then two runs 5 s and 3 s apart.
- Cells: U the operator's `dev` preset (Debug, ASan+UBSan, shared libraries, plugins as MODULEs), validation on;
  V Release, validation on; R Release, validation off. VSync on, displays kept on with `xset` (Q84).
- Boundary (Q85): times start at the request's in-process enqueue; "the panel would draw" is a proxy (the same
  iteration count the panel reads, drawn one frame later by code); the button's work before the enqueue (configuration
  apply, readiness check) is estimated from the frames around the same agent calls (a frame-level estimate that
  includes the rest of those frames).

## Results (ms from the request, three processes per cell)
| | U (dev build) | V (Release, validation) | R (Release) |
|---|---|---|---|
| idle frame period / frame work (median) | 16.7 / 6.0 | 16.7 / 0.8 | 16.7 / 0.4 |
| run 1: start frame (capture and start) | 484–488 | 4.3–4.5 | 4.0–4.1 |
| — of it Hierarchical seeding | 346–348 | 1.4–1.5 | 1.4–1.5 |
| — of it snapshot preparation (validation, point and output capture) | 133–134 | 1.7–1.8 | 1.7–1.8 |
| run 1: panel would draw the first iteration | 526–531 | 100 | 100–101 |
| run 1: first preview published | 554–558 (7 frames) | 333 (19 frames) | 333 (19 frames) |
| run 1: applied (auto-accept) | 2 746–2 771 | 1 737–1 819 | 1 803–1 819 |
| run 2: start frame / panel would draw the first iteration | 350–359 / 381–390 | 1.7 / 99 | 1.7–1.8 / 98–99 |
| frames between iterations | 1 just after the start frame, then 2–3 | typically 3 | typically 3 |
| before the enqueue: configuration-apply frames, variable tick (max) | 54–56 | 1.6 | 1.6–1.7 |

In the operator's setup the start is one frame of about 0.5 s (run 1) or 0.36 s (run 2) in which the UI does not
update. About 347 ms of it is the Hierarchical seeding on the main thread (a mean pass and seven farthest-point passes
over the 172 974 points); 133 ms in run 1 is the snapshot preparation (2.4–10 ms in run 2, where the point capture is
reused). The frames around the configuration apply, which the button performs in the click's frame, have a variable
tick of 54–56 ms in the dev build (1.6–1.7 ms in Release); that is an estimate of the apply's cost, not an isolated
measurement. The dev build's idle frames are not slow (VSync-bound, 6 ms of work). In Release the same start is 4 ms;
there the time to the first progress (about 100 ms) and to the first preview (333 ms) follows the frame cadence of the
iterations: an iteration took three frames in 9 of the 12 Release runs throughout, and three frames in most of the
other three, which also contain one- to four-frame gaps and 70–100 ms present stalls. The first preview counted six
iterations, not five: the preview scheduled after iteration five shares the next submission with iteration six and is
published when that submission completes.

Comparisons (Layer B's reading rule: separate ranges and at least the 100 ms or factor-of-two threshold): U against V,
median differences in run 1 of 482 ms (start), 475 ms (handled), 438 ms (first iteration), 430 ms (panel proxy), in
run 2 of 354, 339, 296 and 288 ms: shown. V against R: no decision-relevant separation in the time to the first
progress or the preview; the idle frame work is about twice as high with validation (0.75–0.80 against 0.38–0.43 ms),
small in absolute terms. U − V is the whole dev build bundle, not one of its parts.

## Predictions and surprises
Missed: the dev build's idle frames were predicted CPU-bound (they are VSync-bound); the idle frame work was predicted
higher in every cell; the seeding was predicted at most 150 ms in U (347 ms); Release was predicted at one to two
frames per iteration (typically three: 19 frames to the first preview against 6–14); the dev build's readiness-check
frames (16.7–18.2 ms) fell below their predicted 20–500 ms. One V process's readiness-check span held a 104 ms frame
whose time is in the platform-begin phase (event polling and window-state checks), not in the call's variable tick.
Details: `tasks/evidence/ARCH-022/q83-audit.md`, `q83/report/compare.html`, `q83/report/{U,V,R}.html`.

## Limits
One host and session, three processes per cell, descriptive ranges. Not measured: input latency, the present-to-display
time, GPU-side timing (so the share of GPU execution in an iteration's three frames is not known); the panel line is a
proxy and the agent path auto-accepts (Q85).

## What this suggests for the options (the operator decides; none of it was tested here)
- The operator's wait is the dev build's start frame: the Hierarchical seeding (about 347 ms) and the snapshot
  preparation (133 ms in the first run), both on the main thread, and the estimated configuration apply in the click
  frame. A Release Sandbox showed a 4 ms start. Moving the seeding off the main thread, or a cheaper seeding, would
  address the largest measured part in the dev build.
- In Release the remaining time follows the iterations' frame cadence (an iteration ends at a host readback before the
  next page is recorded; that this is why it takes three frames is an inference). Fewer host round trips per iteration
  (several iterations per submission) is the candidate that would shorten it; an earlier preview would not move the
  first "Iterations" increment.
