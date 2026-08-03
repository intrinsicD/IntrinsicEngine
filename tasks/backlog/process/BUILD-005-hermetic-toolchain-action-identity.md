---
id: BUILD-005
theme: H
depends_on: [CI-012]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# BUILD-005 — Define hermetic toolchain and action identity

## Goal

- Make configure, compile, link, shader, and test actions reproducibly
  identifiable from pinned toolchain/environment inputs so later local and
  remote reuse cannot cross unsafe C++23-module or capability contexts.

## Non-goals

- No build-backend selection or performance claim; `BUILD-006` owns the
  matched bake-off.
- No sharing of raw CMake/Ninja build trees, absolute-path-coupled BMIs, or
  unverified compiler-cache entries.
- No requirement that untrusted PRs or developer/agent worktrees publish to a
  trusted shared cache.

## Context

- Owner: build/toolchain/dependency setup and action identity; no engine layer
  changes.
- A valid identity must include the complete Clang/`clang-scan-deps`/linker,
  sysroot, CMake/Ninja, Python tooling, shader tools, Vulkan SDK/loader where
  relevant, vcpkg lock/ports, preset flags, environment allowlist, inputs,
  working-directory normalization, and resource/capability class.
- Named-module producer/consumer invalidation and path normalization are
  correctness requirements, not cache tuning.
- One pinned environment image/lock surface should serve local bootstrap and CI
  where possible; do not duplicate package setup in every workflow.

## Right-sizing

- Element: environment/action identity could become a wrapper hierarchy around
  every tool.
- Simpler alternative: one versioned lock plus canonical plain action records
  consumed directly by setup, verifier, and CI.
- Blast radius: presets, setup, dependency/build tooling, CI, regression tests,
  and docs only; no engine module interface changes.
- Reintroduction trigger: a tool-specific adapter exists only when a present
  selected backend cannot express that tool through the common action record.

## Control surfaces

- Config: versioned environment lock/image and explicit action-input allowlist.
- UI: N/A.
- Agent/CLI: environment inspection, action-key explanation, and reproduction.

## Required changes

- [ ] Define a pinned, content-addressed toolchain/environment description for
      every required variant and document which host/kernel/GPU inputs remain
      outside the image.
- [ ] Remove per-workflow dependency-install drift by routing setup through one
      versioned environment/bootstrap surface.
- [ ] Define build and test action keys with canonical commands, declared input
      digests, toolchain/sysroot/environment identity, normalized paths, and
      capability/resource identity.
- [ ] Include C++23 module producer/consumer metadata and prove interface,
      implementation, compiler flag, toolchain, sysroot, and dependency changes
      invalidate the correct actions.
- [ ] Define trusted-cache roles: protected CI writer; untrusted PR,
      developer, and agent read-only unless explicitly isolated; content hashes
      are verified on every read.
- [ ] Add diagnostics that explain the first differing action-key field and a
      replay command for local reproduction.
- [ ] Add the reusable action-identity/trust contract to the contract catalog
      with canonical prose and executable proof, then update this task's
      `contracts` declaration before retirement.

## Tests

- [ ] Compare normalized action identities across at least two clean build
      roots and the reference CI environment.
- [ ] Add invalidation fixtures for `.cpp`, `.cppm`, headers, generated inputs,
      flags, compiler/scanner/linker, sysroot, vcpkg, shader tools, config/data,
      and capability changes.
- [ ] Prove absolute build/worktree paths do not prevent safe reuse while
      lexical source differences that affect outputs do invalidate.
- [ ] Prove corrupt, truncated, mismatched, or unauthorized artifacts are
      rejected and cannot become trusted evidence.

## Docs

- [ ] Update build/setup docs, `CMakePresets.json` guidance, CI policy, and
      `tools/setup/README.md` with environment and trust ownership.
- [ ] Add the implemented stable contract to the catalog and link it from the
      verification architecture.

## Acceptance criteria

- [ ] Equivalent clean roots emit equivalent logical action identities and
      reproducible outputs for the declared reference environment.
- [ ] Every tested module/toolchain/dependency/capability change invalidates
      exactly the required action closure.
- [ ] Untrusted writers and corrupt artifacts cannot populate or satisfy the
      trusted evidence path.
- [ ] No raw build directory or unidentified BMI is used as a portable cache
      unit.
- [ ] Workflow package/setup commands have one versioned source instead of
      repeated mutable definitions.

## Verification

```bash
python3 tools/setup/verify_environment_lock.py --root . --strict
python3 tools/ci/verification_manifest.py --root . --build-dir build/ci-fast --check-actions
python3 tests/regression/tooling/Test.ActionIdentity.py
python3 tests/regression/tooling/Test.ActionCacheTrust.py
cmake --preset ci-fast --fresh
cmake --build --preset ci-fast --target IntrinsicPrFastTests
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Keying reuse only by source paths, timestamps, compiler version strings, or
  partial command hashes.
- Publishing trusted entries from fork PRs or arbitrary dirty worktrees.
- Caching raw build directories/BMIs or masking stale-module failures as
  environmental flakes.
- Modifying production engine code.
