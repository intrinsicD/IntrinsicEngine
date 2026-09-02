# Framework24 Product Convergence

## Objective

IntrinsicEngine must reach full user-facing feature and workflow parity with
Framework24 while becoming the demonstrably better everyday graphics and
geometry-processing environment in modularity, extensibility, reliability,
usability, and performance. Framework24 defines the observable capability
baseline; it does not define IntrinsicEngine's architecture, APIs, algorithms,
source layout, or graphics implementation. Those means may differ completely
and should be redesigned when a better C++23/Vulkan solution requires it.

The current module, layering, Vulkan/RHI, and reliability contracts are quality
floors rather than a frozen architecture. They remain authoritative until a
separate reviewed architecture decision changes them. Neither architectural
novelty nor superior performance compensates for a missing Framework24 feature
or user outcome.

`experimental/framework24/` is a read-only comparison source, never a
production dependency. `REVIEW-004` owns the one-shot final verdict. Until it
retires, Theme J is P0 and new research expansion is paused.

The source-backed feature census is maintained separately in
[`framework24-feature-inventory.md`](framework24-feature-inventory.md). A
golden workflow cannot be accepted while one of its inventory rows remains
unclassified or lacks a bounded owner.

## Feature-parity boundary

Feature parity is behavioral, not structural. Every registered user-facing
Framework24 feature in the inventory must have an operational IntrinsicEngine
equivalent or a strict superset that preserves every user outcome of the
original. Equivalent results may use different interaction design, data flow,
algorithms, APIs, CPU/GPU decomposition, or rendering technology.

Parity is a coverage floor, not a porting strategy or a two-stage instruction
to clone Framework24 before improving it. Each feature should be implemented
directly with the architecture and algorithm best suited to IntrinsicEngine,
subject to the observable parity and quality gates.

The six golden workflows are cross-cutting end-to-end gates. They do not
replace or narrow the row-by-row feature inventory. An inventory row cannot be
waived merely because it is inconvenient, Framework24-specific, or absent from
a selected golden workflow; changing that baseline requires a separate,
explicit user-approved goal revision.

## Non-regression constraints

Convergence must preserve all of the following:

- the layer boundaries in `/AGENTS.md` and app-to-runtime ownership;
- C++23 module interfaces with implementation-heavy code kept private;
- Vulkan behind the backend-neutral RHI, with no `Vk*` leakage;
- deterministic CPU reference implementations as method truth;
- validated config, agent/CLI, and UI controls sharing one apply path;
- fail-closed cancellation, stale-result rejection, undo/redo, persistence,
  sanitizer coverage, and Vulkan validation evidence;
- backend tokens only for implemented variants, with requested, actual, and
  fallback diagnostics.

No scorecard row may be closed by weakening one of these constraints.

## Evidence states

| State | Meaning |
| --- | --- |
| `Open` | The workflow lacks accepted closure evidence; the named task owns the proof or repair. This is an evidence state, not a claim that every underlying subsystem is absent. |
| `Foundation proven` | A bounded lower-level capability has accepted ARA/task evidence, but the full user workflow remains open. |
| `Candidate` | A matched run exists but is dirty, local-development, diagnostic, incomplete, or otherwise not claim-eligible. |
| `Accepted` | `REVIEW-004` cites a clean exact revision, sealed matched evidence where required, and passes the complete workflow gate. |

Capability and performance statements cite an existing ARA claim. Numbers from
an open task or an unsealed local run remain candidate diagnostics and cannot
turn a row green.

## Golden workflows

| ID | User workflow | Required outcome | Current evidence / owner | State |
| --- | --- | --- | --- | --- |
| W1 | Launch and orient | A fresh launch presents a lit, readable, non-overlapping workspace; file actions are discoverable; layout survives restart; the promoted Vulkan device renders without validation errors. | Vulkan presentation foundations: ARA C12-C15. Product closure: `RUNTIME-218`, `UI-048`, `UI-049`, `GRAPHICS-135`. | Open |
| W2 | Import and immediately work | Open or drop a representative small and large UV-less OBJ; base geometry becomes visible, selectable, camera-focusable, and method-ready without waiting for optional UV/texture enrichment. Progress and failure remain visible. | `BUG-158`; matched latency/memory owner `BENCH-001`; atlas cost owners `BUG-159` and `BUG-160`. | Open |
| W3 | Compute and inspect curvature | On a resident mesh, compute principal curvature once, reuse resident topology, publish minimum/maximum/mean/Gaussian scalars plus directions, and immediately show a useful signed visualization. Correctness and matched resident-input latency must pass. | Candidate formulation/regression record: ARA C46-C53 and A44. Runtime persists minimum/maximum/mean/Gaussian plus directions in one transaction with undo/redo. Product closure: `BUG-154`, `BUG-156`, `UI-050`, `BENCH-001`. | Open |
| W4 | Edit, undo, save, and export | Run a geometry operation, inspect its visible result, undo/redo it, save/reload the scene, and export the selected geometry with explicit loss diagnostics. | Mutation foundation: ARA C16. Product closure: `UI-046`, `UI-047`, `UI-048`. | Open |
| W5 | Inspect rendering and properties | Imported geometry has readable lighting; scalar, color, label, isoline, normal, and principal-direction properties are reachable on compatible domains; controls/results are not clipped. | Recipe/residency foundations: ARA C13-C15. Product closure: `RUNTIME-218`, `UI-049`, `UI-050`, `UI-051`. | Open |
| W6 | Select a method backend truthfully | Every integrated method has a canonical `cpu_reference`. Available optimized/parallel CPU and Vulkan implementations are selectable through the same config/agent/UI path; unavailable variants are not advertised; explicit and automatic requests report actual backend and fallback reason. | Bounded K-Means and LOP Vulkan evidence: ARA C11, C34-C36. Cross-method inventory and value gate: `REVIEW-004` / `BENCH-001`. | Open |

## Comparable-or-better measurement contract

`BENCH-001` owns the manifests and runners. A performance comparison is valid
only when both engines use the same source asset, equivalent requested output,
same host and power state, optimized non-sanitized builds, declared warmup and
sample counts, and separate cold-load versus resident-input measurements.
Report median, p95, peak resident memory, output diagnostics, source revision,
and the exact Framework24 comparison revision.

For time metrics, comparable means the upper confidence bound of the paired
Intrinsic/Framework24 median ratio is at most `1.05`; better means that bound is
below `1.00`. For peak resident memory, comparable means a ratio at most
`1.10`. A workflow may use a stricter absolute SLO, but not a looser matched
ratio without a documented product tradeoff accepted in `REVIEW-004`.
Correctness, visibility, or reliability failures override timing: a fast wrong,
invisible, blocked, or fallback result does not pass.

Required timed boundaries are:

- process start to first usable frame;
- import request to visible/selectable geometry;
- import request to geometry-method readiness;
- optional enrichment time and peak memory, reported separately;
- resident-mesh curvature request to published/visualized result;
- steady-state render preparation, execution, and complete frame p95;
- each accelerated method's end-to-end request, including transfer and
  publication rather than kernel-only time.

## Backend completion rule

The architecture supports a per-method backend axis; it does not fabricate
implementations to fill a matrix. Each integrated method must:

1. retain a deterministic `cpu_reference` implementation and correctness
   oracle;
2. declare only built and tested implementations in `method.yaml`;
3. expose `cpu_parallel` / `cpu_optimized` and `gpu_vulkan_compute` only after
   parity and end-to-end benchmark evidence shows useful value;
4. route config, agent/CLI, and UI requests through one selector that reports
   requested backend, actual backend, and fallback reason;
5. record a measured “not worthwhile” result instead of shipping a slower or
   maintenance-only variant merely to claim coverage.

## Final gate

`REVIEW-004` runs only after its static remediation dependencies retire. It
executes all six workflows on a clean exact revision, requires every
registered-feature inventory row to have a working equivalent or strict
superset with no lost user outcome, validates the complete CPU, ASan, UBSan,
and promoted-Vulkan gates, audits every method manifest and control surface,
and checks the matched Framework24 benchmark bundle. Any blocking finding gets
a separate scoped task added to `REVIEW-004`; partial evidence is not reused as
the final verdict.

Research focus resumes only after every golden-workflow row and every
registered-feature inventory row is `Accepted` and `REVIEW-004` retires.
