# HKTex-specific correction (discussion only)

The user clarified the intended method with https://circle-group.github.io/research/HeatKernelTextures/ . Earlier rounds addressed generic heat-derived fields, not this learned representation. Their blanket representation-level rejection does not apply to HKTex.

Sources read: https://arxiv.org/html/2609.07557v1 (sections 4–6 and supplement S.1); https://github.com/circle-group/hktex (README and FAISS adapter/config). Actual Claude Opus 5.5 was consulted again at medium effort with the paper and README supplied. No implementation or reproduction occurred.

Codex assessment: HKTex is a credible candidate for UV-free albedo and deserves a bounded offline comparison before any runtime adoption. Nonlinear shaping and movable adaptive kernels address subtriangle detail. Existing vertex properties remain an essential direct-rendering baseline. Published storage is distinct from reconstructed spectral caches and runtime working memory. The published Mitsuba timings establish no Vulkan performance result. Albedo evidence does not establish normal-map, categorical-label or general material support.

Qualifications to Claude's response: remeshing need not universally require full refitting; correspondence and transfer may help but were not evaluated here. Cache portability needs testing; a discrepancy would not by itself prove that all storage benefits collapse. Zero value for vertex-derived properties is too strong: compression could matter, though it remains untested. The sigmoid shaping operation is distinct from footprint-aware antialiasing. Do not adopt the suggested sharpness adjustment without a derivation. No blanket rejection or adoption was authorized.

Tentative next experiment: fit one representative static manifold albedo asset using the reference, compare reconstruction quality, complete load/precompute/fit costs, cache and peak resident memory, and magnified/minified views against the engine atlas and direct attributes. Only then assess a Vulkan evaluator.
