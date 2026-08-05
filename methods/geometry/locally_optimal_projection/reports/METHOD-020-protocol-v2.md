# METHOD-020 Vulkan preregistration v2

Frozen after the v1 WLOP oracle rejection and before changing the executable
GPU parity fixture. The implementation boundary, candidate/capability matrix,
backend-identity requirements, parity tolerances, repetition counts, and
descriptive-only timing interpretation remain exactly those in
`METHOD-020-protocol.md`.

## Why a new protocol identity exists

The v1 isotropic-WLOP CPU oracle failed with `empty_neighborhood` after one
iteration, before Vulkan execution. The rejection is preserved in
`METHOD-020-v1-screening.md` and does not count as GPU evidence. V2 replaces
the whole confirmation dataset under stable ID
`builtin.lop_family.gpu_vulkan.v2` and benchmark ID
`geometry.lop_family.gpu_vulkan.v2.smoke`; v1 remains immutable evidence of
the rejected attempt.

## Frozen confirmation fixtures

V2 reuses the already CPU-confirmed METHOD-019 fixture definitions rather than
selecting a radius by trial against the Vulkan candidate. For both planes, let
`u = column / (width - 1)`, `v = row / (height - 1)`,
`y = 2v - 1`, and
`z = 0.018 sin(13x + 7y) + 0.006 cos(5x - 11y)`.

- LOP: `32 x 32`, `x = 2u - 1`, 1,024 input points, 256 output
  points, support radius `0.18`, repulsion weight `0.35`, eight iterations,
  zero convergence tolerance, and seed `1901`.
- isotropic WLOP: `40 x 24`, `x = 2u^1.7 - 1`, 960 input points, 240 output
  points, support radius `0.20`, repulsion weight `0.35`, eight iterations,
  zero convergence tolerance, and seed `1902`.

The CPU oracle must first return either `success` or a finite
`not_converged` last iterate. A CPU hard failure rejects the corresponding v2
strategy before Vulkan execution and cannot be reported as GPU evidence.

## Frozen decision rule

CPU and GPU receive identical float positions and parameters and use the same
deterministic subsample ordering. Status, cardinality, iteration count, and
convergence match exactly. GPU-versus-reference and same-host GPU-repeat
position error remain bounded by RMS `5e-4` and L-infinity `2e-3` world units.
The actual backend must be `gpu_vulkan_compute`, fallback must be false, and
all positions must be finite.

The benchmark retains one untimed warmup and three measured CPU and GPU runs.
It records request-to-completion time and the available GPU timing source but
makes no speedup claim. Failure of a frozen gate leaves that strategy CPU-only;
another fixture or tolerance change requires another protocol and benchmark
identity.
