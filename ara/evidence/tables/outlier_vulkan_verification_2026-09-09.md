# Outlier analysis verification — 2026-09-09

[C81](../../logic/claims.md) is bounded to the fixtures and source hashes in
[record.json](../diagnostics/outlier_vulkan_2026-09-09/record.json). The checkout
includes preceding normal-neighborhood work and remains uncommitted.

| Evidence class | Result | Scope |
| --- | --- | --- |
| `ci`, Clang 23, unsanitized | `IntrinsicTests` and `ExtrinsicSandbox` built | Final combined source; ccache disabled for existing BUG-178 |
| Full CPU selector | 4,384 passed, 6 skipped, 0 failed; 104.45 s | 4,390 selected, excludes GPU/Vulkan/slow/quarantine |
| Host-display CPU follow-up | 5/5 passed | All five native-window capability skips rerun; 4,389 distinct CPU passes in total |
| Expected remaining skip | LSan control in unsanitized CPU build | No leak-freedom inference |
| `ci-vulkan`, ASan+UBSan | Outlier integration smoke passed, 44.70 s | RTX 3050, driver 590.48.01, actual operational device and readbacks |
| GPU/reference comparison | Exact mask equality; observed score error 0, tolerance 1e-5 | Eight canonical domains, cold/warm statistical and radius phases |
| Dense radius fixture | 1,027 neighbors counted with retained capacity 1 | Coincident peers, excluded self, one isolated outlier |
| Failure/history cases | Assertions passed | Stale/cancelled retention, deleted rows, custom properties, current-mask removal, guarded undo/redo |
| Runtime benchmark | Schema v2 and smoke gates passed | Warm framed GPU 9,906.665 ms; same-run CPU reference 94.265 ms; one warmup/measurement, small display-paced fixture, no speedup/default change |

The initial focused run exposed two obsolete UI ownership/reset assertions after
moving the panel. They were corrected and pass in the final full CPU run.
Sandboxed Vulkan test discovery encountered the already tracked BUG-180 ptrace
restriction; actual GPU execution used host access and the existing cohort
`detect_leaks=0` setting. Two GPU prerequisite rejection logs are the intentional
stale/cancelled tests. No VUID or ASan/UBSan finding appears in the completed log.

The CPU kernel retains the existing population-variance threshold and inclusive
radius formulation. Vulkan supplies neighborhoods/counts; CPU computes scores
and classifications. No LOF/LoOP implementation or rendered mask/score pixel
readback is claimed. GUI route and label/scalar recipe coverage is CPU evidence.

## Review

The scope is RUNTIME-209/UI-041: one analysis/removal workflow, preserving earlier
uncommitted normal work. No new layer or linked dependency is introduced. Plain
config/result records, a private transient CPU provenance stamp and existing
spatial-cache/job/history APIs provide the integration. GPU recording stays on
the device thread; workers consume captured data. Named publication guards
source/deletion/output revisions; removal additionally guards the full property
set. Undoing a different analysis cannot legitimize an edited prior mask.

Config, programmatic commands and UI share preview/apply/execute. CPU octree
remains default, explicit Vulkan has no CPU-query fallback, and the compatibility
combined-removal entry points remain CPU-only. Docs, module inventory, task links
and deferred consumer reminders were synchronized. Task notes remain active
pending publication.

| Clean-workshop row | Result |
| --- | --- |
| 1. Promoted layer imports | pass — strict checker, no violations |
| 2. CMake links | pass — existing runtime/geometry target ownership |
| 3. Exported layer types | pass — geometry exports spans/results; runtime exports config/commands |
| 4. Renderer growth ownership | n/a — no renderer member or subsystem added |
| 5. Typed pass IDs | n/a — existing framed query transport |
| 6. Recipe dependencies | n/a — existing recipe and job dependency paths |
| 7. Maturity follow-up | pass — bounded CPU and actual Vulkan evidence; publication pending |
| 8. Temporary exceptions | pass — no new layer exception; existing BUG-177/178/180 remain separate |

Raw artifacts: [Vulkan](../diagnostics/outlier_vulkan_2026-09-09/vulkan.txt),
[CPU cases](../diagnostics/outlier_vulkan_2026-09-09/outlier-cpu.txt),
[CPU summary](../diagnostics/outlier_vulkan_2026-09-09/cpu-summary.txt),
[native follow-up](../diagnostics/outlier_vulkan_2026-09-09/native-cpu.txt),
[benchmark](../diagnostics/outlier_vulkan_2026-09-09/benchmark/runtime-smoke.json).
