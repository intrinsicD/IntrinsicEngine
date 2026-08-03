---
id: CI-016
theme: H
depends_on: [CI-015, BUILD-005, BUILD-006]
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
# CI-016 — Add a content-addressed build and test result cache

## Goal

- Reuse verified build actions and hermetic test results across local
  worktrees and CI only when their complete action/test identities are exactly
  equivalent, while preserving trustworthy execution and audit evidence.

## Non-goals

- No cache hit may substitute for a distinct required sanitizer, capability,
  stochastic-seed, timing, or resource identity.
- No default result caching for global-state, concurrency, real-filesystem,
  windowing, GPU/Vulkan, timing/SLO, or otherwise unaudited tests.
- No cache-dependent correctness: a cold/disabled cache must run the same plan
  and produce the same logical result.

## Context

- Owner: verifier cache adapter, cache policy, and CI trust configuration; no
  engine layer changes.
- `BUILD-006` selects the production build/action backend. This task uses that
  decision and adds test-result reuse rather than creating a competing cache.
- The conservative default is `non-cacheable`. Eligibility requires explicit
  hermeticity/isolation proof from `CI-015`.
- Protected CI is the trusted writer. Developer, agent, and untrusted PR
  contexts are read-only consumers unless they use a separate untrusted
  namespace that can never satisfy protected evidence.

## Right-sizing

- Element: cache integration could create a repository-owned cache service,
  client hierarchy, or second action backend.
- Simpler alternative: one thin adapter to the `BUILD-006` selected backend
  plus plain test-result objects and trust policy.
- Blast radius: verifier/cache configuration, test metadata, CI, tests, and
  docs; a disabled cache follows the exact same execution plan.
- Reintroduction trigger: a custom service is considered only after the
  selected backend demonstrably cannot satisfy a present trust/retention need
  and a separate operations owner is named.

## Control surfaces

- Config: versioned cache endpoint/namespace, trust role, size/retention, audit
  sampling, and fail-open-to-execution policy.
- UI: N/A.
- Agent/CLI: inspect/explain hit, miss, rejection, invalidation, and local
  cache-disable behavior.

## Required changes

- [ ] Integrate the `BUILD-006` selected action cache behind the unified
      verifier with exact content verification and a cache-disabled fallback.
- [ ] Define test-result keys from executable, runtime data, config,
      environment, seed, capability/resource, shard policy, and test-framework
      identities; omitted/unknown inputs force execution.
- [ ] Admit only audited pure cases and record the eligibility proof/policy
      version in the graph and result entry.
- [ ] Separate trusted protected-CI entries from untrusted local/PR writes and
      enforce read/write permissions and namespaces in code and workflow
      configuration.
- [ ] Verify content digests and receipt bindings on read; reject corrupt,
      partial, stale, mismatched, revoked, or unauthorized entries and execute
      the action/test normally.
- [ ] Add deterministic retention/eviction and stampede/concurrent-publication
      behavior without a repository-specific cache service.
- [ ] Add protected random shadow re-execution of accepted test cache hits and
      publish mismatch/poisoning telemetry as a blocking incident.
- [ ] Report executed versus reused actions/cases and wall, CPU, GPU, transfer,
      and storage costs separately.

## Tests

- [ ] Add key-invalidation tests for every build/test input class, policy
      version, variant, capability, resource, seed, data, and environment.
- [ ] Exercise cold, warm, disabled, unavailable, corrupt, partial,
      unauthorized, concurrent-writer, eviction, and cancellation paths.
- [ ] Compare cold execution, warm execution, and forced shadow re-execution
      for exact logical results and artifacts across the admitted cohort.
- [ ] Prove unaudited stateful/capability/timing tests always execute and cannot
      be made cacheable by missing metadata or caller flags.

## Docs

- [ ] Document key composition, eligibility, trust roles, retention, audit,
      incident response, cache-disable reproduction, and cost telemetry in CI
      and test strategy docs.
- [ ] Update the verification roadmap with the selected cache implementation
      and explicit non-cacheable classes.

## Acceptance criteria

- [ ] A cache-disabled or empty-cache run executes the identical logical plan
      and produces result parity with a warm run.
- [ ] Every accepted hit is bound to the complete graph/action/test identity
      and content-verified; any mismatch executes normally and is diagnosed.
- [ ] Protected evidence cannot be populated from an untrusted writer.
- [ ] Shadow re-execution finds zero unexplained result/artifact divergence for
      the admitted cohort.
- [ ] Reports show actual executed work separately from reused work and do not
      present cache hits as new executions.

## Verification

```bash
python3 tools/ci/verify.py run --profile edit --cache-mode off --output build/verification/cold.json
python3 tools/ci/verify.py run --profile edit --cache-mode read-write --output build/verification/warm.json
python3 tools/ci/verify.py run --profile edit --cache-mode audit --output build/verification/audit.json
python3 tests/regression/tooling/Test.VerificationCache.py
python3 tests/regression/tooling/Test.TestResultCacheParity.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Treating a cache lookup, plan, or artifact download as fresh execution
  evidence.
- Making cache service availability a prerequisite for correctness.
- Caching raw build trees/BMIs or any test whose complete inputs and isolation
  are unproved.
- Allowing fork PRs, arbitrary dirty worktrees, or agents to publish trusted
  entries.
