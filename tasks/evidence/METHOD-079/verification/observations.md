# Preparation observations

- Baseline real-path technical checks passed before instrumentation (baseline-technical.txt).
- First native probe build rejected C++ exceptions under the repository's -fno-exceptions contract. Probe-only IO failures now emit a diagnostic and exit 79; no engine contract changed.
- Analytic controls pass; removing spectral truncation and discarding negative signs each makes the controls fail (controls.log).
- Initial technical run passed both native endpoint parity and exact snapshot replay/corruption rejection. HTML assembly failed because Matplotlib Axes.set does not accept linthresh. Changed to set_yscale and added an early unresolved HTML skeleton so a report assembly failure preserves an explicit non-verdict. No scientific values were inspected.
- Final report path and all technical configurations pass; native endpoints and captured algebra remain bitwise replayable. All 28 original Accelerated cases pass. Real total-alarm test confirms child termination and failure-before-report. Numeric technical outputs remain uninspected as research results.
