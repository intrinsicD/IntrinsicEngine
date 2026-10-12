# Intent and assessment

1. "Bitte pass unsere implementieren entsprechend dieser Punkte des Original Codes an. Dann wiederhole den Vergleich, auch mit dem Original."

Prior instruction "Ok ohne Claude" persists: two fresh Codex reviewers substitute for
Claude+Codex. This is operator-directed CPD/BCPD work beyond Framework24 P0; no separate
goal file exists. The shortest path is the reviewed shared-E-step adaptation plus one
fixed comparison using the existing six cases and the real author executable. Every cell
serves Q1: old/adapted controls bind the change; the author program provides an independent
implementation reference; full/partial cases and three seeds expose coverage/draw dependence;
four replays expose repeatability. No unrelated benchmark or new registration method.

Full tier. The adaptation copies the original ridge/solve, union identities and sigma trigger;
it intentionally keeps our exact row repair, error checks and bounded truncation. No answer
arrived to the optional question about copying the author's mass-clamp/fabs shortcuts; the
recommended retain-guards default was stated before implementation. These retained differences,
G bases, stop rules and original RNG coupling prevent claims of total upstream equivalence.

## Hypotheses and frozen predictions
P1: the adapted guarded policies preserve regular native convergence and agree with the
same-version Auto endpoint. For each adapted arm/case: native success+converged, RMS≤.001
and maximum≤.01 world units to adapted Auto. This is an endpoint consistency screen, not
truth recovery. An ineffective/unstable adaptation can still use Nyström while violating
these limits, exhausting the cap or failing numerically.

P2: a separate stronger registration screen asks whether each full native variant learns
the fixture. Truth RMS must improve at least10% over both the original unregistered source and
the unchanged serialized native source mapped into the target world frame. Thus the
threshold is .9 times the smaller baseline RMS. Centered spread ratio to truth must
stay in[.5,2]. This excludes improvement from centering alone. Previous METHOD-082 already refuted this
for partial overlap, including Auto; improvement is not assumed from numerical convergence.
A no-op, collapsed or consistently wrong registration must fail this screen even if all
variants agree. All observed, hidden, clutter-replaced and boundary-ambiguous points are
reported separately so full-set averages cannot hide where the error sits.

P3: for each paired case, adapted union's truth RMS is no worse than1.01 times upstream
Nyström+KD truth RMS plus2e-6 world units for upstream endpoint rounding/export uncertainty.
This is a descriptive adequacy comparison between complete implementations, not a causal
attribution of any difference to the E-step. The direct+KD upstream arm is an additional
native reference; its RNG-coupled G change is explicitly not called a clean Nyström ablation.

Screens are evaluated per case and summarized across fixed cases. One valid counterexample
refutes a universal screen, even if another case is censored. Unavailable required endpoints
are unevaluated; numerical failure is a failure to converge, not a missing successful point
set. Raw numbers and negative results remain in the HTML regardless of verdict. Equal
numeric tolerances do not imply equivalent stopping. Bitwise endpoint/non-timing trace
replays are reported separately; any drift limits conclusions at its observed scale and
must be included in the post-run audit, never silently normalized away.

## Controls and measurement checks
`../check.py` tests the actual report pipeline with known endpoint/trace files. It asserts
positive endpoints pass; raw and native-frame no-op, collapse and wrong-but-consistent fail learning; caps,
explicit numerical failures and shifted adapted endpoints fail P1; missing/nonfinite data
and malformed original history are invalid; censored/unstarted cells remain unevaluated;
replay endpoint, final-state and planned-local-flag drift is detected. Missing Auto
endpoints are unevaluated unless another observed condition refutes convergence.
Nonfinite original sigma, scale and mass are rejected before reporting. Removing the agreement gate lets its negative control escape.
Real engine, upstream and stage-event report figures are rendered in this control.
Round1 found that centering alone passed P2 on all three full cases, that missing Auto
was treated as disagreement, and that original final nonfinite metadata could pass the
reader. The new synthetic test failed before the fix on native_noop; all six actual
frame-aware null probes must now be rejected. Removing each repaired control must fail
the assert-based check. These are prospective measurement repairs, with no solver
outcomes examined or thresholds calibrated from a new run. The original
incorrect parser reuse was caught because the new trace has one more field; the old frozen
parser was retained and a strict new schema reader used. A first regression control moved
Auto too, correctly passing consistency; the corrected control shifts only approximated
arms. The wrong-but-consistent control deliberately retains the shared-error case.

`../verification/adapter-check.py` asserts both executables reject malformed point rows
and failed JSON/binary writes. Changing truth cannot change their registered positions.
Old/adapted exact full-rank first updates are bit-identical. The unmodified author program
matches the same full-rank first update within5e-6 normalized units, accommodating its six
output decimals. This is a known-equation/input-adapter control, not large-scale parity.
The engine's regularized duplicate-landmark oracle, weighted row-repair oracle, union
identity/quota and transition-boundary tests passed; before-change/ridge and removal
mutations failed as predicted. Focused geometry/Bayesian/runtime:62/62. Reference detail is
`../code-review.md`; no scientific acceptance derives from those unit tests alone.

The fixtures and raw views were already inspected during METHOD-082; the actual files and
first/last-point before/after views are retained. Normalization/export/inverse-frame checks
are asserted before any run. No new-data calibration or thresholds inferred from this run.
The largest real configurations pass the prefix technical test with the final5000-step
setting; observed values are not read as registration results. No new result-bearing pilot.

## Replication, interpretation, limits
Replication units are six fixed fixture cases, paired across nine arms. Three seeds per
coverage regime are deliberately descriptive: no population confidence interval or general
partial-overlap claim. Four replay cells cover the newly added random policy and original
program in both regimes; they do not certify every seed's floating-point determinism.
Decision-relevant sizes are the .001/.01 endpoint thresholds,10% truth improvement, and1%
original-relative RMS comparison. Serialization uncertainty is below these quality scales;
a smaller observed difference or comparable replay drift is inconclusive, not a win.

No runtime hypothesis or speed claim: distinct G implementations, BLAS/compiler/runtime,
native stopping lengths and CPU-slot contention make wall time a diagnostic only. No
parameter is retuned from the outcomes. Maximum5000 and1800s replace the prior900s censoring
budget while retaining a finite bound; a further timeout remains unevaluated. Original
loss/trajectory observability is limited as described in Layer A. We answer native stop
and shared endpoint-quality questions, not full variational-objective convergence, dense
G parity at100k, optimized GPU parity or proof of a global optimum.

Kill only for enforced resources or technical invalidity. Unexpected scientific outcomes
are reported and the authorized fixed plan continues. Any changed analysis is a separately
labeled version preserving raw output and frozen verdicts. After the run an independent
referee recomputes conclusions from raw artifacts before AC4 is accepted or claims recorded.
