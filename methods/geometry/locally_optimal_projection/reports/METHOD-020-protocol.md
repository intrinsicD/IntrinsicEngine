# METHOD-020 Vulkan preregistration

Frozen before Vulkan implementation. This record fixes the backend capability,
parity, and benchmark interpretation. A failed parity gate leaves the affected
strategy CPU-only; it does not loosen a tolerance, change a fixture, or turn a
fallback run into GPU evidence.

## Frozen implementation boundary

The CPU `cpu_reference` implementation is the numerical oracle. The Vulkan
candidate may change execution and intermediate storage, but not the selected
LOP/WLOP equations, seeded initialization, stopping rule, output cardinality,
or publication semantics.

The candidate uses one runtime-private Vulkan implementation with an
`h`-sized dense cell grid. Source cells are count/scan/scatter built once;
projected cells are rebuilt per iteration. Each query visits the 27 adjacent
cells and exact-filters squared distance against the resolved support radius.
The shared graphics prefix-scan primitive supplies offsets, and one final
multi-range GPU transfer returns positions plus diagnostics. Convergence stays
on device and no per-iteration readback may steer execution.

The adopted candidate set is deliberately bounded:

- ordinary LOP;
- isotropic WLOP.

Anisotropic WLOP is capability-negative because normal preparation and the
anisotropic kernel are distinct work, not an isotropic flag. CLOP is
capability-negative because its Gaussian-mixture continuous attraction is not
the LOP neighborhood kernel. EAR is capability-negative because its
progressive insertion, clearance ordering, and normal refinement change
cardinality and execution structure. These three pairs must fail canonical
preview for `gpu_vulkan_compute`; they may not silently run an isotropic or
ordinary LOP kernel. A future task may preregister each independently.

An operational Vulkan device is required for positive evidence. Device,
pipeline, allocation, or transport unavailability before GPU execution falls
back to `cpu_reference` with `RequestedBackend=gpu_vulkan_compute`,
`ActualBackend=cpu_reference`, `FellBackToCpu=true`, and a diagnostic. Such a
run proves fallback only. A shader-reported algorithm failure is an actual GPU
failure and is not rerun as CPU under the same request.

## Frozen confirmation fixtures

The executable manifest
`benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke.yaml` is canonical.
Its built-in v1 suite is separate from unit-test screening and from the
METHOD-019 CPU comparison suite:

- LOP: a `24 x 20` plane over `[-1,1]^2`, with
  `z = 0.014 sin(9x + 5y) + 0.004 cos(7x - 3y)`, 480 input points, 128 output
  points, support radius `0.24`, repulsion weight `0.35`, six iterations, and
  seed `2201`;
- isotropic WLOP: a `28 x 16` plane whose normalized x coordinate is warped by
  `u^1.55`, with the same height equation, 448 input points, 112 output points,
  support radius `0.26`, repulsion weight `0.35`, six iterations, and seed
  `2202`.

CPU and GPU receive identical float input positions and resolved parameters.
Output correspondence is vector index: both paths use the same deterministic
subsample initialization and preserve its order. Status, output count,
iteration count, and convergence flag must match exactly. Positions must be
finite. The GPU is run twice on the same host and the repeat is checked under
the same positional bounds.

## Frozen metrics and decision rule

For each strategy, GPU-versus-reference position RMS and L-infinity deltas
must be at most `5e-4` and `2e-3` world units. These bounds admit expected
float shader reassociation without permitting a visibly different solution.
The same bounds apply between the two GPU runs. Any status, cardinality,
iteration, convergence, backend-identity, or finite-value mismatch fails the
strategy.

After one untimed warmup, three measured GPU runs and three same-fixture CPU
reference runs are recorded. `runtime_ms` is end-to-end request-to-completion
time, `gpu_time_ms` is the available GPU-operation duration (or an explicitly
identified host-timed command interval when timestamps are unavailable), and
`quality_error_l2` is the maximum strategy RMS delta. Raw strategy durations,
device identity, timing source, iteration diagnostics, actual backend, and
fallback state remain in the result diagnostics.

Timing is descriptive. There is no speedup threshold or performance claim in
METHOD-020. A strategy is exposed only when actual identity is
`gpu_vulkan_compute`, fallback is false, and every frozen parity check passes
on the confirmation suite. Screening failures and confirmation outcomes may
not retune this v1 protocol; a changed fixture or tolerance requires a new
protocol and benchmark identity.
