# Source review resolutions
- Claude reports no semantic blocker. All six braced call sites have one concrete
  span parameter; inspected the profiler virtual signature and framegraph/residency
  owners. Named vector lvalues keep pointer, count, mutability and storage lifetime.
- Both sort comparators are unchanged. libstdc++14 ranges::sort delegates to
  std::sort through the identity-projection comparator wrapper. Neither API promises
  stable equal-key ordering; preserve semantic barrier/placement tests and investigate
  any failure rather than automatically calling it over-specified.
- Keep the concise source rationale: selecting pointer/count avoids the vector
  range-constructor's concept evaluation. It is a mechanism, supported by controls,
  not a quantified performance promise. No measured delta or task history in source.
- Do not add a new span adapter or duplicate destination types at every initializer.
  Existing destination types and compilation enforce constness/type requirements.
- Planning suggestions for five runs of every trace control and automatic stopping
  on a few-percent variance were not adopted. Controls select a mechanism; the
  accepted source gets a separately frozen matched target benchmark with all samples.

# Protocol review resolutions
- Claude's proposed blockers were checked against the actual helpers and host.
  Clang 23 is installed and selected in the canonical ci cache; Clang 20 is the
  minimum, not this run's compiler. The benchmark uses a fresh matching build.
- Canonical nested metric objects validate; a synthetic shape preflight was run
  before freeze. It is not stored as a measurement result. log_window explicitly
  preserves the Ninja header; the runner now asserts that contract. Empty no-op
  windows produce zero-duration critical paths.
- The scratch parent already exists (it holds the runner); mkdir nevertheless now
  creates missing parents. Response-file selection now fails with an explicit
  diagnostic unless exactly one exists. Configure cache paths are checked against
  the owned worktree/build; the ci preset exports compile_commands.json.
- Added an 8 GiB setup headroom guard; 19 GiB is available. Preserve all samples
  and failures, treat BMI pre-reading as warm-cache, report all values/spread, and
  distinguish target wall, producer duration and maximum single-process RSS.
- No statistical, cold-build, whole-engine, importer, cross-host, runtime or GPU
  claim. Single trace controls and final source trace remain separate diagnostics.
