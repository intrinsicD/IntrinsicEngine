---
id: RUNTIME-136
theme: none
depends_on: [METHOD-013]
maturity_target: CPUContracted
---
# RUNTIME-136 — Progressive Poisson GPU backend toggle in the Sandbox

## Goal
- After METHOD-013 exposes the Vulkan-compute progressive-Poisson backend seam, add Sandbox command/config/UI wiring that lets users request CPU reference vs Vulkan compute, then displays the requested backend, actual backend, fallback reason, and parity deltas without changing sampler semantics.

## Non-goals
- No GPU backend implementation; METHOD-013 owns the Vulkan compute backend, fallback behavior, and parity diagnostics.
- No change to METHOD-012 CPU reference semantics or GEOM-035 mesh surface sampling.
- No figure/data export or frame capture; RUNTIME-133 and GRAPHICS-109 own those paths.
- No Vulkan, renderer, or RHI ownership in the Sandbox UI layer.

## Context
- Status: backlog. Blocked on METHOD-013 because the runtime command cannot expose a real backend choice until the method/runtime seam reports requested-vs-actual backend and parity telemetry.
- Owning subsystem/layer: `runtime`/UI composition. Runtime may consume the METHOD-013 runtime seam; `app` continues to import runtime only, and the UI must not call Vulkan/RHI directly.
- Follows RUNTIME-134, which retired at `CPUContracted` with the METHOD-012 CPU reference playground, GEOM-035 mesh preprocessing, config-control routing, debounced reruns, deterministic visualization properties, `cpu_reference` backend readout, and per-level accepted-count readout.

## Control surfaces
- Config: extend `EngineConfig.sandbox.progressive_poisson` only if METHOD-013 exposes a stable backend-selection axis.
- UI: Sandbox `PointCloud > Processing > Progressive Poisson Sampling` and `Mesh > Processing > Progressive Poisson Sampling` backend selector plus diagnostics readout.
- Agent/CLI: existing `Engine::PreviewEngineConfigControlDocument` / `Engine::ApplyEngineConfigHotSubset` config-control facade and `ApplySandboxEditorProgressivePoissonCommand(...)`.

## Backends
- Backend axis: CPU reference vs Vulkan compute, consumed from METHOD-013.
- Actual backend and parity/fallback diagnostics must come from the METHOD-013 result contract rather than UI-local inference.

## Required changes
- [ ] Extend the Sandbox progressive-Poisson command/config DTOs to carry the requested backend when METHOD-013 exposes that axis.
- [ ] Route the command through the METHOD-013 runtime seam so CPU reference, Vulkan compute, and CPU fallback all return one result shape.
- [ ] Add UI controls for backend selection and readouts for requested backend, actual backend, fallback reason, parity deltas, and existing per-level accepted counts.
- [ ] Preserve the existing RUNTIME-134 CPU reference behavior and deterministic published properties when the requested or actual backend is CPU.

## Tests
- [ ] Add headless runtime contract coverage for forced CPU selection through the command and config-control path.
- [ ] Add fallback coverage using the Null/non-operational device path or METHOD-013 test seam, asserting requested GPU reports actual CPU with an explicit fallback reason.
- [ ] Add parity-readout coverage using METHOD-013 diagnostics without requiring a GPU in the default CPU gate.
- [ ] Keep any real Vulkan UI/runtime smoke opt-in with `gpu;vulkan` labels.

## Docs
- [ ] Update `src/runtime/README.md` and `methods/geometry/progressive_poisson/README.md` with backend-toggle behavior, requested-vs-actual semantics, and fallback display.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` after promotion or retirement.

## Acceptance criteria
- [ ] Users can request CPU reference or Vulkan compute from the Sandbox progressive-Poisson panels once METHOD-013 is present.
- [ ] The UI shows requested backend, actual backend, fallback reason when applicable, parity deltas, and per-level accepted counts from the backend result.
- [ ] Default CPU/headless tests cover CPU and fallback semantics; any Vulkan execution remains opt-in under `gpu;vulkan`.
- [ ] Layering holds: app imports runtime only, UI does not own Vulkan/RHI calls, and sampler semantics stay in METHOD-012/013.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Implementing the GPU sampler backend in this task.
- Bypassing the config-control facade with ad-hoc UI-only backend state.
- Leaking `Vk*`, graphics backend handles, or RHI device ownership into public UI/app APIs.

## Maturity
- Target: `CPUContracted` UI/config contract over the METHOD-013 backend-selection seam.
- `Operational` owned by `METHOD-013`; this task consumes backend telemetry and does not introduce a new GPU backend.
