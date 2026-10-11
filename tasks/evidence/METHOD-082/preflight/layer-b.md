# Intent and assessment

1. "Dann bitte einmal testen, vollständige Konvergenz usw"

Prior constraint: "Ok ohne Claude". Operator-directed continuation of METHOD-081 outside
Framework24 P0; no separate goal file exists. The shortest path is running the already
reviewed candidate/baseline runners to their regular stopping rule, without another method
change. All cells answer Q1: three seeds test case dependence, partial overlap changes target
coverage, Auto gives a reference, original implementation gives the before/after comparison,
four replay cells check that late trajectory differences exceed nondeterministic replay.

Full tier because convergence/equivalence informs interpretation of the repair. P1: existing
row repair preserves regular solver convergence and agrees with Auto at the final endpoint
under both landmark choices. P2 is a separate, deliberately stronger registration screen:
the converged endpoints improve the known pointwise truth match over the unregistered source.
P2 is not assumed from P1; prior METHOD-077 already exposed substantial partial-overlap truth
error at150 iterations, which is why a wrong-but-consistent control matters here.

Convergence is exactly native status success and termination converged. Iteration cap at5000,
sigma floor and numerical failure are not regular convergence, and no successful file is
silently equated with it. Tolerance remains the existing relative objective1e-5; cap5000 is
over33 times the earlier150-step cap, with enforced resource bounds. This is convergence
under the existing stopping rule, not a proof of a global optimum, posterior correctness,
monotonic ELBO or convergence under arbitrarily tighter tolerances. The unobserved final
check is explicitly disclosed; no residual is fabricated from the previous observed pair.

Frozen complete comparison P1: all34 arms regularly converge; all endpoints have RMS≤.001
and maximum≤.01 world units to their matching final Auto; repeated endpoint buffers and
numeric traces excluding elapsed time are identical, as are termination/count. Equal final
iteration counts across different arms are NOT required. The .001/.01 tolerances are
unchanged from METHOD-081, respectively25% and2.5times one-coordinate noise SD(.004),
and small relative to the roughly unit-radius object. Auto is a bounded/truncated-capable
reference with shared rank100 deformation approximation, not an independent exact Bayesian
solution. All actual policies are reported. If any reference fails to converge, final
displacement is still descriptive but P1 cannot pass. A missing result blocks support,
never reporting the user's answer.

Frozen broader registration screen P2: P1 plus RMS to known truth ≤90% of the unregistered
source's truth RMS and centered RMS radius between .5 and2times truth for EVERY arm.
The10% improvement margin dwarfs the1e-5 dump error; this is a coarse improvement/collapse
check, not an absolute registration-accuracy claim. Shared imperfect local minima can pass
this coarse screen. Truth RMS/max and reduction are reported continuously even when it
fails. Full/partial observed, hidden and
clutter-replaced source-point subsets are disjoint, with counts; rounding-boundary-ambiguous
nonclutter points are a fourth group. All-point metrics keep every source point. Pointwise
truth assumes known synthetic correspondence; it need not coincide with the density objective.

No-op repair can pass P1/P2 if the original also converges; therefore these checks establish
preservation, not a mechanism gain. Accepted Nyström count AND accepted/observed fraction
are descriptive, especially at unequal trajectory lengths. METHOD-081 owns the matched-prefix
repair gain. Positive affinity sums do not establish pairwise positivity. Kernel accounting
changed between implementations; no work/speed gain is inferred from counts.

All weights are the unchanged native normalized-config values: omega.1, beta1, lambda2,
gamma1. No new combined loss is introduced. Exposed NLL raw=weighted, weight1, share100% of
logged NLL; objective-minus-NLL is only a diagnostic. Full variational ELBO terms are not
logged; this experiment cannot judge their individual convergence. Parameter/scale meanings
come from Params and the actual Solver::Step implementation, not a paper reconstruction.

Controls are deterministic known-answer tests before the run. `check.py` executes the entire
read/measure/report/verdict path on34 synthetic cell outputs: positive, all-arms cap,
candidate-only cap, sigma floor, numerical failure without endpoints, displaced candidate,
changed replay, all-arms unregistered identity, wrong-but-consistent translation and collapse,
missing trace, nonfinite endpoint and timeout. P1 distinguishes convergence/equivalence from
P2 learning. Bypassing convergence, endpoint agreement, learning or replay individually
breaks its counterexample test. A fifth mutation proves invalid measurement reports stop
the next native start. A real rendering check recovers the before/after picture after an
interrupted image pair. One boundary-ambiguous point tests subset accounting.
A native-shaped failure control (zero result iterations, retained observer history) fails
before the reader correction and passes after it. Reinstating the old equality also breaks
the complete-report numerical-failure control as the sixth mutation.
`null-probes.json` computes identity, centered collapse and displaced-truth
probes cheaply on all six actual fixtures before execution, with a known truth positive.
Existing METHOD-081 analytic tests/mutations establish exact repaired sufficient statistics
and retained guards; no new engine change or model training occurs. Unregistered/collapsed
outputs provide the information-removal controls; no redundant native null run is needed.

Replication: seeds1–3 are three paired synthetic cases per coverage condition, all retained;
not independent populations or confidence intervals. The four reverse-order repeats cover
full/seed1 replay only. The decision-relevant endpoint resolution is .001RMS/.01max and one
termination-category difference; report individual cells/ranges, not population significance.
No tuning, seed selection, post-hoc threshold choice or speed prediction. A single failure
can refute this all-cases preservation prediction; six passes cannot establish generality.

Technical test: final native configuration executes full+partial candidate paths with only
asserted complete finite output, valid termination category and consistent trace; values are
not research results. Resource/time guards are in code. Hypothesis/P2 failures continue the
frozen plan. A technical stop preserves the partial report. Unexpected outcomes require a
raw-output referee audit and remain reported; a new algorithm failure layer is returned to
the operator before any attempted fix.
