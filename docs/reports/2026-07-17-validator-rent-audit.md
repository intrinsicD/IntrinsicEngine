# Validator Rent Audit — 2026-07-17

## Decision

The inventory snapshot is commit `4ba8a40a` (the promotion of
`PROC-027`), before applying any verdict. It contains **57 tracked Python
files** under `tools/`: 36 IntrinsicEngine repository tools and 21 files in the
separately shipped AgentKit product.

- **Keep: 53.** The tool has a recent protective finding, is a deterministic
  producer or fail-fast guard consumed by a live workflow, enforces a unique
  repository contract, or is a tested part of the separately shipped AgentKit
  product.
- **Warn: 2.** Keep the tool explicitly non-blocking:
  `tools/agents/check_audit_cadence.py` and
  `tools/analysis/module_fanout.py`. Both already run in report-only mode in
  CI; this audit does not weaken a strict gate.
- **Retire: 2.** Remove `tools/repo/check_pr_contract.py` and its callers. Its
  CI mode checks only static headings in the checked-in pull-request template;
  local mode is advisory and always succeeds. Also remove AgentKit's generated
  `check_prereqs.py`: the scaffold copies and documents it but no generated
  runner or workflow invokes it, and the product self-test never exercises it.
  The PR-contract verdict does **not** apply to AgentKit's config-driven
  generated checker.

No validator that produced a protective finding in the primary evidence
window is retired or downgraded.

## Scope And Evidence

The primary evidence window is **2026-06-09 through 2026-07-17**. When that
window contains no finding, the audit uses history from the tool's
introduction through 2026-07-17. Evidence was collected from workflow wiring,
hosted job-step results, regression coverage, task retirement records, direct
tool execution, and repository references.

Evidence types mean:

- **Protective finding:** the tool exposed real repository drift that required
  a source, task, documentation, or generated-artifact correction.
- **Load-bearing producer/guard:** downstream work consumes its output or
  depends on its fail-fast state; removing it would break or obscure a live
  workflow even without a recent policy violation.
- **Contract gate:** it uniquely and deterministically enforces an active
  repository contract, normally with focused regression coverage.
- **Product evidence:** AgentKit's throwaway-repository self-test exercises the
  shipped file or its generated output. AgentKit is a distinct bootstrap
  product, not a second copy of IntrinsicEngine's repository validators.
- **Tool defect / synthetic only:** a false positive, stale allowlist, parser
  bug, or fixture-only failure. These are recorded but are not credited as
  protective findings.
- **No unique evidence:** no finding, unique invariant, producer role, or
  meaningful negative test was found. This is the retirement threshold.

The hosted Actions step audit in the primary window found real failures in the
generated module-inventory freshness step on 2026-07-01 and 2026-06-27, and in
strict task policy on 2026-06-26. Nightly compile-hotspot and module-fanout
steps completed on 2026-07-03 and 2026-07-06; most later nightly runs skipped
them after upstream configure, SLO, or test failures. Absence of a failure in a
skipped step is not treated as evidence.

Recent repository evidence used below includes:

- [`PROC-012`](../../tasks/archive/PROC-012-resolve-duplicate-geom-027-id.md):
  strict task policy exposed the duplicate `GEOM-027` ID on 2026-06-27.
- [`BUG-077`](../../tasks/archive/BUG-077-architecture-backlog-index-links-retired-arch-tasks.md):
  task-state validation exposed seven retired architecture links under an
  active heading on 2026-07-10.
- [`PROC-016`](../../tasks/archive/PROC-016-skills-docs-mirror-drift-fixes.md):
  the 2026-07-08 review found missing mirrors and dead skill routings.
- [`WORKSHOP-001`](../../tasks/archive/WORKSHOP-001-layer-check-module-and-cmake-aware.md):
  layering validation exposed promoted RHI-to-platform imports and a CMake
  link; `ARCH-005`/`WORKSHOP-002` removed them.
- [`HARDEN-074`](../../tasks/archive/HARDEN-074-doc-link-checker-inline-code-labels.md):
  the strengthened link parser exposed real stale relative links.
- [`CI-007`](../../tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md):
  the retained cache policy depends on identity, statistics, and module
  invalidation tooling.
- [`BUG-087`](../../tasks/done/BUG-087-task-validator-documented-root-silent-noop.md),
  [`BUG-089`](../../tasks/done/BUG-089-root-hygiene-rejects-canonical-and-ignored-state.md),
  and [`BUG-011`](../../tasks/archive/BUG-011-ci-vulkan-workflow-allowlist.md)
  are tool defects, not protective findings.

## IntrinsicEngine Python Fleet

`CI/local wiring` names direct workflow use first and transitive or local use
second. `Warn` means report-only retention, not deletion.

| # | Tool | Purpose / category | CI/local wiring | Last protective finding or load-bearing evidence | Evidence type | Verdict |
|---:|---|---|---|---|---|---|
| 1 | `tools/agents/check_audit_cadence.py` | Process signal: reports recurring human-audit age. | `nightly-deep` report-only; imported by the session-brief generator; optional local `--strict`. | Direct run on 2026-07-17 reported the last agent-output audit at 2026-05-28, 50 days old and overdue; drift audit at 2026-06-06 remained within its 42-day limit. | Live report-only finding; deliberately non-gating. | **Warn** |
| 2 | `tools/agents/check_codex_config.py` | Contract gate: ensures `.codex/config.yaml` delegates policy and runs meaningful configure/build/test commands. | `ci-docs` strict. | No protective failure found in the window; it is the only executable check of the repository's Codex verification command required by `AGENTS.md`. | Unique contract gate; no recent finding. | **Keep** |
| 3 | `tools/agents/check_task_maturity_followups.py` | Contract gate: requires an Operational owner or explicit no-follow-up statement for ambiguous backend-facing `CPUContracted` closures. | Invoked transitively by `check_task_policy.py`; focused regression fixtures. | [`HARDEN-077`](../../tasks/archive/HARDEN-077-enforce-operational-followups-for-ambiguous-maturity.md) records positive and negative fixtures and makes this the executable maturity-closure rule. | Regression-backed unique contract gate. | **Keep** |
| 4 | `tools/agents/check_task_policy.py` | Aggregate task-system policy gate. | `ci-docs`, `ci-linux-clang`, and `pr-fast` strict; common local bundle. | Strict CI failed twice on 2026-06-26; it also exposed the duplicate `GEOM-027` resolved by `PROC-012` on 2026-06-27. | Recent protective finding. | **Keep** |
| 5 | `tools/agents/check_task_state_links.py` | Contract gate: reconciles task links and nearby lifecycle claims with actual task location. | `ci-docs` strict; focused regression suite. | `BUG-077` records seven stale retired-task links caught on 2026-07-10. | Recent protective finding. | **Keep** |
| 6 | `tools/agents/generate_session_brief.py` | Deterministic producer: derives the authoritative open/unblocked task view. | `ci-docs --check`; regenerated after task state changes; imports cadence dates. | The committed `tasks/SESSION-BRIEF.md` and its freshness gate are required session inputs established by [`PROC-004`](../../tasks/archive/PROC-004-task-front-matter-and-generated-session-brief.md). | Load-bearing generated-artifact producer. | **Keep** |
| 7 | `tools/agents/skills/intrinsicengine-research-ideation/scripts/validate_portfolio.py` | Skill-local structural validator and self-test for research-idea portfolios. | Not a repository CI gate; invoked by the skill and required by open `PROC-025` portability verification. | `--self-test` is the executable acceptance check for the shipped research-ideation skill and its planned cross-repository port. | Shipped utility with self-test and active consumer. | **Keep** |
| 8 | `tools/agents/sync_skills.py` | Deterministic producer/gate: mirrors canonical agent docs into all skill surfaces. | `ci-docs --check`; local `--write` after canonical doc changes. | `PROC-016` records missing mirrors and dead routing found on 2026-07-08; the generator repaired and now gates that drift. | Recent protective finding plus load-bearing producer. | **Keep** |
| 9 | `tools/agents/validate_method_manifests.py` | Schema gate for method IDs, paper/backend metadata, and referenced paths. | `ci-docs` strict; method authoring workflow. | No protective failure found in the window; every method package uses the manifest as the machine-readable method contract and the gate checks path existence. | Unique manifest contract gate. | **Keep** |
| 10 | `tools/agents/validate_tasks.py` | Structured task parser/validator: sections, front matter, IDs, dependencies, and completion metadata. | Called by `check_task_policy.py`; direct validator regression step in `ci-docs`. | `PROC-012` is a real duplicate-ID finding. `BUG-087` separately fixed the validator's 2026-07-16 zero-file false success and is not counted as protective evidence. | Recent protective finding; separately hardened tool. | **Keep** |
| 11 | `tools/analysis/compile_hotspots.py` | Build diagnostic and optional baseline gate over Ninja compile timings. | `ci-linux-clang` and `nightly-deep`; JSON artifact producer; active `BUILD-004` owns source-complete repair. | It produces the compile-edge evidence consumed by [`BUILD-004`](../../tasks/backlog/process/BUILD-004-source-complete-compile-hotspot-evidence.md); successful hosted runs on 2026-07-03 and 2026-07-06 prove live wiring, not a performance claim. | Load-bearing diagnostic producer with active repair owner. | **Keep** |
| 12 | `tools/analysis/module_fanout.py` | Diagnostic: reports import/include/export fan-out and can compare a historical baseline. | `nightly-deep` invokes report generation without `--fail-on-regression`; available for direct local analysis. | No protective finding found. The live nightly call is report-only and recent runs are often skipped by upstream failures; its Markdown artifact remains useful decomposition evidence. | Report-only diagnostic; no recent finding. | **Warn** |
| 13 | `tools/benchmark/validate_benchmark_manifests.py` | Schema gate for benchmark manifests and stable benchmark IDs. | `ci-docs` and `ci-bench-smoke` strict. | Benchmark discovery and invocation depend on valid manifests; this is the executable benchmark-workflow contract. | Load-bearing manifest contract gate. | **Keep** |
| 14 | `tools/benchmark/validate_benchmark_results.py` | Schema gate for metrics, diagnostics, and machine-readable benchmark results. | `ci-linux-clang`, `ci-bench-smoke`, `pr-fast`, `ci-sanitizers`, `ci-vulkan`, and `nightly-deep`; focused regression tests. | Timing aggregation and benchmark jobs cannot publish success-shaped malformed payloads without this validator. | Load-bearing result contract gate. | **Keep** |
| 15 | `tools/ci/aggregate_gate_timing.py` | Deterministic producer: combines phase timing and cache data into one gate result. | `pr-fast`, `ci-linux-clang`, `ci-vulkan`, `ci-bench-smoke`, and `ci-sanitizers`. | Five live workflows consume its output, which is then checked by the benchmark-result validator. | Load-bearing CI artifact producer. | **Keep** |
| 16 | `tools/ci/ccache_ci.py` | CI policy/producer: validates cache identity and mode, then exports fail-closed statistics. | `pr-fast`; workflow and unit regressions. | `CI-007` retained the PR-fast cache only after corrected identity, nonzero-hit, zero-error, and hosted evidence. | Load-bearing correctness guard and producer. | **Keep** |
| 17 | `tools/ci/ccache_module_invalidation_probe.py` | Hermetic C++23 module cache-invalidation proof. | `pr-fast`; focused regression suite. | `CI-007` used the probe on 2026-07-13 to prove interface edits invalidate affected importers and cached output matches a clean build. | Recent load-bearing correctness evidence. | **Keep** |
| 18 | `tools/ci/check_prerequisites.py` | Fail-fast guard for missing test binaries, inventories, and required paths. | `pr-fast`, `ci-linux-clang`, `ci-vulkan`, and `nightly-deep`; focused regressions. | Downstream CTest/benchmark steps use its explicit blocked state instead of reporting misleading test failures when producers did not run. | Load-bearing fail-fast guard. | **Keep** |
| 19 | `tools/ci/check_workflow_names.py` | Contract gate for workflow allowlist, displayed names, triggers, and readable YAML. | `ci-docs`; local repository policy checks. | No protective finding in the window. `BUG-011` was a stale allowlist false positive and is not credited; the current checker still uniquely enforces the canonical workflow set and trigger structure. | Unique contract gate; prior tool defect excluded. | **Keep** |
| 20 | `tools/ci/time_command.py` | Deterministic producer: streams a command and writes phase wall-clock metadata. | Six heavy/timed workflows: `pr-fast`, `ci-linux-clang`, `ci-vulkan`, `ci-bench-smoke`, `ci-sanitizers`, and `nightly-deep`. | Every gate-timing result depends on these per-phase records. | Load-bearing CI artifact producer. | **Keep** |
| 21 | `tools/ci/touched_scope.py` | Local planner/runner for conservative changed-path build, test, and structural checks. | Local only; `Test.TouchedScope.py`; open `CI-005` owns possible promotion. | Regression coverage proves policy and kernel-checker changes select their live guards; agents use it for documented local iteration. | Tested local utility with documented consumer. | **Keep** |
| 22 | `tools/ci/validate_gate_timing_baseline.py` | Schema/statistics validator for the historical CI-003 latency baseline. | `Test.CiTiming.py`; baseline maintenance, not a direct workflow step. | The published CI-003 baseline and p95/median calculations depend on deterministic validation before comparison. | Regression-backed evidence utility. | **Keep** |
| 23 | `tools/docs/check_doc_links.py` | Strict relative Markdown link validator. | `ci-docs` strict, `pr-fast`, and local structural bundles. | `HARDEN-074` records real stale links exposed when inline-code labels became visible; current task moves are continuously guarded. | Protective finding and live strict gate. | **Keep** |
| 24 | `tools/docs/check_docs_sync.py` | Diff-aware policy gate for coupled code/documentation changes. | `ci-docs` strict diff mode; touched-scope and local wrappers. | The gate evaluates five repository-owned sync rules in the current PROC-027 slice and blocks missing coupled docs; [`PROC-021`](../../tasks/archive/PROC-021-docs-sync-strict-mode-wiring.md) established fail-closed CI wiring. | Load-bearing diff contract gate. | **Keep** |
| 25 | `tools/repo/build_knowledge_graph.py` | Optional deterministic producer merging module and method/paper graphs. | Local and `provision_knowledge_graph.sh`; no merge gate. | It is the documented no-key producer for the `.mcp.json` knowledge-graph discovery aid and consumes both graph adapters. | Load-bearing optional product utility. | **Keep** |
| 26 | `tools/repo/check_expected_top_level.py` | Compatibility entry point delegating to canonical root hygiene. | No workflow caller; compatibility/regression use only. | `BUG-089` explicitly retained this thin entry point on 2026-07-16 while eliminating duplicate scans. Immediate removal would reverse that documented compatibility decision without consumer evidence. | Time-local compatibility commitment; tool defect excluded. | **Keep** |
| 27 | `tools/repo/check_kernel_convergence.py` | Exact no-backsliding ratchet for the `Runtime.Engine` kernel surface. | `pr-fast` strict; touched-scope and focused regression suites. | Commit `109af4bd` restored the Engine convergence budget after live drift on 2026-07-16; the exact snapshot and open debt owner now fail closed. | Recent protective ratchet plus regressions. | **Keep** |
| 28 | `tools/repo/check_layering.py` | Architecture gate over includes, C++23 imports, and CMake link edges. | `ci-linux-clang` and `pr-fast` strict; clean-workshop bundle and focused regressions. | `WORKSHOP-001` exposed real RHI/platform module and CMake violations; their removal restored the strict gate. | Protective finding and core architecture gate. | **Keep** |
| 29 | `tools/repo/check_layering_allowlist_quality.py` | Contract gate for exception metadata, scope, uniqueness, and open owners. | `ci-docs` strict; clean-workshop bundle. | Legacy exception ownership required targeted rebinding under `HARDEN-082`; the current empty allowlist remains fail-closed against unowned future exceptions. | Historical protective policy evidence plus unique gate. | **Keep** |
| 30 | `tools/repo/check_pr_contract.py` | Static PR-template heading checker plus success-only local advice. | At snapshot: `ci-docs` CI mode and generic touched-scope local smoke. | No protective finding located. CI inspected the checked-in template, not submitted PR bodies; local mode printed advice and returned success. The template itself remains the human review surface. | No unique evidence. | **Retire** |
| 31 | `tools/repo/check_root_hygiene.py` | Canonical root Markdown and top-level repository policy gate. | `ci-docs` strict; local repo-hygiene wrapper and focused regressions. | `BUG-089` was a checker false positive, not a protective finding. The repaired checker now has isolated negative fixtures proving unowned roots and Markdown fail while named local state passes. | Regression-backed unique contract gate; tool defect excluded. | **Keep** |
| 32 | `tools/repo/check_shader_outputs.py` | Fail-fast utility requiring expected SPIR-V outputs after shader compilation. | Build/task-local use; not a workflow gate. | Shader compilation consumers need an explicit failure for an empty or partial output tree rather than a success-shaped build. | Load-bearing fail-fast utility. | **Keep** |
| 33 | `tools/repo/check_test_layout.py` | Strict taxonomy-owned test source layout gate. | `ci-docs` strict; structural bundles. | It preserves the completed HARDEN-041/042 directory migration and rejects reintroduced legacy wrapper test roots; focused policy fixtures established by `HARDEN-043`. | Regression-backed repository contract gate. | **Keep** |
| 34 | `tools/repo/export_method_graph.py` | Optional producer for paper-to-method-to-code graph nodes and edges. | Called by `build_knowledge_graph.py`; local only. | The merged knowledge graph's research chain is absent without this adapter; it validates referenced method/paper/code paths while building. | Load-bearing optional producer. | **Keep** |
| 35 | `tools/repo/export_module_graph.py` | Optional producer for C++23 module graph and layer-tagged edges. | Called by `build_knowledge_graph.py`; local only. | The graph aid depends on its module parser because generic tree-sitter extraction does not understand this repository's C++23 module surface. | Load-bearing optional producer. | **Keep** |
| 36 | `tools/repo/generate_module_inventory.py` | Deterministic producer for the committed module inventory. | `ci-docs` regeneration/diff gate; local after module changes. | Hosted freshness steps failed on 2026-07-01 and multiple 2026-06-27 runs when the committed inventory was stale. | Recent protective finding plus generated-artifact producer. | **Keep** |

## AgentKit Python Product

AgentKit's 21 Python files are listed individually because they are tracked
under `tools/`, but their evidence is evaluated as one separately shipped,
zero-dependency product. They are not IntrinsicEngine CI gate duplicates.
`tools/agentkit/selftest.sh` scaffolds default and custom-contract throwaway
repositories, rejects unrendered placeholders, runs both the vendored and
package check runners in strict mode, exercises `doctor` and `new-task`, and
checks custom skill mirroring. The last implementation repair was commit
`8135697b` on 2026-06-04.

| # | Tool | Purpose / category | CI/local wiring | Last protective finding or load-bearing evidence | Evidence type | Verdict |
|---:|---|---|---|---|---|---|
| 37 | `tools/agentkit/agentkit.py` | Zero-install launcher that adds `src/` and dispatches the CLI. | Direct quickstart and AgentKit self-test; no IntrinsicEngine merge gate. | Both self-test repositories are created through this launcher. | Product evidence: directly exercised. | **Keep** |
| 38 | `tools/agentkit/src/agentkit/__init__.py` | Package identity and version surface. | Imported by the launcher/package entry points. | Every launcher/self-test invocation imports the package; packaging exposes its version. | Product evidence: load-bearing package module. | **Keep** |
| 39 | `tools/agentkit/src/agentkit/__main__.py` | `python -m agentkit` entry point. | Installed/local module invocation; not an IntrinsicEngine gate. | This is the documented standard Python module entry surface and delegates to the same tested CLI. | Product evidence: public entry point. | **Keep** |
| 40 | `tools/agentkit/src/agentkit/bootstrap.py` | Generator for config, contracts, docs, skills, tasks, checks, and workflows. | `agentkit init`; AgentKit self-test. | The self-test performs two complete initializations and verifies their generated trees. | Product evidence: directly exercised. | **Keep** |
| 41 | `tools/agentkit/src/agentkit/check_runner.py` | Package runner aggregating generated validators. | `agentkit check`; AgentKit self-test. | The self-test runs the package aggregate strict before and after creating a task. | Product evidence: directly exercised. | **Keep** |
| 42 | `tools/agentkit/src/agentkit/cli.py` | Argument parsing and dispatch for all five commands. | Console script, launcher, and module entry point; AgentKit self-test. | The self-test exercises `init`, `check`, `doctor`, and `new-task`; `resync` shares this dispatcher. | Product evidence: directly exercised. | **Keep** |
| 43 | `tools/agentkit/src/agentkit/config.py` | Config defaults/loaders and render context for `agentkit.toml`. | Used by generator, runner, doctor, resync, and CLI. | Default and custom contract configurations both pass the self-test, including custom mirror paths. | Product evidence: directly exercised. | **Keep** |
| 44 | `tools/agentkit/src/agentkit/doctor.py` | Reports missing, present, and drifted generated workflow files. | `agentkit doctor`; AgentKit self-test. | The self-test requires `missing: 0` after generation. | Product evidence: directly exercised. | **Keep** |
| 45 | `tools/agentkit/src/agentkit/newtask.py` | Creates slugged backlog tasks from shipped templates. | `agentkit new-task`; AgentKit self-test. | The self-test creates `FEAT-001` and reruns the strict aggregate successfully. | Product evidence: directly exercised. | **Keep** |
| 46 | `tools/agentkit/src/agentkit/render.py` | Minimal dependency-free placeholder renderer. | Used throughout `bootstrap.py`; AgentKit self-test. | Both generated repositories are recursively checked for unrendered placeholders. | Product evidence: directly exercised. | **Keep** |
| 47 | `tools/agentkit/src/agentkit/resync.py` | Re-mirrors canonical generated docs into skill references. | `agentkit resync`; custom-contract generation verifies its mapping inputs. | Custom-contract self-test proves the configured mirror destination; resync is an advertised product command sharing that map. | Product evidence: shipped command, indirect coverage. | **Keep** |
| 48 | `tools/agentkit/src/agentkit/templates/tools/agent/check.py` | Vendored no-install aggregate runner emitted into target repositories. | Generated local preview and generated `ci-docs`; AgentKit self-test. | The self-test executes the generated runner strict in both throwaway repositories. | Product evidence: generated output directly exercised. | **Keep** |
| 49 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/_common.py` | Shared config, file iteration, section parsing, exit codes, and reporting. | Imported by all generated checks. | Strict self-test execution loads it through every aggregate validator. | Product evidence: generated dependency directly exercised. | **Keep** |
| 50 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_agent_config.py` | Generated gate ensuring agent surfaces point to one contract. | Generated aggregate and `ci-docs`; AgentKit self-test. | Both default `AGENTS.md` and custom `CONTRACT.md` repositories pass strict validation. | Product evidence: generated check directly exercised. | **Keep** |
| 51 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_doc_links.py` | Generated relative/root-relative Markdown link gate. | Generated aggregate and `ci-docs`; AgentKit self-test. | Both freshly generated repository trees pass the strict link check. | Product evidence: generated check directly exercised. | **Keep** |
| 52 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_docs_sync.py` | Generated config-driven changed-path/docs coupling gate. | Generated aggregate and `ci-docs`; AgentKit self-test. | Commit `8135697b` added it to local checking so the package runner matches generated CI; strict self-test covers the aggregate. | Product evidence: shipped parity repair and direct aggregate coverage. | **Keep** |
| 53 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_pr_contract.py` | Generated config-driven PR-template section gate for target repositories. | Generated aggregate and generated `ci-docs`; AgentKit self-test. | Both generated templates pass against their own `[pr].required_sections`. Unlike the retired IntrinsicEngine checker, this is part of a portable configurable scaffold. | Product evidence: generated check directly exercised. | **Keep** |
| 54 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_prereqs.py` | Generated three-state artifact prerequisite guard. | Copied into target repositories, but no generated runner or workflow invokes it. | No direct caller, negative self-test, or configured artifact path exists; the documented `BLOCKED` state is unused scaffold. | No unique evidence. | **Retire** |
| 55 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_root_hygiene.py` | Generated root Markdown hygiene gate. | Generated aggregate and `ci-docs`; AgentKit self-test. | Default and custom contract filenames both pass strict root policy, which was a repaired 2026-06-04 product case. | Product evidence: generated check directly exercised. | **Keep** |
| 56 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_tasks.py` | Generated task structure, lifecycle, and duplicate-ID gate. | Generated aggregate and `ci-docs`; AgentKit self-test. | Strict validation passes before and after `FEAT-001` is created by the self-test. | Product evidence: generated check directly exercised. | **Keep** |
| 57 | `tools/agentkit/src/agentkit/templates/tools/agent/checks/check_workflow_names.py` | Generated workflow allowlist, name, and trigger gate. | Generated aggregate and `ci-docs`; AgentKit self-test. | Both generated workflow sets pass their config-derived strict policy. | Product evidence: generated check directly exercised. | **Keep** |

## Non-Python Appendix

These **13 shell scripts** and **3 CMake scripts** are listed so Python-centric
scope cannot hide validator or producer wiring. They receive no
keep/warn/retire verdict in this audit.

| Script | Purpose / wiring |
|---|---|
| `tools/agentkit/selftest.sh` | Local end-to-end AgentKit product smoke test over two throwaway repositories. |
| `tools/agents/check_todo_active_only.sh` | Compatibility wrapper executing strict task policy. |
| `tools/agents/resync_skills.sh` | Compatibility/local wrapper for `sync_skills.py --write`. |
| `tools/benchmark/check_perf_regression.sh` | Threshold comparison for benchmark JSON results; benchmark-task/local use. |
| `tools/check_ui_contract_guard.sh` | Historical compatibility wrapper for the canonical repository UI guard. |
| `tools/ci/run_clean_workshop_review.sh` | Local architecture-review bundle over layering, allowlist, tasks, and links. |
| `tools/ci/run_repo_hygiene_checks.sh` | Local warning-mode root-hygiene and documentation-link bundle. |
| `tools/repo/check_ui_contract_guard.sh` | Canonical source-pattern UI boundary guard used by UI contract verification. |
| `tools/setup/agent_session_setup.sh` | Shared session provisioning and optional build setup entry point. |
| `tools/setup/bootstrap_vcpkg.sh` | Repository-local vcpkg bootstrap guarded by the egress preflight. |
| `tools/setup/provision_knowledge_graph.sh` | Optional graphify installation and deterministic knowledge-graph rebuild. |
| `tools/setup/vcpkg_preflight.sh` | Network/egress diagnosis producer for vcpkg setup. |
| `tools/setup/wait_for_agent_setup.sh` | Synchronizes consumers with asynchronous session setup/toolchain readiness. |
| `tools/diagnostics/validate_frame_pacing_capture.cmake` | Validates frame-pacing capture payloads in CMake-driven diagnostics. |
| `tools/vcpkg/overlay-ports/imgui/portfile.cmake` | vcpkg overlay recipe for the repository's ImGui dependency. |
| `tools/vcpkg/overlay-ports/xatlas/portfile.cmake` | vcpkg overlay recipe for the repository's xatlas dependency. |

## Application Constraints

- Retiring the repository PR-contract checker requires removing its `ci-docs`
  step, touched-scope command, current tooling/contract documentation, and
  open-task verification invocations. Historical task and retirement-log
  command evidence remains historical.
- The AgentKit generated PR-contract checker remains intact because AgentKit is
  a separate configurable product with its own generated workflows and
  self-test.
- AgentKit's unused prerequisite checker, its copied-file registration, and
  its `BLOCKED` documentation are removed together; a concrete producer /
  consumer workflow plus a negative product test is the reintroduction trigger.
- `module_fanout.py` remains report-only in nightly CI; enabling
  `--fail-on-regression` would be a new gate requiring a current baseline and a
  separately owned tightening task.
- `check_audit_cadence.py` remains report-only in nightly CI. Its `--strict`
  option is a local reviewer tool, not a PR gate.
- `check_expected_top_level.py` remains only for the compatibility commitment
  recorded by `BUG-089`; future removal needs evidence that no external caller
  relies on the old entry point.
