# METHOD-020 Vulkan preregistration v3

Frozen after CPU-oracle screening and before changing or executing the v3
Vulkan parity fixture. The implementation boundary, candidate/capability
matrix, backend-identity requirements, parity tolerances, repetition counts,
and descriptive-only timing interpretation remain those in the original
`METHOD-020-protocol.md`.

## Protocol lineage

V1 and v2 are preserved rejected protocols: each WLOP fixture failed its CPU
prerequisite before Vulkan submission. Their dispositions are recorded in
`METHOD-020-v1-screening.md` and `METHOD-020-v2-screening.md`. V3 uses stable
dataset ID `builtin.lop_family.gpu_vulkan.v3` and benchmark ID
`geometry.lop_family.gpu_vulkan.v3.smoke`.

## Frozen confirmation fixtures

For both planes, let `u = column / (width - 1)`,
`v = row / (height - 1)`, `y = 2v - 1`, and
`z = 0.018 sin(13x + 7y) + 0.006 cos(5x - 11y)`.

- LOP: `32 x 32`, `x = 2u - 1`, 1,024 input points, 256 output
  points, support radius `0.18`, repulsion weight `0.35`, eight iterations,
  zero convergence tolerance, and seed `1901`.
- isotropic WLOP: `40 x 24`, `x = 2u^1.7 - 1`, 960 input points, 240 output
  points, support radius `0.32`, repulsion weight `0.35`, eight iterations,
  zero convergence tolerance, and seed `1902`.

The WLOP radius was selected before Vulkan execution from the bounded CPU
screening in `METHOD-020-v3-screening.md`. The CPU oracle at `h=0.32` returns
a finite 240-position `not_converged` last iterate after eight iterations with
no empty neighborhoods. A CPU hard failure still rejects the strategy before
Vulkan execution.

## Frozen decision rule

CPU and GPU receive identical float positions and parameters and use the same
deterministic subsample ordering. Status, cardinality, iteration count, and
convergence match exactly. GPU-versus-reference and same-host GPU-repeat
position error are bounded by RMS `5e-4` and L-infinity `2e-3` world units.
The actual backend must be `gpu_vulkan_compute`, fallback must be false, and
all output positions must be finite.

The benchmark retains one untimed warmup and three measured CPU and GPU runs.
It records request-to-completion time and the available GPU timing source but
makes no speedup claim. Failure of a frozen gate leaves that strategy CPU-only;
another fixture or tolerance change requires another protocol and benchmark
identity.
