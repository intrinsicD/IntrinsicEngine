# ICP spatial integration verification

[C77](../../logic/claims.md#c77-bounded-canonical-domain-icp-and-shared-correspondence-integration)
binds the local implementation result. [Source hashes and run metadata](../diagnostics/registration_spatial_2026-09-08/record.json)
identify the dirty working tree; this is not publication-grade performance evidence.

| Evidence | Result |
| --- | --- |
| Clang 23 ci CPU suite with host display access | 4,348 passed, one expected unsanitized LSan skip, no failures |
| Canonical operand contracts | All 64 domain pairs; properties preserved; undo/redo, stale input rejection and signed/nonuniform/zero scale checks pass |
| ci-vulkan, ASan+UBSan, general cohort leak policy unchanged | Two readback tests pass, 16.74 s and 46.72 s, with display off |
| Framed correspondence comparison | 1,024 live seeded samples, both ICP variants compared against matching CPU reference; zero matrix error; one CPU and one GPU target build |
| CPU kernel smoke | KD-tree 29.726 ms, LBVH cold 73.860 ms, warm 72.784 ms; local diagnostic only |
| Final runtime smoke, display off | KD-tree 212.936 ms, LBVH cold 234.388 ms, warm 231.579 ms; Vulkan cold 13,014.763 ms, warm 12,999.105 ms |
| Final runtime benchmark disposition | Failed unchanged 5,000 ms timing threshold; transform error 0.0; schema v2 validates the failed result |

The CPU kernel and runtime rows have different instrumentation/timed scopes and
must not be used as a cross-row speed ratio. Earlier display-active runtime
output is retained separately (before the signed-scale publication correction).
CPU KD-tree remains the default; all smoke outputs are non-claim-eligible.

The initial display-off CTest run timed out at 30 s. An unchanged direct run
completed every assertion in 46.715 s; consistent ~13 s GPU rounds and the
observed power state match the present-pacing mechanism documented by BUG-143.
BUG-179 records the 70 s internal time bound and specific 120 s CTest budget,
with every assertion, benchmark threshold and label retained. The first CTest
property guard failed to apply; its failed output is preserved alongside the
final passing run. No display setting was changed.

The direct diagnostic used a different, leak-enabled environment. It passed
assertions, then exited 1 with 240 retained bytes in three allocations reached
through pipeline creation. Unknown-module frames do not establish driver
ownership. BUG-180 records that unresolved finding; these results do not claim
whole-process leak freedom. See the [review](../../../docs/reviews/2026-09-08-icp-spatial-integration.md)
for workflow limits and the [method note](../../../methods/geometry/registration/paper.md)
for numerical assumptions and literature intake.
