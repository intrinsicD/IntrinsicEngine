# METHOD-048 planning review: HKTex mesh authoring, compiled rendering and default adoption

**Scope and basis.** I reviewed the task text, `AGENTS.md`, `proposal.md` and `review-resolution.md` exactly as you pasted them. I ran no tools, so I could not check the stated SHA256 digest (`3307…26ab`). This verdict applies only to the text as supplied. I did not check any fact about the upstream repository or the paper beyond what these sources say. Where a finding depends on upstream behavior, the fix is written as an S00 determination rather than as a claimed fact.

## Blocking

**B1. Production spectral preparation comes after the slices that need it.**
- **Location:** S01 gate ("no production fallback to unbounded dense solves. Optimized sparse preparation is owned by S15"), S15 ("after S04/S07"), and S04, S11, S12 and S17, which operate on the engine assets and imported meshes named in S00 and in the Engine integration table.
- **Problem:** Between S01 and S15, the only preparation is a dense solver limited to tiny meshes. That leaves S04 fitting, S11 authoring, S12 pinned scenes and S17 with no legal way to prepare a real mesh. S15 also frames the sparse partial generalized eigensolver as an acceleration ablation. In fact it is a core capability: without it, the native method cannot run at engine scale. The task's own reuse search found no existing owner for it.
- **Fix:** Add a core slice **S01b — Production sparse generalized partial eigensolver**, after S01 and before S04. It should:
  - reuse `Geometry.Sparse` and `Geometry.DEC`;
  - include the dependency-versus-minimal-implementation decision;
  - gate on residual, mass-orthogonality and subspace-angle parity against the S01 dense oracle on small meshes, plus an upstream-fixture delta;
  - record the stable ID `rendering.hktex.prepare_sparse`.

  Reduce S15 to the *optimized* variants of this solver (streaming, caching, grid grouping), variable projection, and deployment-cost fitting, each with its own ID.

**B2. The parity tolerances cannot be met at ranking discontinuities, and the task forbids the usual relief.**
- **Location:** Impact protocol ("CPU L∞ ≤ 1e-6", "Vulkan ≤ 1e-4", "Near-tie disagreement … never an excuse for color error"); gates for S05, S06 and S12; row 8 of `review-resolution.md`.
- **Problem:** The outer kNN and inner top-contribution selections make the output discontinuous. The compiled distance `q_s(b)` rounds differently from direct 64-D embedding evaluation, and float32 on Vulkan rounds differently again. Near any rank-change boundary, a correct compiled evaluator can pick a different but equally valid ranking and show an O(1) color difference. As written, the gate can be "passed" only by sampling points that happen to avoid ties. That is exactly what the resolution rejects.
- **Fix:** Define an *interval-consistent oracle* in S00:
  - The float64 oracle reports, for each query, the set of rankings consistent with rigorous distance and contribution intervals. It also reports the color for each ranking.
  - An implementation passes if its color is within tolerance of the color for the ranking it actually selected, **and** that ranking belongs to the admissible set.
  - Exact ties still use stable-ID order.
  - The compiled CPU path must certify its ranking gap. Where the gap is smaller than its rounding bound, it recomputes the ranking in float64 or with exact expansion arithmetic.
  - S12 must implement the same fallback (native float64 if `shaderFloat64` is present, emulated double-float otherwise). Failing that, it must report uncertified-pixel counts as an explicit failure category with its own cap, fixed at S00 and not waivable.

  Apply the same rule to upstream comparisons, because upstream runs in float32.

**B3. Native fitting has no differentiation mechanism.**
- **Location:** S03 (the evaluator oracle, which has no derivatives) and S04 ("Adam-like shape updates, geodesic position/momentum updates … loss/gradient checks").
- **Problem:** The upstream trainers presumably rely on PyTorch autograd; S00 should confirm this. The native plan never says how gradients are obtained for:
  - kernel color, sharpness, power, σ and diffusion time;
  - anisotropy, which enters through the aligned-grid basis interpolation;
  - surface position, through the barycentric spectral evaluation;
  - gradients through the fixed selection set.

  Nor does it say which convention holds at selection changes. "Loss/gradient checks" presuppose that a gradient implementation exists.
- **Fix:** Add a differentiable reference to S03, or split it out as S03b before S04:
  - Use hand-derived per-stage adjoints in float64. Justify any AD dependency under §5 and §4.
  - Selection is held fixed within a step, with a documented straight-through convention matching upstream autograd at the selected set.
  - Test each stage with central finite differences away from selection boundaries, and test agreement with upstream gradients from exported S00 fixtures.
  - Name the gradient contract for the power function at nonpositive inputs and for the denominator floor `max(Σα,1)`, where the gradient is zero on the floor branch.

## High

**H1. Slices are not classified as core or candidate, and core work depends on rejectable candidates.**
- **Location:** Scope ("S00–S18 are required scope. An experimental candidate may finish with a reproducible rejection"), the S12 dependency on S05–S11, and the Acceptance criteria.
- **Problem:** No slice is labeled core or candidate. S12 (Vulkan) depends on S06–S09, and each of those may legitimately be rejected; S09's gate even allows "explicit opt-in status". So closure is ambiguous, and one rejected filter experiment blocks Vulkan.
- **Fix:**
  - Add a slice classification table.
    - **Core, must pass:** S00, S01, S01b, S02, S03, S03b, S04, S05, S08, S10, S11, S12a, S17.
    - **Candidate, rejection allowed:** S06, S07, S09, the S15 variants, S16 generators, and the S13 normal adaptation.
    - **Adoption:** S18.
  - Split S12:
    - **S12a:** Vulkan point evaluation plus material integration, after S05, S10 and S11.
    - **S12b:** Vulkan filtering, after S09 and S12a, with the ID `rendering.hktex.vulkan_filter`.
  - State in S18 which rejected candidates block default promotion. For example, a rejected or opt-in-only S09 blocks it, because S18 requires stable minification.

**H2. The default and maturity gates are not honestly preregistered.**
- **Location:** Frontmatter `maturity_target: ParityProven`; Impact protocol ("S00 must turn every remaining budget into finite values"); S18 ("Freeze held-out corpus … numeric … thresholds before adoption runs"); Scope's final sentence ("umbrella remains open").
- **Problems:**
  1. Thresholds are frozen twice, at S00 and again at S18. The S18 freeze can happen after S05–S17 results are visible, which permits tuning the gate to pass.
  2. `ParityProven` does not describe a goal that includes operational Vulkan rendering and default adoption.
  3. No product frame or memory budget exists; the proposal says so. Only the operator can supply one.
  4. There is no terminal disposition if adoption fails or the operator declines it.
  5. The default's behavior on Null, headless and incapable devices is unspecified. This breaks §1's requested/actual/fallback reporting requirement.
- **Fix:**
  - Freeze the held-out corpus, devices, scenes and all adoption thresholds once, in S00, as an operator-approved decision record. Later changes are amendments that carry a rationale, are written without access to held-out results, and are retained.
  - Set the maturity target to the level in `docs/agent/task-maturity.md` that covers operational default adoption. If none exists, target ParityProven and split S18 into a follow-up task.
  - Add an S18 terminal outcome, "default rejected (operator-recorded)", which retires the task at its actually reached maturity.
  - Specify how per-backend capability is reported, and what saved HKTex-default scenes show on incapable backends (requested HKTex, actual X, fallback reason).
  - Name the current default producer and the exact config key that S18 changes.

**H3. Parity covers upstream code only, not the published results.**
- **Location:** "Sources and fixed reference"; S04 and S17 gates.
- **Problem:** Parity is defined only against exported code fixtures. No slice reproduces a published paper experiment with the pinned configs. That leaves "full HKTex" coverage unproven against the paper itself.
- **Fix:**
  - Add a **publication-reproduction row** to S04 (texture/property fitting) and S17 (multi-view). Run the pinned upstream configs and datasets, if their licenses allow, through both upstream and native (or the S17 adapter). Report against the paper's stated metrics with a preregistered tolerance.
  - Where data is unlicensed or unavailable, record that absence as a named limitation.
  - In S00, import the pinned upstream configs verbatim as hashed native presets, so the baseline configuration *is* the pinned reference, not a transcription of it.
  - Add a paper/supplement coverage matrix to `paper.md`. It should map each algorithm, equation, experiment and ablation to a slice or to an explicit out-of-scope disposition, replacing the prose list in Scope.

**H4. Shared distance coefficients and the affine heat form are assumed, not established.**
- **Location:** S05 (`H_s(b)=Σ b_i A_si`, "sharing edge coefficients"); Spatial section ("the pinned biharmonic embedding"); proposal §1.
- **Problem:** The identity `‖Σ b_i z_i − z_s‖² = Σ b_i D_si − Σ_{i<j} b_i b_j E_ij` is correct when `Σ b = 1`. But:
  - `E_ij` is kernel-independent only if the distance embedding is shared by all kernels. If the distance embedding uses the kernel's anisotropy-interpolated basis, `E` is kernel-specific and one kNN index is invalid.
  - `H_s` is affine in `b` only if the heat normalizer is constant per source. A normalizer that depends on the query (for example, a diagonal heat term at `x`) turns the heat term into a ratio.
- **Fix:** Make S00 decide both questions from the traced code and fixtures:
  - which basis defines distance, and which defines heat;
  - how normalization depends on the query.

  Then add a conditional to S05: if the distance embedding is per-kernel, store `E` per kernel and revise the S06 index and storage accounting; if the normalizer depends on the query, compile numerator and normalizer as separate interpolants and evaluate the ratio.

**H5. Upstream kNN semantics and fixture-generation hardware are unpinned.**
- **Location:** Sources ("Preserve … index settings"); Ownership ("deterministic exhaustive embedding-distance scan"); S00 fixture export; S04.
- **Problems:**
  - If the pinned FAISS index type is approximate, exact exhaustive native search is not parity with upstream.
  - The kNN refresh schedule during upstream training (every iteration or periodic) is part of the fit semantics.
  - If `knn_heat/` or the trainers need CUDA, S00 fixture generation cannot run under the stated tooling assumptions.
- **Fix:** S00 records:
  - the FAISS index type, and whether it is exact;
  - the kNN refresh cadence in each trainer;
  - the hardware and backend needed to run the pinned code.

  Fixtures are generated with an exact index. Any approximate-index difference from upstream is reported as a separate comparator. S04 either reproduces the refresh cadence or names the deviation. The fixture environment gets a lockfile and regeneration command, and committed fixtures get a size cap. CI consumes only committed fixtures.

**H6. S17's native scope contradicts itself, and the renderer mismatch is unacknowledged.**
- **Location:** S17 bullets 2–4.
- **Problems:**
  - "CPU reference tests validate … objective and gradient checks" needs a native forward image model, yet the same bullet excludes any differentiable renderer.
  - "Held-out image errors" does not say whether the error is measured in Mitsuba's forward model or in engine renders, and those use different BRDF, lighting and tonemapping.
  - Upstream mesh preprocessing (normalization, deduplication, reordering) can break vertex and face correspondence.
  - Launching Python/Mitsuba from a runtime job adds unowned process-spawn machinery.
- **Fix:**
  - Limit native S17 to validating the observation schema, import, correspondence and compilation. Delete the native objective/gradient claim, or scope a named minimal forward model (for example, known-visibility diffuse projection) with its own identity.
  - Measure held-out error in the upstream forward model. Report the engine-render comparison as a non-parity diagnostic with the model differences listed.
  - Require exact position/index hash correspondence after upstream preprocessing. Reject the import otherwise.
  - Run acquisition as an out-of-process tool under `tools/`; the runtime job owns only import, validation and compilation. If launching from the editor is required, add a process-launch port with an ADR and a layering decision.
  - If no licensed calibrated dataset is identified in S00, the acceptance disposition is "synthetic-only" and is not a completed acquisition.

## Medium

- **M1. Benchmark runner arrives too late.** S04 owns `HktexBench`, but S01–S03 already emit `rendering.hktex.prepare`, `surface_motion` and `reference`. Move the runner skeleton, manifests and `INTRINSIC_HKTEX_BENCH_CONFIG` into S00. The example seals results to `/tmp`; state that claim-eligible results are sealed into retained `ara/evidence/` locations.

- **M2. The S07 certificate depends on the exact composition formula.** The reference only says "blend division … then base color and RGB clamp". If base color is blended in (for example, `(1−Σα)·base`), the per-source bound must be `|α_s|·‖c_s − base‖`, not `|α_s|·‖c_s‖`. S00 should write the exact per-channel composition formula, including clamp placement, and S07 should derive `B_s` from it.

- **M3. S09 bound is misstated.** Replace "`integral(P-C) ≤ epsilon`" with `|∫ w(P−C)| ≤ ‖P−C‖∞ ≤ ε`, for `w ≥ 0` and `∫w = 1`. For fully covered nodes, zeroth moments are exact only for a constant filter weight. Add the oscillation term `osc_node(w)·∫_node|C|`, or use higher moments, and bound it within the footprint budget.

- **M4. Compiled distance has precision hazards.** `Σ b_i D_si − Σ b_i b_j E_ij` cancels catastrophically near a source and can go slightly negative. S05 should specify the clamp `max(q,0)` as a documented semantic, and give a float32 error analysis for S12 that feeds B2's certification bounds.

- **M5. Accepted input domain differs across slices.** S00 fixtures include boundaries and disconnected parts, and S11 accepts every mesh meeting the contract, yet S16 "implements declared component/boundary policies". S01 should implement the upstream-equivalent component, nullspace and boundary behavior. S16 then only extends the accepted domain, and S11 rejects anything outside the current domain before starting a job.

- **M6. Fit-time kNN acceleration has no owner.** The Spatial section mentions "compiled-cell workspaces for optimization", but no slice builds or ablates them. Native CPU fitting with exhaustive 64-D kNN may miss any S18 fit-latency budget. Assign it to S15 with ID `rendering.hktex.fit_candidates`, gated on exactness against exhaustive search. State that native fitting is CPU-only and record the hardware used for upstream comparisons.

- **M7. Frontmatter gaps.**
  - `depends_on: [METHOD-047]` is never explained. Say what it supplies and which slice needs it, or drop it.
  - The contracts list omits scopes the task clearly touches: benchmark manifests, research claims, the asset/scene codec, backend truthfulness and spatial consumers. Per §11, add the catalog IDs or justify leaving each out.

- **M8. Rendering path underspecified.**
  - The Goal says "native compiled CPU/Vulkan rendering". Unless an existing CPU render backend is named, say "compiled CPU evaluation and Vulkan rendering".
  - S12a must detect fragment-barycentric support. The fallback is either interpolated vertex attributes (with the duplication cost counted) or a named alternative, and the backend must report the actual addressing path used.

- **M9. Bundled stable IDs hide attribution.** S14 covers edits, deformation, remeshing transfer and export under one ID. Split into `edit`, `deform`, `transfer` and `export` IDs. For S12, split per H1.

- **M10. Compiled-record schema needs one owner.** Name the single owner of the compiled record schema (the core-only assets payload), and have S05's method output convert into it losslessly with a test. The S12 GPU parity comparison must evaluate the *deserialized S10 payload* on the CPU, not the method's internal structure.

- **M11. Wording that breaks configured-reference parity.**
  - "64-D" should be "the configured embedding dimension".
  - "Adam-like" should be "the pinned upstream optimizer and hyperparameters".
  - The S02 "round-trip" transport test must reverse the same path. Closed loops pick up holonomy from curvature, so identity is expected only on flat regions.

- **M12. The "Planning review" section describes future state as present.** It says the review "is retained" before it exists. Replace that with the evidence path and verdict once recorded. Resolving these findings changes the digest, so re-review the revised text.

## Verified adequate

Several parts are sound as written:
- The ownership table respects the §2 and §4 layering rules.
- The candidate exclusion rule `l_s > U_k` is sound, and a superset of the outer set suffices for the inner stage.
- The distance expansion identity is correct.
- Stage-wise fixture injection matches pinned-reference reuse.
- The Gaussian-before-power ordering, the denominator floor, and keeping three separate comparators follow the traced code.
- The `run_and_seal.py` positional-output usage matches your verified detail.
- The CTest invocations are correct: repeated `-L` options AND together, and `--no-tests=error` is present.
- Planned and executed states are kept distinct throughout.

**REVISE**