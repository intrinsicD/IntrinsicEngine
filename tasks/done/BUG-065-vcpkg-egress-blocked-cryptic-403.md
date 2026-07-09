---
id: BUG-065
theme: infra
depends_on: []
---
# BUG-065 — vcpkg bootstrap egress block surfaces as a cryptic 403

## Status
- **Retired 2026-07-09.** Commit/PR: pending local change set on branch
  `claude/agentic-workflow-continue-fl4754`.
- In-repo scope (fail-loud diagnostics + status marker + opt-in pre-bake) is
  complete and verified live in a blocked sandbox. The underlying egress policy
  is environment-level and out of repository scope (see Non-goals / Docs).

## Goal
- Make a session whose network egress blocks the vcpkg tool download fail
  **loudly with an actionable diagnosis** instead of a cryptic
  `curl: (22) ... 403`, and record the vcpkg reachability status during session
  setup so the later `cmake --preset ci` failure is pre-explained.

## Non-goals
- Not changing the network egress policy itself — that is chosen per environment
  and is not a repository artifact.
- No new dependency-management mechanism; vcpkg manifest mode is unchanged.
- No attempt to route around, cache-bust, or retry a 403 policy denial
  (`/root/.ccr/README.md`: report, do not retry).

## Context
- Owner/layer: infra / `tools/setup`.
- Symptom: on a warm-toolchain cloud session, `cmake --preset ci` aborts during
  `project()` with `vcpkg install failed` and the underlying
  `build/ci/vcpkg-bootstrap.log` shows only `Downloading vcpkg-glibc...
  curl: (22) The requested URL returned error: 403`. This reads as a build
  defect but is an environment egress policy.
- Evidence (probed 2026-07-09 in this session):
  - `git ls-remote https://github.com/...` → OK (git protocol allowed).
  - `curl https://api.github.com` → 200 (GitHub API allowed).
  - `curl https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-glibc`
    → **403** (general github web / release-asset GET blocked).
  - `curl "$HTTPS_PROXY/__agentproxy/status"` → `recentRelayFailures: []`
    (a policy denial, not a relay/TLS failure).
- The `ci`/`dev` presets chainload the repository-local vcpkg toolchain, whose
  `bootstrap-vcpkg.sh` downloads a prebuilt tool binary (`vcpkg-glibc`, release
  tag `2026-05-27`) from GitHub release assets — exactly the blocked class — so
  no preset can configure and the CPU test gate cannot run in such a session.
- `tools/setup/agent_session_setup.sh` provisions the clang-20 toolchain +
  Ninja + Vulkan/windowing headers, but short-circuits when the toolchain is
  already warm and never touched vcpkg, so the block only surfaced later as the
  preset 403 with no breadcrumb.

## Required changes
- [x] Add `tools/setup/vcpkg_preflight.sh`: a single-probe egress preflight that
      emits a `ready|reachable|blocked|unknown` token and, on `blocked`, prints
      an actionable diagnosis (blocked host, why, environment-level fixes).
- [x] Gate `tools/setup/bootstrap_vcpkg.sh` on the preflight so it aborts before
      the doomed download with the diagnosis (exit 3), honoring
      `INTRINSIC_VCPKG_FORCE=1` to attempt anyway, and wrap the bootstrap call to
      re-surface the diagnosis if the download fails despite a passing probe.
- [x] Have `tools/setup/agent_session_setup.sh` always run the preflight (both
      the warm-toolchain short-circuit and the full path), record the status to
      `/tmp/intrinsic-session-setup.vcpkg`, and add an opt-in pre-bake
      (`--bootstrap-vcpkg` / `INTRINSIC_SESSION_BOOTSTRAP_VCPKG=1`) that
      bootstraps the vcpkg tool when the host is reachable.

## Tests
- [x] `bash -n` clean on all three setup scripts.
- [x] Live in a blocked sandbox: `tools/setup/vcpkg_preflight.sh` prints the
      diagnosis, emits `blocked` on stdout, and exits 3.
- [x] Live: `tools/setup/bootstrap_vcpkg.sh` aborts at the preflight gate with
      the diagnosis and exit 3 (no cryptic curl 403 reaches the user).
- [x] Live: the status-marker write path records `blocked` to the status marker.

## Docs
- [x] Document the blocked-vcpkg-download symptom, root cause, and the
      environment-level fixes in `docs/build-troubleshooting.md`.
- [x] Note the always-on preflight, the status marker, and the opt-in pre-bake
      flag in `AGENTS.md` §"Shared optional session setup".

## Acceptance criteria
- [x] A blocked session prints a clear, actionable diagnosis (which host, why,
      how to fix) rather than a bare `curl: (22) ... 403`.
- [x] Session setup records a machine-readable vcpkg reachability token.
- [x] The hardening never makes setup fatal on its own (preflight failures are
      non-fatal to `agent_session_setup.sh`).
- [x] Environment-level resolution (allow the host, or pre-bake vcpkg into the
      snapshot) is documented for the operator.

## Verification
```bash
bash -n tools/setup/vcpkg_preflight.sh tools/setup/bootstrap_vcpkg.sh tools/setup/agent_session_setup.sh
tools/setup/vcpkg_preflight.sh          # blocked host: prints diagnosis, exit 3
tools/setup/bootstrap_vcpkg.sh          # aborts at the gate with the diagnosis
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Disabling TLS verification, unsetting `HTTPS_PROXY`, or retrying/rerouting a
  403 policy denial.
- Making vcpkg bootstrap or the preflight a hard, fatal dependency of
  `agent_session_setup.sh`.
- Introducing a non-vcpkg dependency path to "work around" the block.

## Execution log
- 2026-07-09: Reproduced the preset `vcpkg install failed` / `curl 403`, then
  isolated the cause with single diagnostic probes (git + API allowed; github
  web/release GET → 403; `recentRelayFailures: []`).
- 2026-07-09: Added `vcpkg_preflight.sh`, gated `bootstrap_vcpkg.sh`, and made
  `agent_session_setup.sh` always report vcpkg status with an opt-in pre-bake.
- 2026-07-09: Verified live — preflight and the bootstrap gate both emit the
  diagnosis and exit 3; the status marker records `blocked`; `bash -n` clean.
