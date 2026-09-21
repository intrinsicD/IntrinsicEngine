# Rejected mock lifetime compilation pilot

Decision: abandon the extraction and restore source. It adds 42 lines without a
useful diagnostic reduction. This is a closed feasibility experiment, not a
build-speed benchmark or a new obligation on RUNTIME-270. BUILD-006 explicitly
excludes engine-source optimization; the prior BUILD-009 and fixture work are done.
No existing open acceptance criterion was completed by this experiment.

Baseline: `e11c7c37f67e4f6e76e6ceb7dbaabdf408ca74cc`, clean checkout. The archive
contains the exact rejected two-file patch and before/after source hashes.
Existing `MockRHI.cpp` was the proposed owner; no new translation unit or abstraction.
All other source stayed unchanged, and the two edited files were restored exactly.

Candidate ranking: remaining mock lifetimes reached 50 source translation units
and showed repeated container instantiations. JobHarness had 13 source consumers
and only 0.239 s of parsing in MeshMethods, so ranked lower. Asset-import coverage
was expensive in retained C104 data but had no established repeated mechanism;
file size alone did not justify a change. Fresh mesh/clustering traces selected
the mock pilot, but independent review found only 76.9 ms of mock-tagged function
instantiation in MeshMethods; class layout cost would remain.

| Direct compiler invocation | Before, s | Candidate, s |
| --- | ---: | ---: |
| MeshMethods | 20.372 | 20.557 |
| ClusteringMethods | 17.794 | 17.772 |
| Shared MockRHI owner | 1.160 | 1.434 |

One diagnostic sample per arm/source, Clang 23 ci Debug, identical existing BMIs,
compiler flags and source paths, serial execution, no compiler cache, traces on.
Object/trace output directories differ. Normal desktop load was uncontrolled.
These are not statistical estimates, target rebuilds, or clean builds. Scanning,
linking, prerequisite compilation and other consumers are excluded. The apparent
owner regression and negligible consumer changes are retained without causal claims.
All six compiles succeeded; 11 default/copy/move/noexcept traits for each of four
mock classes matched. No unit tests, sanitizer or GPU runs were needed for the
restored source. The final repository change contains only this negative record.

Stop rule: abandon if fresh traces/pilot show no useful reduction. A survivor
would need >=5% and >=0.5 s representative target-rebuild improvement and <=2%
clean-build regression. Those target criteria were never tested because the pilot
stopped the experiment. No speedup or target-regression claim follows.

Claude Fable 5.1 reviewed the plan and fixed experiment. The next useful diagnostic
would isolate dominant non-mock property/registry instantiations and establish a
common owner across real consumers; it is not an approved implementation or a task.
