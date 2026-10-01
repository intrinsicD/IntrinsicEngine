# K-means

`Geometry.KMeans` is the canonical CPU Lloyd reference. Runtime clustering keeps
its initialization, first-index distance ties, empty-cluster farthest-point
reseed, and post-update termination: unchanged labels or squared maximum
centroid shift at most the squared tolerance. Initial labels are zero; the
reported iteration includes the update that satisfied convergence.

The Vulkan implementation id is `vulkan_resident_paged_lloyd`; the CPU id is
`geometry_kmeans_cpu_reference`. Requested/actual backend and fallback reasons
are reported independently. GPU admission requires the runtime
`SpatialIndexCache` and its property residency. No production CPU parity run
is performed. GPU labels use UInt32 storage; other requested scalar storage
uses the CPU reference with an explicit fallback reason. GMM and other clustering methods are unchanged.

Canonical vec3 positions remain tightly packed at stride 12. A device pass
gathers live rows into working storage. CPU initialization uploads only the
seed centroids and, when deleted rows require it, the live-slot map. These
bytes are declared as CPU-stage uploads. Residency input uploads/hits, small
host convergence diagnostics, terminal centroid readback, and Accept's typed
label readback are reported separately. A retained input revision is reused
without a second upload. Canonical prior labels, when needed to preserve deleted slots, are also counted
as resident inputs. All-live runs do not upload prior output labels.

Assignment and update pages visit at most 2^18 point/cluster pairs; a
submission visits at most 2^24 pairs. One submission records consecutive
gather, assignment, reduction and update pages, and a due preview's scatter and
presentation pages before the next iteration, until a budget is exhausted or an
iteration's update completes. Each submission reads back the small convergence
diagnostics; only the host decides whether another iteration runs. The terminal
preview submission reads back the final centroids instead. Page shapes depend
only on point count, cluster count and the page budget, never on where a
submission boundary falls. It also has a conservative serial-depth
budget of 2^14: sum the longest lane path across every dispatch, even when
workgroups overlap. Assignment costs `innerCount + 1`; Update costs
`ceil(innerCount / 64) + 7`; Reduce costs `ceil(rowCount / 64) + 7`;
gather/scatter/presentation cost 1. The seven reduction steps are six tree
levels and one ordered page combine. Update's inner pages and Reduce's row
pages contain at most 4096 entries, so their longest lane scan is 64 entries
and their accounted dispatch depth is at most 71. The remaining depth and pair
budgets gate every dispatch. All workgroups use 64 lanes; Update dispatches one
workgroup per centroid, Reduce one per page. Double sums use a fixed tree and
combine pages in dispatch order, without floating-point atomics. The device
must support shaderFloat64.

The existing cluster-count contract is [1, 1024] in config, validation and UI;
CPU seeding remains synchronous. There is no point-count cap beyond 32-bit row
representability and successful storage allocation. Every submission completes
before the next one. Internal test knobs force smaller pair budgets.

The service retains the last run's workspace buffers and three pipelines while
idle and reuses any buffer whose capacity suffices. A workspace returns to the
pool only when its last submission completed; a run finished with a submission
still in flight, or a failed recording, does not return it. Results report
the buffers and pipelines created at admission.

The integer label ring and float presentation ring are reserved together at
admission, and publish only after all
preview writes complete. `sandbox.clustering.gpu_preview_interval` defaults to
5, is validated in [1, 1000000], and round-trips through configuration. Final
and stopped results publish even between intervals. The scalar colormap observes
the float front under the label property's name; Show selects that property.

| method.engine-integration field | Publication |
| --- | --- |
| Same-domain outputs | GPU preview: yes (label colormap); commit via scalar transaction |

`Runtime.PointScalarTransaction` owns the typed front readback and guarded
Accept lifecycle. The existing clustering publication writes labels, colors,
and optional scalar labels in one undoable entry. Accept refuses stale inputs
or outputs. Stop finishes the current iteration; Discard and detach leave CPU
outputs unchanged. Batch requests and `run_kmeans` agent commands auto-accept; refused stale Accept
finishes with StaleSource. A terminal preview fails with a reason after 600
busy-ring retries. Failed recordings retain resources until device-idle shutdown.
The panel exposes Stop, Accept, Discard, backend identity and IO counters.

CPU centroid sums also accumulate in double before conversion to float.
Exact CPU/GPU bit parity is not guaranteed: summation grouping, division
rounding and FMA contraction can differ; near assignment ties or termination
thresholds can amplify rounding differences. Run-to-run GPU determinism uses
the same fixed page shape and ordered reduction.

Vulkan parity tests: exact labels for the fixtures (equal distances choose the
lowest centroid index), centroid Linf tolerance 1e-5, identical iteration and
termination fields. The tests record label mismatch count and centroid Linf and
include split centroid pages with duplicate-seed ties, a 2049-row k=8 fixture,
and preserved deleted-slot labels. Both typed and float fronts are read after
terminal preview publication. Each case discards the first run,
and checks zero input upload and zero workspace buffer/pipeline creation on the
repeat run with forced multi-page budgets. When budgets fit a whole iteration
(the 2049-row case), the repeat run uses exactly one submission per iteration
plus the terminal preview; tighter budgets still split iterations.

Measured delta (RTX 3050 under Xephyr, 2026-10-01): all three
`ClusteringServiceGpuSmoke` cases, including 2,049 points with k=8 and a deleted slot,
report 0 label mismatches and centroid Linf 0 against the CPU reference. A timing
measurement at 1M points is still outstanding; the per-submission bound rests on the
budget constants above.
