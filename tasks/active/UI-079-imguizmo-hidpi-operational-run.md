---
id: UI-079
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: operational rerun of existing smokes on another host; evidence is the recorded run
maturity_target: Operational
contract_schema: 1
contracts: [runtime.gizmo-transform-session]
contract_review: Extends the Operational evidence of the gizmo-transform-session contract (scene rectangle and cursor mapping at a pixel ratio other than 1); no contract change.
---
# UI-079 — Run the ImGuizmo smokes at a real HiDPI pixel ratio

Status: in-progress. Phase 1 (X11 preparation) implemented; phase 2 needs the
operator's GNOME Wayland session.

## Goal
- Prove the Sandbox ImGuizmo drags on Vulkan with a framebuffer/window ratio
  other than 1, so the operational claim (ara C118) is no longer limited to
  ratio 1.

## Context
- UI-078 slice 4a (`49a609334`) and BUG-236 ran the three ImGuizmo smokes
  (`ImGuizmoGroupDragAndUndoReachSameFramePixels`,
  `ImGuizmoOrthographicSplitViewportDragAndUndo`,
  `ImGuizmoScaleAxisDragAndUndo`) only on X11 at ratio 1. The ratio ≠ 1
  mapping rests on CPU evidence:
  `SceneInteractionModule.GizmoUiSceneRectIsTheCurrentClaimMappedBackFromFramebufferPixels`.
- Host: GNOME on Ubuntu with display scaling. A forced
  `DisplayFramebufferScale` or an XWayland run does not count.

## Decisions (operator, 2026-10-08)
- GLFW keeps its default platform selection (`GLFW_ANY_PLATFORM`): native
  Wayland inside a Wayland session, X11 under X11. No config field; the
  backend logs the selected platform and GLFW version once at init.
- Linux GLFW is built with X11 and Wayland (`vcpkg.json` `windowing`).
- 200 % is run first, then 150 % fractional scaling; the log records ratio,
  GLFW content scale and ImGui framebuffer scale as floats, so a non-integer
  or compositor-rounded ratio is visible.
- `GlfwPlatformSmoke.NativeFocusChangesEmitWindowFocusEvents` skips under
  native Wayland (no programmatic focus without user input); X11 stays strict.

## Phases
1. X11 session: Wayland-enabled GLFW build, platform/device/scale logs, smoke
   settle on stable window/framebuffer/backbuffer sizes, focus-smoke skip,
   X11 regression. Commit and push before logout; record the tested commit
   below.
2. Operator logs into "Ubuntu on Wayland", sets the test monitor to 200 %,
   keeps the session unlocked and runs `claude --continue` in the repository
   (logging out ends this Claude Code session). Run the commands below.
3. Copy the relevant logs and host data to `ara/evidence/diagnostics/`, bind
   C118 to them, update ADR 0006 Validation, retire this task.

Phase-1 tested commit: `d6af297f9`. X11 logs:
`build/ui079-evidence/` (not versioned).

## Acceptance criteria
- [ ] All three `RuntimeSandboxAcceptanceGpuSmoke.ImGuizmo*` cases pass 3/3
      under native Wayland at 200 % and 3/3 at 150 %; a skipped ImGuizmo case
      does not count. Per scale: GLFW platform, window, framebuffer and
      backbuffer sizes, ratio X/Y, content scale X/Y, ImGui scale X/Y, scene
      rectangles (logical and physical), GPU/driver, session and revision are
      recorded here.
- [ ] Any failure is fixed or filed with its repro; ara C118 and ADR 0006
      Validation are updated to the measured ratios.

## Phase 2 commands
No rebuild for the session change. Do not carry display variables over from
the X11 session. Shell state does not persist between Claude Code Bash calls:
start each run block below with the setup block.

Setup block:

```bash
set -euo pipefail
cd ~/Documents/IntrinsicEngine
test "$XDG_SESSION_TYPE" = wayland
test -n "${WAYLAND_DISPLAY:-}"
gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver \
  --method org.gnome.ScreenSaver.GetActive   # must print (false,)
git diff --exit-code
git diff --cached --exit-code
mkdir -p build/ui079-evidence

host() {
  git log -1 --format='%H %s'
  printf 'session=%s desktop=%s wayland=%s display=%s\n' \
    "$XDG_SESSION_TYPE" "${XDG_CURRENT_DESKTOP:-}" "$WAYLAND_DISPLAY" "${DISPLAY:-}"
  gnome-shell --version
  gsettings get org.gnome.mutter experimental-features
  nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
  pkg-config --modversion wayland-client wayland-cursor wayland-egl xkbcommon
}
smokes() { # $1 = 200 or 150
  host > "build/ui079-evidence/wayland-$1-host.txt"
  vulkaninfo --summary > "build/ui079-evidence/wayland-$1-vulkaninfo.txt" 2>&1
  gnome-session-inhibit --inhibit idle --reason "UI-079 HiDPI smoke" \
    ctest --test-dir build/ci-vulkan -V --no-tests=error --timeout 120 --parallel 1 \
      -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImGuizmo' \
      --repeat until-fail:3 \
      --output-log "$PWD/build/ui079-evidence/wayland-$1-imguizmo.log" \
      --output-junit "$PWD/build/ui079-evidence/wayland-$1-imguizmo.xml"
  grep '\[UI-078 smoke\]' "build/ui079-evidence/wayland-$1-imguizmo.log"
}
```

Run at 200 % (setup block first):

```bash
smokes 200
ctest --test-dir build/ci-vulkan -V --no-tests=error --timeout 120 --parallel 1 \
  -R '^GlfwPlatformSmoke\.' --output-log "$PWD/build/ui079-evidence/wayland-platform.log"
```

Operator: Settings > Displays > Scale 150 %. If no 150 % option is offered,
enable mutter's `scale-monitor-framebuffer` experimental feature while
keeping the existing ones, then pick 150 % (log out and in again if the option
still does not appear):

```bash
cur=$(gsettings get org.gnome.mutter experimental-features)
echo "$cur" > build/ui079-evidence/mutter-experimental-features.orig
new=$(python3 -c 'import ast, sys
v = ast.literal_eval(sys.argv[1].removeprefix("@as "))
print(v if "scale-monitor-framebuffer" in v else v + ["scale-monitor-framebuffer"])' "$cur")
gsettings set org.gnome.mutter experimental-features "$new"
# Restore later, if wanted:
# gsettings set org.gnome.mutter experimental-features "$(cat build/ui079-evidence/mutter-experimental-features.orig)"
```

Run at 150 % (setup block first):

```bash
smokes 150
```

Expected: `platform=Wayland`, 9 ImGuizmo passes per scale, ratio 2 at 200 %;
at 150 % the measured ratio (1.5 with `wp_fractional_scale_v1`, otherwise a
compositor-scaled integer buffer) is recorded as measured. Only the focus
smoke may skip, with its Wayland reason.

## Verification
```bash
cmake --preset ci-vulkan -DVCPKG_MANIFEST_INSTALL=ON -DINTRINSIC_BUILD_SANDBOX=ON -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicPlatformGlfwSmokeTests
ctest --test-dir build/ci-vulkan -V --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImGuizmo' --repeat until-fail:3
ctest --test-dir build/ci-vulkan -V --timeout 120 --no-tests=error -R '^GlfwPlatformSmoke\.'
python3 tools/agents/check_ara_claims.py --root . --strict
```
