# METHOD-048 planning review dispositions

Claude Opus 5.5, high effort, reviewed supplied text read-only. This is task review,
not implementation verification. Round 1 requested revision. No experiments ran.

| Finding | Disposition |
|---|---|
| B1 sparse preparation too late | Added required S01b before native fitting; S15 now optimizes an existing production solver. |
| B2 rank discontinuity/parity | Added arithmetic interval/gap certification, bounded precision refinement and explicit unresolved failure. Kept strict deterministic color parity; interval-admissible outputs are diagnostics, not permission to conceal large color changes. Higher precision alone is not an ordering proof. |
| B3 missing differentiation | Added S03b stage adjoints, upstream gradients, fixed-selection derivative conventions, power/floor/clamp behavior. Corrected suggested floor wording: numerator derivatives do not vanish. |
| H1 core versus candidate | Added classification table; separated S12a point rendering and S12b filtering. Candidate rejection does not block point rendering, but missing viable filtering blocks default adoption. |
| H2 default/maturity | One S00 preregistration with concrete relative product targets; S18 consumes it. Named proposed serialized selector and incapable-backend behavior. Retained ParityProven: repository taxonomy explicitly makes it cumulative over Operational and lists default promotion. Operator rejection is an explicit terminal option; no fabricated approval requirement. |
| H3 paper reproduction | Added paper equation/experiment coverage matrix, hashed configuration mapping and publication reproduction rows for texture and multiview fitting. Missing data remains a limitation. |
| H4 compiler assumptions | Traced shared isotropic embedding in pinned code; require S00 normalization fixtures and conditional coefficient/query forms for variants. |
| H5 reference environment | Exact/approximate index identity, trainer refresh cadence, hardware, optional upstream CUDA, locked environment and bounded fixture generation are explicit. |
| H6 multiview | External tools command owns inverse renderer/gradients; native jobs import/validate/compile. Source correspondence mandatory; held-out forward-model error separated from native-render diagnostic. Licensed real example remains required. |
| M1 runner timing | HktexBench created in S00; smoke /tmp versus retained claim evidence distinguished. |
| M2 compositing | Recorded actual additive-base composition from pinned heat_kernel_texture_knn.py; bounds must derive from it. |
| M3 filtering bound | Corrected absolute normalized-weight integral inequality and variable-filter moment error. |
| M4 distance precision | Added cancellation bound and conditional zero clamp; negative outside arithmetic enclosure fails. |
| M5 input domain | S01 implements base domain policies; S16 only extends; readiness rejects unsupported input. |
| M6 fit search | Added S15 fit_candidates ablation and rebuild/refresh cost; native fitting CPU-only explicit. |
| M7 metadata | Explained METHOD-047 reuse dependency. Catalog has no specific IDs for several requested scopes; canonical contracts still bind. Spatial locality ID applies only if cache interface changes, which is not planned. |
| M8 rendering | CPU evaluation wording, barycentric capabilities and counted addressing fallback. |
| M9 benchmark attribution | Split edit/deform/transfer/export IDs and separate Vulkan filtering ID. |
| M10 schema | CPU assets owns compiled schema; lossless method conversion tested; GPU comparator uses deserialized payload. |
| M11 configured fidelity | Configured embedding dimension, pinned optimizer, reversed-path transport versus holonomy. |
| M12 review provenance | Review directory and digest/verdict record explicitly identify actual planning evidence. |

The review is limited to supplied task/design/contract excerpts; it does not certify
uninspected upstream code or engine behavior. Codex verified the source composition,
isotropic distance use, benchmark runner convention and maturity/catalog corrections.

## Round 2

Claude confirmed all original dispositions, then identified one remaining high
issue: exact ties and refinement provenance. The task now specifies exact
polynomial distance predicates, proved contribution-expression equality, bounded
arbitrary precision with explicit unresolved outcomes, and optional original
reference operands for refinement. Rounded compiled coefficients cannot restore
lost order. Optional spectral operands are counted, and compact payloads can
report refinement unavailable. Exact-tie and unresolved-budget fixtures have
different expected outcomes. Also added the S17 dataset precondition, corrected
S12a/S12b verification naming and removed the duplicated word.

## Round 3

Verdict: APPROVE for task-review-3.txt. No blocking/high findings remain.
The reviewer’s final non-blocking clarification was added verbatim in substance
to S06/S07: canonical-reference bounds include coefficient-generation error and
evaluation error. These two local clarifications are the only changes after the
approved snapshot; record.json binds both reviewed and final hashes. No code,
performance or experiment approval is implied.
