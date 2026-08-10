# Superseded METHOD-038 custody attempts

`20260810-task-status-advance/` preserves the first frozen 10k Automatic
scratch replay, portable bundle, and independently rejected audit. Its result
is scientifically unchanged, but scratch/non-claim-eligible custody binds the
live task bytes. Recording the audited negative result in the active METHOD-038
task therefore advanced that task hash and made run `scratch-001`
non-canonical. Canonical run `scratch-002` replays the same immutable gates
against the final checkpoint task wording; run 001 remains untouched for
auditability.

`20260810-fixture-cohort-runner-change/` preserves canonical run
`scratch-002`, its two sealed supplied-curvature results, portable bundle, and
independently rejected audit. Checkpoint 2 deliberately changes the private
profile runner by adding cold/reusable descriptor and continuous-boundary
fixtures, so scratch-002's frozen runner hash can no longer remain the live
protocol. C40 continues to bind these immutable historical paths; the new live
protocol starts at `scratch-003` and does not reinterpret the old result.
