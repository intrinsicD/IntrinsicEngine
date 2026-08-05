# METHOD-020 Vulkan parity result

## Outcome

Ordinary LOP and isotropic WLOP are `ParityProven` through the production
`gpu_vulkan_compute` route on the two frozen v3 analytic fixtures. The
claim-grade v4/run-002 confirmation completed eight actual Vulkan requests on
an NVIDIA GeForce RTX 3050 with zero fallback. Its worst positional RMS error
was `1.6746e-5` against a `5e-4` bound, and its worst L-infinity error was
`1.31344e-4` against a `2e-3` bound.

Anisotropic WLOP, CLOP, and EAR remain explicit capability-negative Vulkan
pairs. They fail the shared availability preview instead of substituting a
different strategy or silently falling back.

## Fixed comparison

- Frozen v4 protocol source: `04ec3d256696cce3584721065cd1f6ca651d3d28`
- Protocol digest:
  `7774197b1d262b64214a9c255cd91104febe0ff48a23ed1db90e0b72cf4ee8b5`
- Manifest: `geometry.lop_family.gpu_vulkan.v3.smoke`, fixture version 3
- Device: NVIDIA GeForce RTX 3050, NVIDIA driver `590.48.01`, Vulkan API
  `1.4.325`
- Sampling per strategy: one warmup plus three measured executions
- Oracle: same-fixture `cpu_reference`, matched seed, parameters, cardinality,
  iteration count, status, and convergence state
- Parity/repeat bounds: RMS `<= 5e-4`, L-infinity `<= 2e-3`

| Strategy | CPU parity RMS | CPU parity L∞ | Same-host repeat RMS | Same-host repeat L∞ | CPU time (ms) | Vulkan service time (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ordinary LOP | 0.000016746 | 0.000131344 | 0.000006463 | 0.000061033 | 4.556277 | 89.446669 |
| isotropic WLOP | 0.000001025 | 0.000007278 | 0.000000964 | 0.000007123 | 12.252184 | 88.720473 |

Every bound passed. The reported Vulkan duration is descriptive host
service-command-to-applied-event time, not isolated device time. It includes
runtime scheduling, transfer, and publication overhead; `speedup_claimed` is
therefore false. These values support no acceleration claim.

## Evidence boundary

The result supports parity only for the two declared analytic fixtures, frozen
parameters, exact source revision, and recorded device/driver. It does not
establish scanner-corpus quality, cross-vendor or cross-device parity,
asymptotic or memory efficiency, isolated GPU-kernel timing, or speedup.

The earlier v3/run-001 actual-GPU output is preserved but independently
rejected because its pre-BUG-129 runner could report unavailable execution as
success. V4/run-002 reused the v3 fixtures and tolerances only after the runner
and custody path became fail-closed; its independent audit was accepted.

## Evidence and replay

- Frozen protocol:
  [`../../../../tasks/evidence/METHOD-020/experiment/protocols/v4/protocol.yaml`](../../../../tasks/evidence/METHOD-020/experiment/protocols/v4/protocol.yaml)
- Executable manifest:
  [`../../../../benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v3.yaml`](../../../../benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v3.yaml)
- Claim-eligible schema-v2 result:
  [`../../../../tasks/evidence/METHOD-020/experiment/inputs/run-002/benchmark_result.json`](../../../../tasks/evidence/METHOD-020/experiment/inputs/run-002/benchmark_result.json)
- Portable custody bundle:
  [`../../../../tasks/evidence/METHOD-020/experiment/runs/run-002/bundle.yaml`](../../../../tasks/evidence/METHOD-020/experiment/runs/run-002/bundle.yaml)
- Accepted independent audit:
  [`../../../../tasks/evidence/METHOD-020/experiment/runs/run-002/audit.json`](../../../../tasks/evidence/METHOD-020/experiment/runs/run-002/audit.json)

Replay and view commands are recorded verbatim in the frozen protocol and
portable bundle. The accepted result uses append-only identities
`method-020-vulkan-v4` / `attempt-002`; rejected run-001 remains alongside it.
