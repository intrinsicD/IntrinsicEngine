# METHOD-019 optimized-CPU candidate result

## Outcome

No LOP-family optimized CPU candidate met the preregistered useful-acceleration
gate, so none was adopted as a public backend. All four candidates preserved
the frozen reference parity, state, output-shape, determinism, identity, and
no-fallback contracts, but every median paired runtime ratio exceeded `0.80`.
The ordinary geometry API, method manifests, runtime config/typed operation,
and Sandbox panel therefore remain `cpu_reference`-only.

## Fixed comparison

- Preregistration revision:
  `6f2277b079044c96d6c38405941393f93861384b`
- Implementation revision:
  `cfd0d9bdebe6c46969394d61593bf4305365ebec`
- Manifest: `geometry.lop_family.comparison.smoke`, fixture version 1
- Build: CMake `ci` preset, Clang 23.0.0, C++23, unsanitized
- Host: Linux 6.14.0-37-generic, Intel Core i9-11900KF, one measured CPU
  thread (the machine has 8 cores / 16 hardware threads)
- Sampling: one untimed warmup pair, then five alternating measured pairs;
  statistic is median `candidate_ns/reference_ns`
- Parity bounds: position and normal RMS `<= 1e-6`, L-infinity `<= 2e-6`
- Adoption: parity, deterministic identity, no fallback, and ratio `<= 0.80`

| Strategy | Reference state | Candidate state | Reference median (ms) | Candidate median (ms) | Paired median ratio | Max RMS delta | Decision |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| LOP | `not_converged` | `not_converged` | 38.155045 | 36.838666 | 0.961823997 | 0 | not adopted |
| WLOP | `empty_neighborhood` | `empty_neighborhood` | 17.486181 | 16.867073 | 0.966844794 | 0 | not adopted |
| CLOP | `mixture_not_converged` | `mixture_not_converged` | 259.851969 | 259.425509 | 0.998817796 | 0 | not adopted |
| EAR | `success` | `success` | 292.048554 | 301.978827 | 1.037145476 | 0 | not adopted |

The WLOP and CLOP fixtures are intentional failure fixtures under the frozen
protocol: exact matching failure status plus an empty payload is successful
parity evidence. The result does not reinterpret failure as acceleration.

## Literature and implementation boundary

The evaluated execution changes follow the local-support structure of the
original LOP (Lipman et al. 2007), WLOP (Huang et al. 2009), CLOP (Preiner et
al. 2014), and EAR (Huang et al. 2013) formulations: traversal scratch reuse,
exact sorted neighborhood reuse, cached Gaussian-product factors, and
underflow-only component pruning. The review also covered FLOP/KLOP,
incomplete-gamma LOP kernels, L0 and graph/intrinsic resampling, EC-Net, and
cross-field upsampling. Those change objectives, kernels, neighborhoods, or
priors and remain separately named methods rather than hidden accelerations of
the reference oracle.

The result supports only the bounded decision above on this exact fixture and
host. It is not a cross-host slowdown claim and does not rule out a future
separately preregistered implementation using a materially different exact
data structure or a named method extension.

## Evidence and replay

- Frozen protocol:
  [`METHOD-019-protocol.md`](METHOD-019-protocol.md)
- Executable manifest:
  [`../../../../benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`](../../../../benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml)
- Claim-eligible schema-v2 result:
  [`../../../../tasks/evidence/METHOD-019/experiment/inputs/benchmark_result.json`](../../../../tasks/evidence/METHOD-019/experiment/inputs/benchmark_result.json)
- Auditable per-strategy rows:
  [`../../../../tasks/evidence/METHOD-019/experiment/inputs/strategy_rows.jsonl`](../../../../tasks/evidence/METHOD-019/experiment/inputs/strategy_rows.jsonl)
- Portable custody bundle:
  [`../../../../tasks/evidence/METHOD-019/experiment/runs/run-001/bundle.yaml`](../../../../tasks/evidence/METHOD-019/experiment/runs/run-001/bundle.yaml)

Replay uses the exact command recorded by the custody protocol. The clean
singleton is sealed with source state `clean_commit`, the implementation
revision above, and append-only run/attempt identity
`method-019-confirmation-v1` / `attempt-001`.
