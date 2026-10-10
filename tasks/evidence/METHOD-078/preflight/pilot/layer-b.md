# Layer B — diagnostic intent

## User question (verbatim)
1. "Dann bitte nyström Nutzung untersuchen und verbessern"

Goal served: operator-directed continuation of METHOD-077. No standalone goal file applies.
The shortest next step is to distinguish the first rejection from subsequent cached suppression,
and test whether removing that suppression buys useful approximations or just repeated failures.
The priority is unchanged accuracy and less total work, not an accepted-iteration count alone.
This is a pilot, not a production-policy acceptance or a ranking of complete registrations.

## Hypotheses and predictions for question 1
- H1: sigma-only memory suppresses viable later approximations as source/weights/landmarks change.
  Prediction: candidate accepts at least one later iteration for which baseline is suppressed.
- H2: the fixed cost cap stops landmark growth before an acceptable approximation. Prediction:
  attempted numerical/accuracy rejections followed by a logged cost stop.
- H3: the narrow-kernel approximation remains invalid/inaccurate even when retried. Prediction:
  additional candidate attempts but no additional accepted iterations; wall time can increase.
An ineffective change shows no extra accepted iterations, or extra iterations without a clear
end-to-end time reduction, or worse numerical outcomes. We do not interpret Nyström sparsity alone
as a bug. First-failure diagnostics may expose a different next scope; no adaptive change is allowed.

## Scope and limitations
Seed 1, the `full` fixture and two representative sources (point/fixed and box/resampled), 20
iterations, keep the cause of early fallback observable at bounded cost. They do not represent all
nine variants, partial overlap, full convergence, or unseen shapes. The existing METHOD-077 Auto
reference failed its truth gate; this pilot retains truth error and before/after visuals and cannot
establish successful registration. Relative E-step equivalence to Auto is not proof of exact-dense
identity. Normalization and solver parameters remain those of the existing experiment; this is not
parameter tuning. Threads are 8 because this host has 8 physical cores (0-7), not the earlier host's
16; absolute times cannot be compared with the old run. Both binaries have identical diagnostics.

## Metrics, controls and predeclared verdict
- Attempts, accepted iterations and cached suppressions: counts; attempt rejection classified as
  numerical intermediate, sampled-error limit, cost cap, or cached suppression. Raw traces retained.
- Native registration-call `seconds` includes preparation, all E/M work and diagnostic output, excluding
  report generation. Two cold-start passes in reverse order expose order/noise; no population CI
  or generalization claim from one seed. Candidate must beat the fastest baseline repeat by at
  least 10% with its slowest repeat, in BOTH variants; otherwise do not adopt. This conservative
  within-pilot screen can only justify a separately reviewed follow-up, never shipping.
- RMS to current Auto reference <=1e-3, max <=1e-2 world units, same status/termination/iterations;
  compared repeats must have identical deterministic outputs named in `assessed`.
- Positive controls: Auto has zero Nystrom iterations; cost control has zero Nystrom iterations
  and RMS to Auto <=2e-4 (existing METHOD-077 floor tolerance). Negative control must exceed
  1e-3 RMS to Auto. Missing or failed controls make the comparison invalid, not passed.
- Raw sampled-error limit .001, eigen cutoff 1e-10 and half-dense estimated-cost cap are unchanged.
  Accuracy claims stay limited to the existing sampled check; neither the counter nor .001 is
  a global entrywise bound. No threshold is inferred or tuned from this pilot.
- `assessed` defines the frozen decision. Even if all screens pass, result is FOLLOW-UP REQUIRED.
  Otherwise retain the negative finding and restore production selection. No larger run is
  implicitly authorized by this packet, and no new variant is selected from it.

## Measurement checks and technical validity
Existing eight Nyström tests pass before edits. The added deterministic counterexample fails on
baseline: after a separated-source rejection, a fresh evaluator accepts the new input while the
reused evaluator suppresses it. The candidate passes all 29 Accelerated-suite tests. These tests
are not evidence of a runtime gain in METHOD-077.
The analysis self-check covers missing cells, a timing failure, a passing pilot screen and a
parity failure, missing comparison values, and a timeout with partial trace output. Removing
the timing rejection, stage parsing, or failure verdict makes the self-check fail. Numerical
positive/negative controls of the pilot are reviewed prospectively, not assumed passed.
Before freeze, technical-only calls of both binaries for both variants and the Auto reference
use the final point count/threads/iteration cap and assert successful execution, finite trace
values, and complete position buffers. Their efficacy and timing numbers are not inspected or
used as results. Memory and wall limits are enforced for the evidence calls. The native code is
CPU-only, no other builds or benchmark jobs are started during measurement.

## Stop and reporting rules
Manifest mismatch, missing approvals, process error, non-finite reported iteration state, missing
position buffers, per-cell wall/address-space limit or total wall limit are technical stops; they
cannot become a pass. A scientific negative result completes the remaining frozen cells.
The driver generates HTML from raw output after each completed cell; it includes mechanism traces,
stage boundaries, NLL and objective/prior curves and before/after views. BCPD has no additional
weighted loss in its implemented stopping objective; full variational ELBO is not claimed.
The original METHOD-077 evidence and its analysis are not changed. Any follow-up gets a new packet.
