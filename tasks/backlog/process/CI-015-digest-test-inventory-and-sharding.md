---
id: CI-015
theme: H
depends_on: [CI-012, CI-013]
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
# CI-015 — Add digest-keyed test inventory and deterministic sharding

## Goal

- Execute the existing logical test inventory through digest-keyed case
  discovery and duration-balanced deterministic shards while preserving
  per-case identity, isolation, resources, diagnostics, and exact result
  parity.

## Non-goals

- No test-result caching; `CI-016` owns result reuse after hermeticity proof.
- No one-executable-per-source rewrite and no proprietary assertion format.
- No grouping of global-state, concurrency, filesystem, windowing, RHI/GPU,
  death, timing, or stochastic tests without explicit reset/isolation proof.

## Context

- Owner: test registration/execution support under `cmake/`, `tests/`, and
  `tools/ci`; no production behavior changes.
- GoogleTest remains the assertion framework and CTest remains an IDE/local
  compatibility projection. Normal verification must not launch unchanged
  executables solely to rediscover their case names.
- Pure tests benefit from fewer process launches; stateful or capability-heavy
  tests require process isolation. The safe default for missing metadata is
  isolated and non-cacheable.
- The executor must account for in-process worker pools and CTest-style
  `PROCESSORS` reservations when scheduling shards.

## Right-sizing

- Element: digest discovery and sharding could introduce a proprietary test
  framework or one executable per test file.
- Simpler alternative: retain GoogleTest/CTest, cache their inventory by
  producer digest, and pass deterministic filters to cohesive existing
  producers.
- Blast radius: test CMake/support, verifier tooling, test binaries, and docs;
  assertion sources and production APIs change only when independently needed.
- Reintroduction trigger: a new test protocol is considered only when a present
  non-GoogleTest producer cannot emit the common logical-case/result contract.

## Required changes

- [ ] Cache each producer's expanded logical case inventory by executable and
      runtime-data digest, invalidating it on any discovery-affecting input.
- [ ] Add one owning registration surface for isolation class, resources,
      timeout, seed policy, cacheability candidate, and capability requirements;
      derive everything else into the evidence graph.
- [ ] Generate deterministic duration-balanced shards for eligible pure cases,
      with a stable fallback when no trustworthy timing history exists.
- [ ] Keep stateful/death/concurrency/filesystem/window/RHI/GPU/timing/stochastic
      cases in isolated processes unless a fixture-level reset/hermeticity audit
      explicitly admits them.
- [ ] Emit per-logical-case XML/JSON status, duration, seed, stdout/stderr
      digest, and a direct reproduction filter even when cases share a shard.
- [ ] Reconcile shard filters against the graph before execution and reject
      missing, duplicate, disabled-state-drifted, or unexpected cases.
- [ ] Retain the current individual/grouped CTest plans as shadow controls;
      ordinary PRE_TEST rediscovery is not deleted until `CI-020`.

## Tests

- [ ] Prove exact case/status parity across individual CTest, current grouped
      CTest, and the new shard executor for every required CPU variant.
- [ ] Add invalidation tests for executable, data, config, environment,
      disabled-case, and discovery-policy changes.
- [ ] Add scheduler tests for duration balancing, resource reservations,
      oversubscription avoidance, shard crashes/timeouts, and deterministic
      reproduction.
- [ ] Prove stateful/capability-heavy fixtures default to isolation and cannot
      become grouped from missing metadata.
- [ ] Compare production source/branch coverage with the current execution plan
      under identical product identity.

## Docs

- [ ] Update `tests/README.md`, test strategy, and `tools/ci/README.md` with
      inventory keys, shard policy, isolation classes, resource accounting,
      and reproduction commands.
- [ ] Update current CMake helper documentation while clearly marking CTest
      replacement retirement as deferred to `CI-020`.

## Acceptance criteria

- [ ] Unchanged binaries reuse discovery without executing discovery again;
      changed discovery inputs invalidate it deterministically.
- [ ] Every required logical case executes exactly once per plan and retains an
      individually addressable result/reproduction command.
- [ ] Logical inventory, status, and source/branch coverage match the current
      control plans exactly.
- [ ] No unproven stateful or capability-heavy case shares a process shard.
- [ ] Scheduler concurrency and in-test worker reservations cannot
      oversubscribe the declared runner budget.

## Verification

```bash
cmake --preset ci --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci --target IntrinsicCpuTests
python3 tools/ci/verify.py run --profile merge --variant cpu --executor shard --output build/ci/verification/shard-receipt.json
python3 tests/regression/tooling/Test.DigestTestInventory.py
python3 tests/regression/tooling/Test.TestShardParity.py --build-dir build/ci
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Hiding individual failures behind only a shard-level status.
- Treating producer labels as proof that every case is pure or cacheable.
- Increasing process or worker concurrency from host core count without
  explicit resource accounting and matched evidence.
- Deleting current execution controls before final cutover.
