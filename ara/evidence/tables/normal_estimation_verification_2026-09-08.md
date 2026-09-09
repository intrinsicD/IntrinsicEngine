# Canonical normal-estimation workflow verification

Local CPU implementation evidence for C79. The [record](../diagnostics/normal_estimation_2026-09-08/record.json) binds the tested source hashes and separates this run from earlier GPU/query evidence.

| Scope | Result |
| --- | --- |
| Point-set PCA | Eight canonical domains, each with KD-tree and supplied cached CPU LBVH, publish named planar normals within 1e-5 component/length bounds. Radius support, sparse fallback and repeated index reuse are checked. |
| Topology variants | Named-position mesh face weighting and graph neighborhoods pass; graph normals accept mesh edges. Deleted face/edge support and a position slot named `v:normal` are covered. |
| Publication | Unrelated properties and deleted output rows (including NaN) survive; create/update undo/redo works; changed output blocks history; queued source, deletion, topology and output edits discard work. Unrelated edits remain valid. |
| Config and UI | Validated config round-trip/configured run, canonical catalogs, shared menu aliases/draw, result delivery/dismissal and vector-recipe bindings pass. This does not assert a new GPU-rendered image comparison. |
| Verification | [179 focused checks](../diagnostics/normal_estimation_2026-09-08/focused-tests.txt) passed. [Full CPU gate](../diagnostics/normal_estimation_2026-09-08/cpu-gate.txt): 4,363 passed, one expected unsanitized LSan-control skip, zero failures. Sandbox builds with the ci app override. |
| Initial failures | [Visualization fixture capability](../diagnostics/normal_estimation_2026-09-08/initial-fixture-failure.txt) and [old separate-panel UI expectations](../diagnostics/normal_estimation_2026-09-08/initial-ui-expectations.txt) were corrected. No numerical oracle or correctness tolerance was relaxed. |

CPU integration only: no new GPU PCA, Vulkan visualization, sanitizer, performance or full Framework24 PCA/features/saliency claim. The working tree is dirty; no benchmark or default-adoption qualification follows. See [normal-estimation contract](../../../docs/architecture/normal-estimation.md).
