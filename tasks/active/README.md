# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-088` — Resolved UV rendering and bake texture residency](GRAPHICS-088-resolved-uv-rendering-and-bake-residency.md):
  active CPU-contracted graphics slice for resolved-UV material sampling,
  generated texture asset bindings, UV debug material, and UV bake packet
  provenance; full operational Vulkan/generated-bake proof remains gated by
  `RUNTIME-109` / `ASSETIO-008`.
- [`INFRA-001` — Move third-party dependencies to a vcpkg manifest](INFRA-001-vcpkg-manifest-mode.md):
  active dependency-management migration slice; CI configure timing is now
  instrumented, with retirement waiting on post-commit exact-hit timing
  evidence.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
