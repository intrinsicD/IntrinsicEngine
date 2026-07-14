# GRAPHICS-051 — Differentiable rendering mode (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Slang autodiff (`GRAPHICS-041`) ships an implementation slice and a `methods/` consumer exists.

## Goal
Lock down the contract for a differentiable-rendering execution mode in which the frame graph runs forward to produce a rendered image and a derived loss, then runs backward (via Slang autodiff per `GRAPHICS-041`) to compute gradients with respect to scene parameters (transforms, materials, light, neural weights, Gaussian parameters), enabling inverse rendering and method-driven research integration without touching the production rendering path. Planning only — no autodiff bodies and no inverse-rendering examples here.

## Non-goals
- No specific inverse-rendering applications (each lives under `methods/`).
- No CPU autodiff. Mitsuba 3 / Dr.Jit are reference points, not dependencies.
- No general optimizer framework; gradients are exposed, optimization choice belongs to method consumers.
- No removal of the standard production frame graph; differentiable mode is a separate recipe.
- No live ECS access for gradient writeback. Gradients flow back through the snapshot extraction boundary as a research-only side channel, recorded explicitly.

## Context
- Owner layer: `graphics/framegraph` (forward/backward graph compile), `graphics/renderer` (per-pass differentiable kernels), `methods/` (research consumers — out of scope here), `runtime/` (gradient-aware extraction sink for research workflows only).
- IntrinsicEngine's snapshot-extraction boundary makes differentiable rendering tractable: the immutable render world is the natural site of "scene parameters" with respect to which gradients are taken. Vendor analogues: Mitsuba 3 / Dr.Jit (offline), 3D Gaussian Splatting fitting (online).
- Slang autodiff (gated by `GRAPHICS-041` annotation policy) is the canonical mechanism: passes annotated `differentiable` produce both forward and backward kernels at compile time.
- Cross-links: `GRAPHICS-041` (autodiff annotation policy), `GRAPHICS-048` (3DGS fitting is a natural consumer), `GRAPHICS-049` (NRC training is a special case), `methods/` workflow per `docs/agent/method-workflow.md`.

## Recorded decisions
1. **Recipe selection.** `DifferentiableRender` is a separate future recipe selected through an explicit method/runtime API; production recipes are unchanged, and differentiable mode is opt-in per method and does not depend on the retired `RenderConfig::FrameRecipe` selector. Rationale: isolating differentiable mode in its own recipe selected by an explicit API guarantees zero impact on production recipes (a non-goal), and avoiding the retired selector keeps the path aligned with the current frame-recipe contract.
2. **Forward + backward graph.** Compile produces a paired forward + backward render graph; the backward graph executes in reverse pass order consuming per-pass adjoints, with the compile output shape recorded. Rationale: reverse-mode autodiff requires running passes backward over their recorded forward state, so emitting a paired graph at compile time (rather than re-deriving at runtime) makes the backward schedule deterministic and inspectable.
3. **`differentiable` annotation.** Per-pass and per-shader-module annotation declares which inputs participate in autodiff; annotated passes have backward kernels, and non-annotated passes are pass-through no-ops on the backward pass. Rationale: explicit annotation scopes the (expensive) backward-kernel generation to exactly the passes a method differentiates, and making non-annotated passes backward no-ops lets a production pass coexist in a differentiable graph without a gradient implementation.
4. **Adjoint buffer lifetime.** Per-pass adjoint buffers are frame-graph-managed transients with explicit retain across forward → backward, with the lifetime-extension rule recorded. Rationale: the backward pass reads forward-pass intermediates, so their lifetime must extend past where a normal transient would be reclaimed; making this an explicit frame-graph retain keeps adjoint memory accounted for rather than leaked or prematurely freed.
5. **Loss declaration.** A `Pass.Loss` provides the loss scalar + the `dL/d(rendered_image)` seed; subsequent backward passes propagate adjoints to scene parameters, with the canonical Loss-pass shape recorded. Rationale: a dedicated loss pass is the single well-defined seed point for backpropagation, decoupling the (method-chosen) loss from the rendering passes so the same differentiable graph serves any loss the method declares.
6. **Gradient sink.** Gradients with respect to scene parameters land in a `GradientSnapshot` returned to the runtime/method consumer, with the snapshot shape and an explicit research-only disclaimer recorded (this is a side channel, not the production data flow). Rationale: returning gradients through a named snapshot keeps the writeback a single auditable channel, and the explicit research-only disclaimer prevents the bidirectional path from being mistaken for (or reused as) the production extraction flow.
7. **Determinism.** Forward and backward must be bitwise reproducible given identical inputs and weights, with the determinism rule and test fixture recorded. Rationale: gradient-based optimization and finite-difference validation both require reproducibility — nondeterministic forward/backward would make gradients un-testable and optimization unstable — so determinism is a contract, not a best-effort property.
8. **Memory cost.** Differentiable mode retains the per-pass intermediates required by backward kernels; the memory amplification factor heuristic and the mitigation policy (gradient checkpointing as future work) are recorded. Rationale: retaining forward state is the inherent cost of reverse-mode autodiff, so recording the amplification heuristic sets expectations and naming gradient checkpointing as future work reserves the standard mitigation without committing to it now.
9. **Performance bound.** Differentiable mode is not bound to real-time budgets, and the policy that production recipes' performance targets do not regress is recorded. Rationale: inverse rendering is an offline/research workload where correctness and gradient quality dominate over frame rate, so explicitly de-coupling it from real-time budgets — while protecting production budgets — keeps the two modes' performance contracts independent.
10. **Layering.** `graphics/framegraph` owns forward+backward compile, `graphics/renderer` owns differentiable kernels, `methods/` consumes gradients, and `runtime/` provides the gradient-snapshot return path — the only place the snapshot-extraction boundary is bidirectional, and it is opt-in per recipe. Rationale: confining the single bidirectional channel to runtime (which already owns extraction) and gating it per recipe preserves AGENTS.md §2/§4 everywhere else, so graphics never gains live-scene access and the boundary stays unidirectional in production.
11. **Test split.** `unit` for analytic gradients of small differentiable kernels (finite-difference vs autodiff); `contract;graphics` for forward+backward graph compile + adjoint lifetime under null RHI; opt-in `gpu;vulkan` for end-to-end gradient correctness on a fixture. Rationale: finite-difference-vs-autodiff on small kernels is the strongest CPU gradient-correctness signal, compile/lifetime is device-independent, and only end-to-end device gradients need a GPU — keeping the default gate green.
12. **Forbidden in production.** Differentiable mode never runs in production builds by default, enforced by a recorded build-time gating rule. Rationale: build-time gating (not just a runtime flag) guarantees the differentiable path, its retained-memory cost, and its bidirectional channel cannot be reached in a shipped build, which is the strongest guarantee that production is unaffected.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-051-Impl-A** — Forward+backward graph compiler + adjoint buffer lifetime + null-RHI shape tests.
- **GRAPHICS-051-Impl-B** — `Pass.Loss` + gradient sink + `GradientSnapshot` return path.
- **GRAPHICS-051-Impl-C** — Reference differentiable kernels for transform/material/light parameters (gated by `GRAPHICS-041`).
- **GRAPHICS-051-Impl-D** — Method-consumer integration test (one fixture; not a method itself).
- **GRAPHICS-051-Impl-E** — Determinism + finite-difference parity unit tests.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The differentiable-rendering hookpoint reference for `docs/agent/method-workflow.md` is deferred to the implementation children (`GRAPHICS-051-Impl-B/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing when the feature is current-state per AGENTS.md §9.
- [x] The bidirectional-side-channel rule for `docs/architecture/graphics.md` is deferred to the same implementation children for the same reason.
- [x] The forward+backward compile shape for `src/graphics/framegraph/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Production recipes remain unchanged and unaffected.
- [x] Build-time gating prevents differentiable mode from shipping by default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve differentiable-rendering decisions are recorded with explicit answers and trade-off rationales: the isolated explicit-API `DifferentiableRender` recipe, the compile-time paired forward+backward graph, the `differentiable` annotation with backward-no-op pass-through, the frame-graph-retained adjoint buffer lifetime, the `Pass.Loss` seed point, the `GradientSnapshot` research-only side channel, the bitwise-determinism contract, the retained-intermediate memory heuristic with gradient-checkpointing reserved, the non-real-time performance policy protecting production budgets, the runtime-only bidirectional-boundary layering, the finite-difference / null-RHI-contract / opt-in-`gpu;vulkan` test split, and the build-time production gating. Implementation children `GRAPHICS-051-Impl-A..E` are identified but not opened; production recipes stay unchanged, the snapshot-extraction boundary stays unidirectional in production, and no autodiff bodies land. Per AGENTS.md §9 the method-workflow/architecture/framegraph doc updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No CPU autodiff framework dependency.
- No production-recipe gradient leakage.
- No bypass of the snapshot-extraction boundary outside the recorded gradient-sink side channel.
- No live ECS gradient writeback.
- No mixing of mechanical file moves with semantic refactors.
