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

`20260811-fold-screening-controls/` preserves canonical run `scratch-003`, its
six sealed 10k fixture results, portable bundle, and independently accepted
audit. METHOD-038 checkpoint 3 changes the private profile runner again by
adding the paired-diagonal 30/45/60-degree fold controls and their shared
feature-classifier oracle. The old runner hash and task checkpoint are retained
unchanged here; the new live protocol starts at `scratch-004` and binds the
fold-control source checkpoint without reinterpreting scratch-003.

`20260811-fold-screening-schema-rejection/` preserves frozen run `scratch-004`,
its failed cell journal, unsealed raw benchmark output, and command receipt.
Every preregistered numeric control passed, but the schema-v2 sealer rejected
the runner's composite `cpu_reference_v1+feature_classifier` backend label as
unsupported before it could create a canonical result. The follow-up changes
only that serialization field to the registered `cpu_reference` backend and
requires a new protocol and run identity; scratch-004 is not positive evidence.
