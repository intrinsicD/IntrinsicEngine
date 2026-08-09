# BUG-138 artifacts

Screenshots from the live `ExtrinsicSandbox` sessions described in
`tasks/done/BUG-138-async-mesh-geometry-jobs-never-execute.md`. Both sessions
used the `ci-vulkan` build with the promoted Vulkan device, one imported
`tests/data/sculpt.obj`, and a nested `Xephyr` display (the host seat was
locked and its lock screen holds the real seat's input grab). Panels are
cropped to the text that carries the claim.

## Before the fix

- `refusal-before-fix.png` — four consecutive frames of the
  `Mesh / Processing / Simplify` panel during one refused edit. The first three
  show the submit-time `Mesh simplify CPU job queued (job 3:1).` line; the
  fourth shows the terminal result slice A added, `Sandbox.MeshSimplify.CPU did
  not apply: the mesh changed after the job was queued, so the result no longer
  matches the geometry it was computed from.` This is the message the contract
  tests reproduce verbatim. Every line is rendered twice, which is `BUG-141`,
  not this defect.
- `denoise-applies-on-the-same-entity.png` — `Last denoise run: Applied` on the
  entity where simplify was being refused. Denoise runs the same domain,
  metadata-signature, and position checks and skips only the topology
  comparison, so this is the reading that isolated the failure to that
  comparison.

## After the fix

Five consecutive topology edits on the one imported entity, in order:

- `second-simplify-applied.png` — `Applied`, `faces: 4000 -> 2000`. This is the
  edit that failed deterministically before the fix.
- `third-simplify-applied.png` — `Applied`, `faces: 2000 -> 1000`.
- `subdivide-applied.png` — `Applied`, Loop, `faces: 1000 -> 4000`.
- `remesh-applied.png` — `Last remesh run: Applied`, uniform, one iteration.
