---
id: BUILD-012
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive dependency evaluation; evidence is the diff or the recorded no-go reason, a fresh vcpkg configure, the ImGui Vulkan smokes, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. No catalog contract owns third-party dependency packaging; ADR 0020 and docs/agent/contract.md own the vcpkg policy and are updated if the overlay goes.
---
# BUILD-012 — Evaluate replacing the ImGui vcpkg overlay port with the registry port

## Goal
- Decide whether `tools/vcpkg/overlay-ports/imgui` can be replaced by the
  registry `imgui` port at the pinned `builtin-baseline` (`06a7fdd56…`, port
  version 1.92.8, the same version as the overlay) plus features. If it can,
  replace it; if not, record why and keep the overlay.
- Origin: REVIEW-007 T22 (2026-10-06). The xatlas overlay stays: the pinned
  baseline has no `xatlas` port (REVIEW-007 refuted that part).

## Acceptance criteria
- [ ] The evaluation covers what the overlay does that registry features do
      not replace directly. The overlay installs `imgui_impl_glfw`/
      `imgui_impl_vulkan` *sources* to `share/imgui/backends`.
      `cmake/Dependencies.cmake` compiles them into the repo-owned `imgui_lib`
      with `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` and `GLFW_INCLUDE_NONE` and links
      `volk`. The registry `vulkan-binding` feature depends on the vcpkg
      `vulkan` port, but ADR 0020 keeps the Vulkan SDK outside vcpkg. The
      overlay also selects the `-docking` source tag through
      `docking-experimental`.
- [ ] If replaced: `vcpkg.json` uses registry features, the overlay directory
      is deleted, `imgui_lib`, `imgui_core_lib`, `imguizmo_lib` and the headless
      (`INTRINSIC_HEADLESS_NO_GLFW`) path still configure, and ADR 0020 and
      `docs/build-troubleshooting.md` are updated. If kept: add the reason to
      ADR 0020 and close this task with no code change.
- [ ] A fresh `ci-vulkan` configure resolves the manifest. The ImGui GPU smokes
      and the Sandbox acceptance smoke (which composes the Sandbox editor
      controller) pass on Vulkan.

## Verification
```bash
cmake --preset ci-fast
cmake --preset ci-vulkan --fresh
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^(ImGuiSurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke)\.'
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Overlay files: `portfile.cmake`, `CMakeLists.txt`, `imgui-config.cmake.in`,
  `vcpkg.json` under `tools/vcpkg/overlay-ports/imgui`; wiring in
  `vcpkg-configuration.json` (`overlay-ports`) and `cmake/Dependencies.cmake`.
- A manifest or overlay edit starts a new CI binary-cache line
  (`docs/build-troubleshooting.md`).
- UI-078 moves `imguizmo` within `vcpkg.json`; coordinate if both are open.
