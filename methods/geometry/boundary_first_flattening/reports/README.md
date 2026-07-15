# Reports

The executable correctness evidence is the focused geometry test suite plus
the manifest-backed `geometry.boundary_first_flattening.smoke` result. The smoke
records RMS conformal error, aggregate conformal diagnostics, closure
adjustment, failure reason, and a loose PR-fast runtime budget; it explicitly
sets `performance_claim: false`.

No comparative performance report exists. Any future speed or backend-adoption
statement requires a named baseline, recorded measurement conditions, and a
separately reviewed benchmark slice.
