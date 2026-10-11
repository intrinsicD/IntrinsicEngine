# METHOD-081 implementation plan

1. "Also heißt das identifizieren und exakt nachrechnen?"
2. "Dann versuchen wir es damit mal."
3. "Mach dann nochmal einen Test bitte"

Operator-directed CPD/BCPD work outside Framework24 P0. No separate goal file was found.
The shortest slice is the shared E-step implementation, then a bounded repeat of the known failure.

- Q1/Q2: In `NystromRows`, collect target rows with nonpositive/nonfinite approximate sums and omit their contribution to h. Recompute the complete rows with `ExactRow`, adding exact source contributions to P1/PX. Check source validity after the merge. Keep sampled error gates and Auto's Truncated/Dense fallback.
- Q1/Q2: Share the exact-row denominator/source accumulation already used by `ExternalRows`; preserve its existing prevalidation. Build SourceTree before ExactRow when absent. No new backend, setting, sampler, positivity promise or PSD regularization.
- Q2: Bound the additional two-pass work by the existing cumulative half-dense cost budget; over-budget repair rejects the attempt. Include repair work in kernel evaluation accounting, including rejected attempts. Preserve deterministic row/chunk order and weighted log shifts.
- Q3: Add a deterministic public-Evaluator regression before production edits. Two landmarks at scalar coordinates 0 and 1; source 1 then repeated 2; target 0 except one -1. Nyström's invalid row is known analytically; all other rows are exact at a landmark. Assert Nyström accepted, every sufficient statistic agrees with dense, weighted/unweighted and serial/parallel agree. Test high-invalid-row budget rejection and accuracy rejection after an invalid-row repair. Tests must fail without their corresponding mechanism.
- Q3: Build `IntrinsicGeometryRegistrationSlowTests` (plus the owning Bayesian target if different), run the accelerated/Bayesian suites, mutation-check the repair and budget/accuracy guards. The extraction touches the CPU side of ExternalRows; include its existing mocked-evaluator regression suite, no GPU algorithm change.
- Q3: Freeze a small paired baseline/candidate experiment on the established synthetic 100000-point fixture for fixed FPS and resampled union-box landmarks, with the existing registration runner and raw trace/output report. Determine exact cells, controls and budgets in a reviewed preflight after tests. No tuning based on the outcome, no general speed/positivity claim. Independent result audit.

Scope: EStep.cpp, its interface synopsis (no exported API change unless review proves needed), existing accelerated tests, relevant method docs, task/evidence/ARA records. Callers CPD and BCPD share Evaluator; Vulkan only shares the existing CPU exact-row merge. Config, UI, exports and fixtures require no new representation.

Reuse: `Impl::ExactRow`, `SourceRowTerms`, `ExternalRows` own weighted stable exact evaluation and deterministic per-source merge. Existing `NystromLandmarks()` is the test/diagnostic seam. Existing METHOD-078/080 runner and reports are evidence references, not automatically rerun or rewritten.

Done: scoped behavior passes analytic regressions and existing suites, independent code review passes, bounded reviewed comparison and raw HTML report answer what changed in accepted steps/accuracy/runtime. A further failure exposed by this repair is reported and not silently repaired in this slice.
