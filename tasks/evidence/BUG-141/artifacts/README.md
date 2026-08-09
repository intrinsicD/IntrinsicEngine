# BUG-141 artifacts

Before/after pairs from live `ExtrinsicSandbox` sessions on 2026-08-09, both on
the `ci-vulkan` build with the promoted Vulkan device and one imported
`tests/data/sculpt.obj`, driven on a nested `Xephyr` display. Panels are
cropped to the header and the owning operation's result line.

The "before" images were captured while diagnosing `BUG-138`; they are the same
frames registered under `tasks/evidence/BUG-138/artifacts/`, kept here because
they are what slice A is measured against.

## Mislabelled and duplicated

- `queued-job-before.png` — four frames of one refused simplify. The first
  three show `GeometryProcessingFailed: Mesh simplify CPU job queued
  (job 3:1).` — a job that was running normally, announced under a failure
  code — and every line is printed twice.
- `queued-job-after.png` — three frames of one simplify at target 2000. No
  diagnostic line appears in the header at any point. The panel reports its own
  outcome instead: `Last simplify run: Pending` while queued, then
  `Last simplify run: Applied`.

## Unscoped

- `denoise-panel-before.png` — the `Mesh / Processing / Denoise` panel carrying
  two `Sandbox.MeshSimplify.CPU did not apply: …` lines it had nothing to do
  with, because the runtime folds the shared processing diagnostics into every
  domain window's header.
- `denoise-panel-after.png` — the same panel on the same entity after a
  simplify, with a clean header.
