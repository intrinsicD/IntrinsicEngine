Experiment pre-flight review, round 1 of 2 (full tier). Rules: read-only; do not modify files, build, or run anything; do not
call other agents or CLIs; no further delegation. Read only files inside /home/alex/Documents/IntrinsicEngine (do not open the
manifest script path outside the repository that Layer A mentions). English, at most ~1000 words.

Repository: /home/alex/Documents/IntrinsicEngine at commit 3fea80e71. Packet digest: sha256 of ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/manifest.txt = de8b3cc3d307a814f87c20761271ca79ff8afe64ed7eff241f2f4bafcdd1fa56.

In this round read ONLY Layer A: ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-a.md, plus the code it names (runner
benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp, driver tools/benchmark/cpd_landmark_variants.py, the library
files listed). Do NOT open ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md, tasks/active/METHOD-077-*, tasks/done/METHOD-076-*, research_notes/,
reports/, or any result file before round 2. (Instruction-only rule; say if you broke it.)

Reconstruct from Layer A and the code only:
1. What each cell measures, metric by metric, in which units; how the reference and the comparisons are formed.
2. What a no-op / ineffective landmark method would produce in these metrics.
3. Which degenerate solution or false success would score well, and whether the plan catches it.
4. What this run cannot distinguish.
5. Execution risks: timeouts, ordering, resumption, determinism, resource use, anything in the driver or runner that could
   corrupt or silently drop a cell.
Report the reconstruction; no verdict yet.
