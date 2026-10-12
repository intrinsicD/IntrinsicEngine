# Reporting repair check

1. User question: "Dann bitte einmal testen, vollständige Konvergenz usw"

The raw auditor found that the frozen reporter requires native JSON and therefore
omits the available curves of interrupted cells. Reuse METHOD-081 `compare.traces`
and the installed Matplotlib/NumPy libraries; the complete-report helper cannot
accept a censored trace without fabricating a native result. A separate version1
appendix is the smallest repair preserving the original analysis and all233 hashes.
Scope: one report source, its generated HTML/two figures, this known-answer check,
and evidence/task recording. No engine callers, fixtures, config, exports or C++ tests.

Predictions were written in `postrun-check.py` before generating the appendix:
two named sections retain3726/3728 completed M-steps, their figures exist, unavailable
endpoints/termination are explicit, both original and counterexample verdicts remain,
all local links resolve and all233 hashes match. `postrun-before.log` demonstrates
failure before the repair; `postrun-after.log` records success after it.

The first report generation rejected an extra retained RSS diagnostic field in the
outcome record. The report assertion was narrowed to the actual disposition keys
(`kind` and `reason`); no prediction, raw outcome or scientific threshold changed.
Both charts were inspected for readable labels, curves and interruption boundaries.

Verification command:
```
python3 tasks/evidence/METHOD-082/verification/postrun-check.py
```
