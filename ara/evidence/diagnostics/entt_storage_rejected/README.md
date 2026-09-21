# EnTT storage ownership feasibility assessment

Closed without a repository source change. The proposed `extern template`
allocation boundary fails the standalone diagnostic gate; it is not a measured
build regression or a rejection of every possible EnTT optimization. No task was
opened, no existing acceptance criterion was completed, and no new work was
attached to RUNTIME-270. BUILD-010 remains complete.

Starting source: `6c4e8164f` (clean). Existing BUILD-010 after-traces, rather than
rerunning completed experiments, identify 2.202/2.688 seconds of EnTT instantiation
interval union in MeshMethods/ClusteringMethods. Those unions include both
function and class events and are not wholly removable time. Four geometry-domain
component allocation chains recur in both; the textual census finds 52 source
files mentioning the selected aliases/qualified types (not a complete dependency
or instantiation census). Their existing owner is
`src/ecs/Components/ECS.Component.GeometrySources.cpp`.

Candidate ranking: (1) centralize those four component storage instantiations in
the existing owner, because recurring chains are the demonstrated cost;
(2) move registry lifetime bodies, but the sampled constructor event is only
about 60 ms; (3) move more test fixture calls, but existing helpers already own
shared construction and moving mutable reads behind new wrappers would expand
scope. Neither lower-ranked candidate justified another implementation pilot.

Hypothesis and stop condition were stated before writing the standalone probe:
extern declarations must suppress the allocation/storage chain before source
changes or full target measurements are justified. The component interface
currently includes only EnTT's forward declarations; importing full registry
machinery to declare private member instantiations adds a dependency cost.

The three standalone sources perform the same mutable component read. The
`assure` variant declares EnTT's private deduced-return member extern; the
`storage` variant declares its storage and signal-mixin classes extern. All
three compile successfully with Clang 23, C++23, O0/debug, no compiler launcher,
serial execution. Raw commands, sources, compiler version, dependency hashes,
trace events and logs are retained. The probe is intentionally outside the
repository and build directories, using a simple vector-bearing component;
it does not establish named-module legality or engine linking correctness.

Both candidates still instantiate `assure<Component>` and
`allocate_shared<...Component...>`. Class extern declarations reduce part of the
chain but leave constructor-template instantiation. Single diagnostic allocation
events are 79.4 ms baseline, 78.1 ms member-extern, and 48.6 ms class-extern.
These are mechanism observations, not additive totals or performance claims.
No target clean/incremental builds, receiving-owner cost, links, tests, sanitizer
or GPU checks were run: the feasibility gate stopped the candidate first.
Previously completed BUILD-010 target and CPU results are not credited here.

No third-party fork, public accessor facade or additional wrapper layer was
introduced. A future EnTT batch needs a different demonstrated boundary and
must account for registry-header propagation and owner compilation. This record
creates no follow-up obligation.

Claude Fable 5.1 independently recommends against opening this batch. Its
review includes unverified cost estimates and a larger optional module probe;
those estimates are not adopted as measurements here. The smaller feasibility
probe already failed the stated allocation-chain suppression condition, so the
optional probe and an engine-wide implementation were not pursued. The next
useful selection step is attribution of remaining project-owned templates,
where compiled ownership can be changed without third-party internals.
