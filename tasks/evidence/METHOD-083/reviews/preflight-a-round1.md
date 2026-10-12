# Preflight A — actual Round 1 response

Session `/root/method083_preflight_a`, fresh Codex reviewer; read-only/no delegation
and staged blinding instruction-only. No other review supplied before this answer.

Round 1 complete; no approval verdict yet.

Reviewed commit `a81051429`, packet digest `c03533d3e9736f289571bb03b995ea2e1e8072b4c7eb5ab3d0a961ce501a613b`. Native binaries, adapter, baseline/adapted geometry, upstream sources and implementation patch match their declared bindings. Read-only, blinding and no-delegation restrictions are instruction-only; I followed them.

Default-branch CI remains red for `nightly-deep`, `ci-source-coverage`, `ci-linux-clang` and `ci-docs`. Per the packet, existing BUG-240/241/242 own those failures.

**Reconstruction**

The plan comprises six stored fixtures—full/partial, seeds 1–3—with nine arms each, plus four seed-1 replays: **58 processes**.

| Arms | What they measure |
|---|---|
| Baseline/adapted Auto | Same engine-native reference policy, choosing accelerated exact/bounded evaluation. The new Nyström settings should be inactive. |
| Baseline/adapted FPS | Complete guarded Nyström policies with fixed per-cloud FPS landmarks. Adapted changes the landmark-space solve and introduces the sigma-triggered local evaluator. |
| Baseline/adapted box | Complete guarded policies with resampled box landmarks; the same adapted changes apply. |
| Adapted union | Guarded Nyström with fixed random identities drawn from the union, plus the adapted solve/local switch. |
| Upstream Nyström/direct | Two native upstream configurations. Changing J also changes RNG consumption before G construction, so this comparison does not isolate the E-step. |

All engine arms use the same FPS-derived rank-100 G basis. Upstream uses a distinct random Nyström G basis. Numeric seeds do not pair landmark identities across programs.

Inputs are separately centered and share one RMS-radius scale. Endpoint conversion uses the **target** mean. RMS/max errors and agreement thresholds are in original world-coordinate units. Sigma² is in solver-coordinate units²; mass is an effective correspondence count; sampled error and spread ratio are dimensionless. Engine NLL is a summed log-density quantity in the solver frame, not a complete Bayesian ELBO.

Engine stopping compares relative NLL/objective changes before a further M-step. Upstream stops on absolute sigma or sigma-change criteria after its minimum iteration index. The parsers correctly account for their different omitted final observations.

The report evaluates:

- Native convergence and endpoint agreement with same-version Auto: RMS ≤ `0.001`, maximum ≤ `0.01`.
- Truth RMS improvement ≥10% over the original unregistered source, with spread ratio `0.5–2`.
- Adapted-union truth RMS ≤ `1.01 × upstream-Nyström RMS + 2e-6`.
- Exact endpoint and selected trace replay equality.

**Concrete measurement problems**

1. **The native no-op can pass the truth screen.**  
   `prepare-inputs.py:13` separately centers source and target; `comparison.py:120` restores every endpoint using the target mean. Therefore an unchanged solver input becomes approximately:

   `source_world + target_mean − source_mean`.

   But `comparison.py:95–98` compares improvement against the original, untranslated source. I ran a deterministic read-only null-control assertion that such an unchanged native input must fail this screen. It failed for **`full_1`, `full_2`, and `full_3`**.

   `check.py` tests `p=source` in an identity frame, so it misses this native no-op. The screen cannot establish improvement attributable to registration beyond its centroid initialization. Layer B must resolve whether that distinction matters to the intended claim; the actual frame-aware null needs explicit treatment.

2. **Upstream final nonfinite sigma is accepted as native convergence.**  
   In `comparison.py:59–62`, final sigma and scale are parsed without the finite checks applied to engine outputs. A deterministic in-memory negative control with valid finite endpoints/history, `loops=31`, and final `sigma=nan` was accepted as successful native convergence. This contradicts Layer A’s finite-output validation promise. Final sigma, scale and other consumed numerical metadata need validation.

3. **Replay checking omits some observed state.**  
   `comparison.py:142` compares trace columns `0:8`, excluding both elapsed time and the planned-local flag. It also excludes final upstream sigma/scale despite the final update being absent from history. This is narrower than the adjacent “every observed non-timing field” comment. Endpoint replay remains checked.

**False successes and limits**

- Original-world identity, complete collapse and large wrong-but-consistent offsets are covered by synthetic controls. The frame-aware native no-op is not rejected on the full fixtures.
- Endpoint agreement can certify consistent failure; the truth screen is therefore essential, but presently permits centroid-only improvement.
- Aggregate RMS and broad spread limits can hide localized or subset failures. Observed/hidden/clutter subsets are reported, but do not independently gate the summary.
- The campaign cannot establish causal attribution among solve regularization, sampling, fallback behavior and the local switch.
- It cannot establish equivalence to upstream trajectories, G bases, stopping rules, or global-optimum convergence.
- Three paired fixture seeds are descriptive cases, not population uncertainty. Replays test repeatability, not independent replication.
- Accepted landmark counts and repair counts are unavailable, limiting mechanism diagnosis.
- Complete ELBO terms and upstream objective/trajectory observations remain unavailable.

The process/time/address-space bounds and fixed launch list are implemented. Technical checks described in Layer A reach the first completed update; they do not exercise every final-config late transition or normal endpoint termination.

One procedural issue awaits Layer B: the packet requests two Codex reviewers, while the supplied global/repository contracts require fresh Claude plus fresh Codex. An explicit user exception would resolve that difference.

I did not open Layer B, intent plans, old result reports/logs, or another reviewer’s verdict; no solver ran and no files were written. Awaiting Layer B; the two failed measurement controls remain unresolved.
