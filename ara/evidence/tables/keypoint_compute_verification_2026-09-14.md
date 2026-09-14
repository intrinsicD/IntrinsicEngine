# Vulkan keypoint computation verification

[Source hashes, command receipts, review text and diagnostics](../diagnostics/keypoint_compute_2026-09-14/record.json)
bind this local result. Clang 23 Debug; actual GPU: NVIDIA RTX 3050, driver
590.48.01. [C93](../../logic/claims.md#c93-full-vulkan-keypoint-computation-executes-the-bounded-reference-fixtures)
is bounded Operational evidence, not a general-input or performance claim.

| Evidence | Result |
| --- | --- |
| Full CPU | 4,623 selected; 4,617 pass and six sandbox skips. Five native-window follow-ups pass, giving 4,622 distinct passes and one expected unsanitized leak-control skip. |
| ASan / UBSan | Each isolated preset passes 1,450 grouped geometry cases and 37 affected runtime contracts, serially. Full engine sanitizer suites were not rerun. |
| Actual Vulkan | Seven registered cases pass, no skips or CPU fallback: two shared LBVH oracles, two hybrid keypoint cases, three full-compute cases. Validation and combined ASan/UBSan enabled; existing BUG-180 leak exclusion retained. |
| Developer application | Existing `dev` Clang 23 Vulkan/ASan/UBSan sandbox and shaders rebuild successfully. |
| UI / dependencies | Shared processing UI test selects both Vulkan computation and CPU with Vulkan neighborhoods. Spatial compilation-locality contract passes after removing heavy callback-interface imports. |
| Diagnostic smoke | Eight-domain warm request-through-publication: Vulkan 4842.038613 ms, CPU reference 153.401899 ms; saliency L-infinity error 0.0, tolerance 1e-5, masks exact. One warmup and one measured set. |
| Dragon diagnostic | 151,486 OBJ positions, automatic radii, capacity 1,024, observed maximum support 245. CPU 88326.229627 ms; Vulkan 5118.388229 ms. Both produce 644 keypoints, exact masks and saliency values. Single cold request per backend, sanitized Debug, point-domain runtime probe. |

The [schema-v2 smoke](../diagnostics/keypoint_compute_2026-09-14/benchmark/runtime-smoke.json)
binds the manifest hash and is explicitly dirty-source, `claim_eligible: false`.
The small fixture is slower on Vulkan; frame/readback latency belongs to the
measured scope. Neither it nor the single dragon observation proves a repeatable
speedup. No default changed. The temporary dragon probe was removed; its result
precedes the final callback-interface-only repair, followed by the seven final
registered GPU checks on the combined source.

Claude reviewed one fixed source packet and two bounded follow-ups. Repairs
include complete readback-size validation, full PCA bases for repeated/ambiguous
roots, correct covariance-radius culling, truthful dispatch counts and distinct
traversal-overflow diagnostics. Planar/collinear comparisons initially failed:
the CPU reference's analytic eigensolver and both keypoint numerical-zero rules
were corrected; the 1e-5 comparison tolerance was not widened. CPU tests also
cover rank-one spectra and small separated spectra with eigenvector residuals.
Shared barriers, FP64 gating, CPU/GPU comparison precision and intentional
zero-score/isotropic behavior were inspected and retained.

The runtime preserves all eight canonical property domains, deleted rows,
unrelated data, stale/cancelled job guards and atomic undo/redo publication.
The GPU requires shader float64, at most 2^20 live points and complete radius
support within the selected capacity (maximum 1,024); unsupported or overflowing
requests reject without partial publication. All stages currently share one
frame, so batch size is not a frame-time or cancellation-latency guarantee.
See the [method contract](../../../docs/architecture/keypoint-analysis.md).

The original desktop display symptom remains tracked by BUG-194; BUG-193 owns
pacing and BUG-195 owns verification disk headroom. Incomplete disk-exhaustion
records and interrupted builds were not accepted as passing evidence. The final
CPU, sanitizer, GPU and structural runs supersede them.
