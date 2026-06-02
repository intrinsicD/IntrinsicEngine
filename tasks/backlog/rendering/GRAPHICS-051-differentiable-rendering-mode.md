# GRAPHICS-051 — Differentiable rendering mode (planning)

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

## Design decisions to record
1. **Recipe selection.** `DifferentiableRender` is a separate future recipe selected through an explicit method/runtime API; production recipes are unchanged. Record the rule that differentiable mode is opt-in per method and does not depend on the retired `RenderConfig::FrameRecipe` selector.
2. **Forward + backward graph.** Compile produces a paired forward + backward render graph. Backward graph executes in reverse pass order, consuming per-pass adjoints. Record the compile output shape.
3. **`differentiable` annotation.** Per-pass and per-shader-module annotation declares which inputs participate in autodiff. Annotated passes have backward kernels; non-annotated passes are no-ops on the backward pass (pass-through).
4. **Adjoint buffer lifetime.** Per-pass adjoint buffers are frame-graph-managed transients with explicit retain across forward → backward. Record the lifetime extension rule.
5. **Loss declaration.** A `Pass.Loss` provides the loss scalar + dL/d(rendered_image) seed. Subsequent backward passes propagate adjoints to scene parameters. Record the canonical Loss-pass shape.
6. **Gradient sink.** Gradients with respect to scene parameters land in a `GradientSnapshot` returned to the runtime/method consumer. Record the snapshot shape and the explicit research-only disclaimer (this is a side channel, not the production data flow).
7. **Determinism.** Forward and backward must be bitwise reproducible given identical inputs and weights. Record the determinism rule and the test fixture.
8. **Memory cost.** Differentiable mode retains per-pass intermediates required by backward kernels. Record the memory amplification factor heuristic and the mitigation policy (gradient checkpointing as future work).
9. **Performance bound.** Differentiable mode is not bound to real-time budgets. Record the policy that production recipes' performance targets do not regress.
10. **Layering.** `graphics/framegraph` owns forward+backward compile; `graphics/renderer` owns differentiable kernels; `methods/` consumes gradients. `runtime/` provides the gradient-snapshot return path; this is the only place the snapshot-extraction boundary is bidirectional, and it is opt-in per recipe.
11. **Test split.** `unit` for analytic gradients of small differentiable kernels (finite-difference vs autodiff); `contract;graphics` for forward+backward graph compile + adjoint lifetime under null RHI; opt-in `gpu;vulkan` for end-to-end gradient correctness on a fixture.
12. **Forbidden in production.** Differentiable mode never runs in production builds by default. Record the build-time gating rule.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-051-Impl-A** — Forward+backward graph compiler + adjoint buffer lifetime + null-RHI shape tests.
- **GRAPHICS-051-Impl-B** — `Pass.Loss` + gradient sink + `GradientSnapshot` return path.
- **GRAPHICS-051-Impl-C** — Reference differentiable kernels for transform/material/light parameters (gated by `GRAPHICS-041`).
- **GRAPHICS-051-Impl-D** — Method-consumer integration test (one fixture; not a method itself).
- **GRAPHICS-051-Impl-E** — Determinism + finite-difference parity unit tests.

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/agent/method-workflow.md` with the differentiable-rendering hookpoint reference.
- [ ] Update `docs/architecture/graphics.md` with the bidirectional-side-channel rule.
- [ ] Update `src/graphics/framegraph/README.md` with the forward+backward compile shape.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Production recipes remain unchanged and unaffected.
- [ ] Build-time gating prevents differentiable mode from shipping by default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No CPU autodiff framework dependency.
- No production-recipe gradient leakage.
- No bypass of the snapshot-extraction boundary outside the recorded gradient-sink side channel.
- No live ECS gradient writeback.
- No mixing of mechanical file moves with semantic refactors.
