# Temporary evidence retirement

Implementation and experiment artifacts live only while needed by open work.
[Task retirement](../agent/task-format.md#retiring-a-task) owns the current
procedure. Durable knowledge belongs in tests, method/architecture docs,
[lessons](README.md) and [result summaries](../results/).

## Historical migration

The 2026-10-13 cleanup removes completed-task raw artifacts from the checkout.
It preserves the original Git history at
`36a7b5062d9be3239d667efd9e41c4d4028ba249`. A claim proof written as
`36a7b5062d9be3239d667efd9e41c4d4028ba249:path` refers to that historical
object, not a present file. Retrieve it with `git show <revision>:<path>`;
GitHub blob/tree links in old notes use the same exact revision. Historical
inline command/path examples in frozen tasks, session traces and packets also
refer to their original checkout. They are not current runnable commands.

No old experiment was rerun. Claim statements, dispositions, negative results
and limitations remain unchanged; summaries explicitly distinguish recorded
observations from current verification. Existing compact result tables tied to
retired diagnostics moved to `docs/results/`; raw archives, snapshots, logs,
review transcripts and one-off analysis outputs were removed. Reusable atlas
inputs moved to [maintained fixtures](../../tests/data/atlas/README.md), not a
replacement raw-output archive.

## Evidence still needed by open work

Existing active and backlog evidence remains byte-identical. Completed-task
inputs still used by active METHOD-083 stay at their frozen paths:

- `tasks/evidence/METHOD-081/compare.py`: imported `distances` and `quality`.
- `tasks/evidence/METHOD-082/fixtures/{full,partial}_{1,2,3}.npz`: exact six
  registration inputs used by preparation, analysis and controls.

METHOD-083 owns their eventual cleanup. Do not move or regenerate these inputs
while its reviewed configuration depends on them. This retention does not
preserve the completed METHOD-081/082 run outputs.

The METHOD-040, METHOD-043, METHOD-077 and METHOD-083 evidence directories and
backlog BUG-221/222/225/228/229 evidence remain. The open HKTex design context
(`heat_kernel_texture_discussion_20260924`, `hktex_default_design_20260924`,
`hktex_task_plan_20260924`) remains with GRAPHICS-105/METHOD-048. The curvature
frog diagnostic belongs to open METHOD-040.

Ownership of the old `geodesics_virtual_source_2026-09-08`,
`geometry_publication_2026-09-09`, `outlier_vulkan_2026-09-09`,
`point_lbvh_2026-09-08` and `registration_spatial_2026-09-08` diagnostics was
not established in this cleanup; they remain for explicit owner triage.
Unrelated untracked reports, research notes and METHOD-077 reference files
were preserved. Git history was not rewritten.

## Lessons extracted

- [Engineering safeguards](engineering.md): measured ownership changes,
  rejected EnTT/mock-lifetime ideas, deterministic asynchronous tests and
  bounded GPU-path claims.
- [Registration and atlas lessons](registration-and-atlas.md): failed retries,
  affinity positivity, posterior mass, consistent row repair, truth-based
  registration assessment and atlas quality tradeoffs.
- [Property-guided atlas results](../methods/property_guided_atlas.md#historical-correctness-observations):
  fragmentation costs and the boundary of the independent audit.

For future local-only experiments, preserve the source commit, command, packet
identity, all cell dispositions and relevant uncertainty in the result note
before deleting raw files. Those summaries allow interpretation and attempted
reproduction, but cannot support independent reanalysis of deleted raw data.
