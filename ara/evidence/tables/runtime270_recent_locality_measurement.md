# Recent mesh-field and fixture compilation measurements

Local observations recorded by C103. **No meaningful clean-build saving was measured.** Selected incremental rebuilds became modestly faster, while rebuilding the shared fixture implementation became slower.

Compared `beef1cfb8afa93f249f7a293caedd3fbf9f44eb7` (resolve full identity in the manifest) to `b3c18fd17add1a555540b57ba388d62726d39e87`, covering the four recent helper, equality, diagnostic and fixture commits. The earlier source also has one fewer regression test; this is a net batch comparison, not an isolated attribution to each commit.

Protocol: `ci`, Clang 23, Debug, Null/headless, four jobs, compiler cache disabled, identical preinstalled dependency fingerprints, exact clean source commits, identical source/build paths, fresh tmpfs build per sample. Source/dependency inputs are pre-read; this is not a cold-filesystem experiment. Order: before/after/after/before, two observations per arm, no discarded warmups. Normal desktop host, no CPU affinity/governor control. Target: `IntrinsicRuntimeContractTests` and its dependencies, not all engine/test targets.

## Timings

Elapsed target-build seconds include scanning and linking. Positive savings mean lower after median. Ranges are the two observed samples, not confidence intervals.

| Scenario | Before median (range), s | After median (range), s | Saved, s | Saved, % |
|---|---:|---:|---:|---:|
| clean | 428.356 (428.164–428.549) | 428.861 (428.540–429.182) | -0.505 | -0.12% |
| noop | 0.105 (0.105–0.106) | 0.101 (0.100–0.103) | +0.004 | +4.04% |
| curvature_impl | 8.633 (8.624–8.642) | 8.651 (8.626–8.676) | -0.018 | -0.21% |
| geodesics_impl | 6.881 (6.881–6.882) | 6.523 (6.515–6.531) | +0.359 | +5.21% |
| mesh_field_impl | 5.141 (5.135–5.148) | 5.179 (5.179–5.180) | -0.038 | -0.74% |
| clustering_test | 22.286 (22.250–22.321) | 21.826 (21.803–21.849) | +0.459 | +2.06% |
| mesh_test | 25.977 (25.924–26.029) | 24.751 (24.740–24.762) | +1.226 | +4.72% |
| visualization_test | 21.894 (21.863–21.926) | 21.215 (21.210–21.221) | +0.679 | +3.10% |
| fixture_impl | 7.656 (7.648–7.664) | 8.591 (8.586–8.597) | -0.935 | -12.21% |
| properties_header | 8.648 (8.638–8.659) | 8.658 (8.653–8.664) | -0.010 | -0.11% |
| mesh_field_interface | 39.580 (39.556–39.604) | 39.277 (39.260–39.294) | +0.303 | +0.76% |

No-op differences are only milliseconds and establish no useful saving. Clean, curvature and header changes are negligible; the interface difference is small. Every single-source probe compiles one source, property-header probes compile both declared consumers, interface probes compile 12, and no-op probes compile none. The compiled fixture owner cost is included in every clean build and measured separately.

## Verification and evidence

- Four canonical result files validated; all remain `claim_eligible:false`. These are descriptive local observations, not publication-grade or general performance claims.
- All 26 existing compile-tooling tests and 102 manifests passed. The new header-consumer check passed in all four real builds.
- Baseline runtime executable: 1,192 passed, two Null-platform skips. Current canonical `ci` configure and `IntrinsicTests` build passed; CPU CTest selected 4,862, with zero failures and one documented capability skip (4,861 executed passes). No sanitizer or GPU execution claim.
- Task-policy and documentation-link checks passed.
- The initial header probe exposed the runner assumption that a touched file produces an object. `params.probe_sources` now requires every declared consumer; the original source default remains. BUG-204 records the repair. The complete rejected attempt is retained outside the accepted population.
- The initial baseline CTest executable-prefix filter selected no tests; it is not verification evidence. The baseline executable was subsequently run directly and passed as above.

[Summary and all raw values](../diagnostics/runtime270_recent_locality/summary.json), [canonical results](../diagnostics/runtime270_recent_locality/results/01-before-1.json), [protocol](../diagnostics/runtime270_recent_locality/protocol.json), [raw evidence including rejected attempt](../diagnostics/runtime270_recent_locality/raw-evidence.tar.gz), [file hashes](../diagnostics/runtime270_recent_locality/evidence-index.json).

Reproduce the exact protocol with the checked-in manifest and retained
`runtime270_recent_locality/runner.py`, supplying a detached worktree, absent build directory and new output directory. The worktree must have the same preinstalled vcpkg packages available; the runner disables installation and checks their fingerprint.

The retained runner preserves the original measured bytes. BUG-204 subsequently
extracts its unchanged consumer predicate for negative-path tests; the current
canonical runner has a different hash. Original results and hashes are unchanged.
