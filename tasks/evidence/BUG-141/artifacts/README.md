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

## Slice B — lifetime and rejection cause

Captured 2026-08-09 from a second live `ExtrinsicSandbox` session, same
`ci-vulkan` build and the same imported `tests/data/sculpt.obj`, driven on the
nested `Xephyr` display. Both panels were widened first, because at their
default size the result block is clipped (`UI-049`).

- `dismiss-simplify-before.png` — `Mesh / Processing / Simplify` after a run at
  target 2000: `Last simplify run: Applied`, the full result block, and the new
  `Dismiss` button.
- `dismiss-simplify-after.png` — the same panel one click later:
  `Last simplify run: none`. The state is cleared in the session, not only in
  the panel, so it does not come back on the next prepared frame.
- `parameterization-rejection-cause.png` — `Mesh / Processing /
  Parameterize (UV)` refusing the mesh. `Last run diagnostics` now reads
  `Rejected mesh: 1 connected component, 0 boundary loops` above a message that
  names the precondition and what to do about it. The window header carries
  only `Selected entity: 2 (mesh)`: the result message used to be mirrored onto
  the view model and printed there as well, so one command's outcome appeared
  twice in one window.
- `dismiss-parameterization-after.png` — the same panel after `Dismiss`:
  `No parameterization has run this session.`
