# METHOD-047 verification report

The recorded implementation combines scalar-guided connected regions, constrained
charting, selectable native distortion objectives, validated UV publication,
property bakes and a selected-mesh/atlas split workspace. The supported input is
finite indexed triangles; invalid or under-resolved inputs fail explicitly.
See the [method contract](../../../../docs/methods/property_guided_atlas.md).

## Numerical correctness

`final-corpus-summary.json` records 35 accepted runs: three procedural controls
and four frozen METHOD-045 surfaces, each with native None/Angle/Area/Both and
xatlas Angle. All use 1024², padding 2, maximum conformal and area ratios 10,
a 16,384-chart limit and a 40-iteration budget. `backend-audit.json` records
that the runner-reported method/objective matched each request without fallback;
it is a reporting audit, not an independent execution trace. The numerical
campaign supplies fixed region labels. Typed property binding and runtime GMM
integration are covered by runtime tests, not by these geometry-level runs.

Across the seven native cases, the independent maximum ratios are:

| Objective | Maximum conformal ratio | Maximum area distortion |
| --- | ---: | ---: |
| None | 9.987581 | 9.650857 |
| Angle | 1.741859 | 4.559267 |
| Area | 7.622930 | 1.068254 |
| Both | 1.637081 | 1.353008 |

These maxima describe different methods on this finite cohort. They are not a
ranking of semantic segmentation, universal reliability or performance. In
particular, None does not minimize distortion, and prioritizing area can sacrifice
angles. None can satisfy the limits by extensive refinement: on the frozen
surfaces it used 431–2,434 charts, compared with 18–40 for native optimized
objectives. This chart-count cost is part of the tradeoff. The acceptance
thresholds were not tightened or relaxed after results.

The additional `child.obj` case has 100,000 triangles. Native Both at 4096²
produced 260 charts, maximum conformal ratio 1.578208 and maximum area distortion
1.373161. One Debug timing sample is retained for diagnostics only; it supports
no speed claim.

The Python audit independently recomputes source coverage, finite positive UV
areas, bounds, region crossings, one global density and Jacobian metrics.
The audit accepts recomputed distortion maxima up to the configured limit
multiplied by (1 + 1e-6). It does not assert equality of native and Python maxima
within that tolerance. It does not perform a second all-pairs overlap test: the
separate native validator supplies
exact binary32 positive-area overlap and strict interior texel-center checks.
Adversarial CPU tests exercise that validator independently of chart construction.

## Verification and review

| Run | Passed entries | Skipped entries | Failed |
| --- | ---: | ---: | ---: |
| Focused CPU | 198 | 0 | 0 |
| Full CPU | 5011 | 1 | 0 |
| ASan (grouped) | 3333 | 0 | 0 |
| UBSan (grouped) | 3332 | 1 | 0 |
| Vulkan | 97 | 0 | 0 |

`verification.json` lists exact selected/pass/skip counts for focused CPU, full
CPU, separate ASan/UBSan and Vulkan runs. `verification-runs.tar.gz` retains
build/configuration logs, CTest summaries and raw test output. Sanitizer pure
tests are grouped, so CTest entry counts cannot be compared directly to the
ungrouped CPU count. `environment.json` records the toolchain, presets and GPU.

The GPU evidence includes raw signed/zero values, independent coverage,
zero-alpha vector values, chart gutters, actual thin-chart coverage, UV-view
pixel readbacks, generated-property lifecycle and the real atlas split workspace.
Raw float bake comparison uses a 1e-4 tolerance; encoded 8-bit comparison permits
one quantization step. This is a bounded operational/readback result on the
recorded NVIDIA device, not a hardware-independent rasterization guarantee.

`atlas-workspace.png` is a capture of the task-owned Sandbox window from the
new split-workspace integration fixture. The additional GDB capture disables
LeakSanitizer only because LSan cannot run under ptrace; it is separate from the
normal unmodified sanitizer and Vulkan gates.
The capture shows the Atlas tab and a selected reference triangle. Per-bake tab
state and texture contents are covered separately by runtime tests and GPU
readbacks; the screenshot does not show those tabs.

Actual Claude Opus 5.5 reviewed five fixed source revisions and independently
audited the numerical report. The numerical audit corrections are recorded in
`review-resolution.md`. The initial review used
max effort; bounded remediation/reviews used high effort. `claude-execution.json`
records verified model identity without account/session data. The five review
reports, two remediation reports and `review-resolution.md` retain the findings
and their disposition. `claude-review-source.tar.gz` contains exact prompts and
reconstructable fixed-revision patches. The final nested undo correction follows
review 4's proposed fix and is covered by the final focused/full/sanitizer tests.

## Reproducibility and limits

`final-numerical-runs.tar.gz` contains the corpus inputs, runner outputs, sealed
result JSONs, independent audits, commands, source patch and source/binary hashes.
The base revision is in `source-provenance.json`. `post-corpus-source-binding.json`
and `post-corpus-ui-test.patch.gz` prove that the only later build-input change adds
GPU split-workspace coverage; production code, shaders, runner and dependencies
are unchanged. The final Vulkan run includes that test. Archives are SHA-256
indexed by `archive-hashes.json`; `intermediate-numerical-runs.tar.gz` preserves
the prior campaign and is not the final verdict. The files prefixed
`intermediate-` also describe that prior campaign.

All numerical records remain `claim_eligible:false`: this was a local dirty
Debug correctness campaign, not a repeatable performance benchmark. The atlas
solver is CPU-only. Runtime publication requires triangular faces; automatic
import/reconstruction can retain usable geometry without UVs when enrichment
fails. Quality and resource limits can reject a mesh; no mesh-repair guarantee,
semantic object-part guarantee, UDIM, unrestricted mip chain or speed improvement
is claimed. Generated bake records and GPU texels remain session state; UVs
and accepted atlas dimensions persist in scenes. Automatic bakes preserve the
accepted atlas extent and reject sizes above 8192 per axis rather than silently
changing the sampling grid.

The incremental Vulkan build initially read an outdated TextureBakeService
interface. Regenerating changed input artifacts with ccache disabled compiled
the unchanged source; the final GPU gate ran after that recovery. BUG-209 records
this environment incident without attributing an unproved compiler/cache cause.
