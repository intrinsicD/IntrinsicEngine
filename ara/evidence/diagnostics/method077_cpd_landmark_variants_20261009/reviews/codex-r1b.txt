Round-one reconstruction only, for supplied commit `3fea80e71` and packet digest `de8b3cc3d307a814f87c20761271ca79ff8afe64ed7eff241f2f4bafcdd1fa56`. Neither identifier was independently verified.

**1. Cells, metrics, and comparisons**

The 78 cells comprise six fixture/seed pairs, each with an Auto reference, nine landmark variants, one baseline repeat, floor control, and negative control. Every cell runs complete Bayesian registration, including common rank-100 deformation machinery built from up to 200 landmarks.

Point landmarks use farthest-point selection: mixed retains target identities and reselects moving-source landmarks; fixed retains both identities while source coordinates move; resampled changes both starting indices each iteration. Box landmarks sample the union of sigma-padded boxes: mixed updates boxes with the same sampling seed; fixed freezes coordinates; resampled updates boxes and seed. Random/Halton applies only to boxes.

The nominal 256 landmarks can double after rejection, subject to a cost budget and 4096 cap. Accepted counts persist. After total rejection, narrower kernels bypass subsequent attempts—including potentially different resampled draws.

| Output | Reconstructed meaning and units |
|---|---|
| `rms_to_truth` | \(\sqrt{\sum_i\|T(y_i)-truth_i\|^2/N}\), world-distance units. Uses source-index correspondence and stored float-valued, noiseless truth, including occluded points and points whose targets became clutter. |
| `rms_to_exact`, `max_to_exact` | RMS and maximum source-index displacement from that fixture/seed’s saved reference, world-distance units. Absent in reference cells; null when a requested comparison cannot be made. |
| `seconds` | Wall seconds inside `CPD::Register`: initialization, deformation setup, solving, and result construction; excludes fixture generation, comparison metrics, and file output. |
| `sigma2` | Fitted residual variance in world units squared; not truth error. |
| `scale` | Dimensionless fitted similarity scale, applied after nonrigid deformation. |
| `iterations`, `observed_iterations` | Completed M-step/update counts on successful runs. A final convergence-check E-step is excluded. |
| `nystrom_iterations` | Completed updates using accepted Nyström statistics; excludes attempts, rejections, and the final convergence-check E-step. |
| `max_sampled_error` | Dimensionless maximum sampled relative error over observed accepted Nyström updates. Zero when none occur. |
| `kernel_evaluations` | Reported E-step kernel-term count, including successful terminal convergence evaluations; excludes landmark selection, eigendecomposition arithmetic, deformation setup, and M-step work. Accounting exceptions appear below. |
| `status`, `termination`, `backend` | Categorical execution diagnostics. Successful completion includes iteration-cap and sigma-floor termination; backend can summarize mixed execution. |

Metadata records fixture, seed, input counts, requested landmarks/error limit, variant, and threads; it does not report actual landmark counts per iteration.

The reference is **Auto**, potentially dense, truncated, or fast Gauss, with tolerance `1e-6`; it is neither the canonical scalar Reference policy nor a full-rank deformation oracle. Nyström fallback disables fast Gauss, so the floor control and reference need not follow identical paths. See [solver accounting](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.cpp:806).

**2. No-op or ineffective behavior**

A landmark method that always falls back can match reference accuracy closely while reporting zero Nyström updates and zero sampled error. Its time includes rejected-attempt overhead; useful acceleration is not established by agreement alone.

Literal identity output gives truth RMS \(\sqrt{\sum_i\|y_i-truth_i\|^2/N}\), and reference distances measure the reference’s displacement from those unchanged points. Returning the reference itself gives zero comparison error regardless of reference correctness.

The floor control requests an extremely small positive tolerance; rejection is intended, but computed zero error could still pass. The negative control starts with two landmarks but may double or fall back. It therefore does not guarantee a persistently ineffective approximation.

**3. False successes and their visibility**

Collapse, premature stopping, or skipping useful updates can improve time, kernel counts, and possibly fitted variance. All-source correspondence-based truth RMS exposes broad collapse or wrong correspondence; scale provides another clue. Neither completion nor low sigma² certifies registration quality. Shared failure with the reference can nevertheless produce excellent reference-distance metrics.

The sampled checker examines 32 target denominators and 32 source masses at fixed indices. Source-mass checks reuse approximate denominators; PX moments are not checked directly. In the full fixture, eight of 32 sampled target indices are clutter, despite clutter being 5% overall. Unsampled errors and moment errors can therefore escape this diagnostic. See [sampled checks](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1522).

Truth RMS can dilute localized failures; maximum error is only against the reference. No collapse/null-output control or quality-threshold enforcement appears in the inspected driver. Existing controls expose fallback and weakened approximation, but their intended signatures are not asserted there.

**4. What remains indistinguishable**

These cells cannot isolate landmark selection cost from eigensolves, fallback, M-steps, or changed convergence length. They lack actual-count/rejection histories and per-phase timings.

Three fixture seeds do not independently replicate landmark randomness. Only the baseline receives timing repeats; fixed ordering confounds variants with machine drift. Full versus partial also changes target count, clutter fraction, and normalization. Results cannot establish general behavior across shapes, deformation strengths, noise levels, hardware, or unseen-region errors separately.

**5. Execution and recording risks**

The [driver](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py:88) serializes cells and enforces a 600-second subprocess timeout; worst-case cell time totals 13 hours. No memory limit or overall deadline is enforced. Layer A’s resource estimates remain unverified.

Material recording risks:

- Restart skips every recorded outcome, including failures and unevaluated dependents. It does not validate previous commit, runner hash, point count, or configuration against this invocation.
- Result matching omits points, targets, and threads. Unknown `--only` identifiers silently execute nothing; the driver returns zero despite failed cells.
- Malformed JSON, truncated journal lines, and uncaught filesystem/process errors abort remaining cells. Writes lack locking and transactional completion.
- Unrecorded cell JSON/reference files can be deleted on restart. Reference writes are unchecked; hashing proves byte identity, not completeness.
- Early Nyström rejection returns before assigning its work counter; accounting then adds only the nominal target pass, potentially understating consumed work.
- Failure results discard accumulated solver counters, although observer counts may remain. Thus failure telemetry can disagree internally.

Instruction-only boundary preserved: no Layer B, prohibited material, external script, CLI, build, experiment, or delegation was accessed or invoked. Runtime behavior and claimed technical-test evidence remain unchecked; no verdict issued.