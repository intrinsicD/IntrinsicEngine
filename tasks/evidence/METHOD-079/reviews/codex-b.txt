The reconstruction matches the approved intent: diagnose **“am ersten betroffenen Zielpunkt”** by comparing approximate/exact affinity sums, landmark distances relative to sigma, and the retained spectrum. The four cells support that bounded question and deterministic replay. An all-row census, successful registration, or definitive causal attribution is **not required**.

**Verdict: REVISE**  
Digest: `6bffaa05201d09fa03cbd3892be4044b1a01333f0278efd5fd27c4b981d3a4df`  
Frozen commit: `aeb05205bdf69d96f77bef3bd332f0cc7de92df0`

One execution-gate defect remains mandatory. The scientific scope and measurement design otherwise fit this diagnostic.

The applicable preflight checklist is:

| Item | Assessment and evidence |
|---|---|
| Questions, authorization, goal alignment | Satisfied. Layer B quotes the questions and approved first-point proposal. Every cell compares the requested quantities or checks reproducibility. No separate goal file applies. |
| Hypotheses and predictions | Satisfied. Persistent signed approximation error, arithmetic/operand sensitivity, hard-zero behavior and low inlier significance have distinct stated signatures. Unresolved causality is allowed. |
| Ineffective/no-op/degenerate outcomes | Satisfied for this scope. Missing snapshots remain unresolved; zero/sign disagreement cannot receive the persistent-negative label. Collapse and alignment quality cannot score as improvement because no improvement verdict exists. |
| Positive/negative controls and mutation checks | [controls.log](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/verification/controls.log) records known-answer checks and rejection of no-truncation, sign-loss and hard-zero mutations. Qualification: the hard-zero mutation changes the self-test’s local model, not native `ExpNonPositive`; native boundary behavior was inspected statically. |
| Final-configuration technical path | [technical.log](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/verification/technical.log) records both distinct configurations under the specified limits: endpoint parity, finite traces, capture, bitwise replay and corrupted-`g` rejection. The test source supports these assertions. No scientific values were needed for this review. |
| Repetitions, uncertainty, detection limit | Appropriate. Two identical executions per variant test reproducibility, not sampling uncertainty. Population intervals are n/a. The 1% spread criterion and accumulation heuristic are frozen descriptive rules, explicitly not rigorous numerical-error bounds. |
| Metrics, units and oracle | Substantially satisfied. Stable exact Gaussian row sums provide the oracle; normalized and world coordinates are distinguished. Rank changes are separated from arithmetic sensitivity. One derived display omission is noted below. |
| Frozen command/config/data/binaries | Sources and native diff matched the frozen revision in round 1. Commands, seeds, ordering and binary hashes are recorded. Manifest/binary verification itself was not independently executed. Runtime enforcement has the mandatory defect below. |
| Resource limits and stopping | Child address-space limits, phase timeouts and total alarm are implemented. [timeout.log](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/verification/timeout.log) records actual child termination and failure-before-report ordering. Scientific negatives continue; execution failures stop. |
| Reports and failure visibility | Technical tests exercise report generation, a synthetic failure banner and sign-disagreement classification. Raw operands, component tables, traces and before/after views are retained. These checks resolve the round-1 uncertainty about whether these paths had been exercised. |
| Engineering verification | [focused-tests.log](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/verification/focused-tests.log) records 28/28 accelerated-suite cases passing. This is recorded evidence, not a test run performed during this review. |
| Independent review and post-run audit | This is one independent review; no other verdict was consulted. Both required approvals must exist before execution. Post-run audit, bounded ARA recording and probe removal are specified future obligations. |
| Other experiment requirements | Training losses beyond the implemented objective, gradients, teacher/student controls, data splits, tuning, GPU parity and performance comparisons are n/a. The packet reports no applicable LESSONS file; repository-wide discovery was outside this review. |

**Mandatory finding:** [diagnose.py](/home/alex/Documents/IntrinsicEngine/tasks/evidence/METHOD-079/diagnose.py:139) uses Python `assert` for reviewer approval, manifest equality and output-validity gates. Inherited `PYTHONOPTIMIZE` disables them; the manifest subprocess inside the assertion is then never executed. The frozen command does not exclude that environment setting. Replace these gates with explicit checks, or explicitly refuse optimized execution before any starts. Verify that optimized execution cannot bypass approval/drift checks, then refreeze and bind the approvals to the revised digest.

The remaining round-1 findings are **nonblocking for this bounded configuration**:

- **Exact iteration count:** technical evidence establishes the requested prefixes for both configurations. Equality therefore serves as a reproducibility guard here. Its general incompatibility with legitimate early convergence remains a limitation for future configurations.
- **Snapshot validation:** several fields lack semantic/finiteness checks. Corrupted-`g` rejection is demonstrated, not universal corruption detection. Additional validation is optional hardening for this controlled producer/consumer path.
- **Failure-stage tags:** early eigensystem failures remain less specific than Layer A implies. Such failures cannot receive a captured-row diagnosis; retain them as unresolved rather than claiming complete stage attribution.

Other optional corrections: Layer B names `|S|/c`, but the report exposes its operands rather than that derived ratio; calculate it stably during the frozen audit or display it explicitly. The asserted zero separate Bayesian prior contribution remains unverified from the permitted engine files. The per-target failed-sum array could be reduced to per-chunk storage, without making that cleanup a gate.

**Code correctness review: REVISE** for execution-gate enforcement. **Ponytail complexity review: PASS**; no additional framework or broader experiment is warranted.

Not checked: scientific outputs, live binaries/manifest, CI, or another reviewer’s answer; no runs, edits or delegation occurred.  
Risk: execution under optimized Python can bypass required gates; this digest does not receive PASS.