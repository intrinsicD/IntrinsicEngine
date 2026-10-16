---
id: HARDEN-091
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive delegated implementation; evidence is the reviewed diff, discriminating tests, and CI
contract_schema: 1
contracts:
  - repo.source-documentation
  - io.geometry-format-capabilities
  - geometry.element-domain-sources
  - geometry.property-coherence
  - runtime.editor-prepared-frame-locality
contract_review: Runtime export and readiness consume canonical properties and editor command frames; retain the existing coherence and compilation boundaries. No parameterization solver or layer policy changes.
---
# HARDEN-091 — Complete the src workflow follow-up

## Goal
1. “sollten wir diese aufräumen? anders organisieren?”
2. “hat die implementierung einen durchgenden stil?”
3. “gibt es offene punkte, bugs, implementierungs gaps?”
4. “ich hätte gerne dass du alle diese punkte umsetzt. lass subagenten parallel arbeiten wo es geht. orchestriere die subagenten so dass alles abgearbeitet werden kann. vermeide wiederholte tests und compilierung, aber stelle sicher dass am ende alles was hier angefasst wurde korrekt funktioniert.”

Serve Framework24 workflow convergence by fixing the current UI/agent paths,
then completing geometry output and bounded maintenance. The operator approved
Save/Discard/Cancel for Sandbox New/Open/Close and PROC-036 option 2. Native
window-close policy, additional agent confirmation, broad directory changes,
global socket deadlines and general atomic-file-writing infrastructure are outside
this slice. Existing HARDEN-090 evidence remainders retain their owners.

## Acceptance criteria
- [x] Q3/4: All existing Undo/Redo buttons report failures; agent replies distinguish empty history, failure and partial success.
- [x] Q3/4: UI-037 parameterization readiness and Apply share prerequisites for the actual UI draft / resolved agent config.
- [x] Q3/4: Dirty-scene New/Open/Close waits for explicit discard or successful matching save; cancellation, failed save and newer edits preserve the document.
- [x] Q3/4: Geometry text writers preserve finite float values across small/large magnitudes and locale; existing reject-before-write contracts remain intact.
- [x] Q3/4: UI-046 exports supported selected mesh/graph/point-cloud geometry through Runtime and the existing writers; unsupported formats and dropped properties are reported.
- [x] Q3/4: Process waiting handles interrupted/error waits; agent sending stops after first failure and observes shutdown between pending replies.
- [x] Q1/2/4: RHI operational-status documentation is accurate; the unused Vulkan migration stub and stale references are removed; geometry/graphics/physics have concise entry points.
- [ ] Q2/4: PROC-036 option 2 is implemented with a validated formatter and separate token-equivalent family commits, preserving semantic fixes separately.
- [x] Q4: New behavior has baseline or mechanism-removal failures; integrated affected suites and structural checks pass; independent review binds the final source.

## Execution
Source baseline: `ffe7e83728b1e37cac926bacaa6e8364b49b2526`.
Root owns the primary checkout and its existing `build/ci`; three isolated
source worktrees own editor workflows, processing/export, and foundational fixes.
Agents supply test-only and production patches. Root batches old-code RED,
integrated GREEN and bounded review corrections. No parallel writers/builders
share a checkout. Formatting runs after semantic integration in separate commits.
The baseline full-CPU CI is red (run 38960072158); BUG-246 records its diagnosis.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure --no-tests=error -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j 8
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -j 8 -R '^(SceneEditingGeometryExport|RuntimeReuseDriftGuard|SandboxDomainPanels|SandboxEditorPresentation|ParameterizationOperations)\.'
ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -j 8 -R '^(SandboxEditorGizmo|SandboxEditorPresentation\.SceneFilePendingQueryObservesRuntimeJobAndAttachmentLifetime)'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref ffe7e8372 --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/sync_skills.py --check
```
The full CPU run is required by UI-046 and the cross-cutting public surfaces;
source-only corrections after that run use focused verification. Socket tests
run outside the filesystem sandbox. Before committing, docs-sync receives the
explicit changed-path list because diff-mode compares commits.
Formatter validation compares tokens, comments/literals and preprocessing,
and checks idempotence with clang-format 20.1.8. The formatter's MaterialSystem
sample churn and long preserved literals await the operator's final decision.

## Current state
Independent reviewer `/root/final_review` accepted criteria 1–7 and 9 on
2026-10-17, bound to source packet
`2212831a85b91cda6694afa481d92831c5d30f19fd89744f0ae10e5fff61ed79`.
Implementation: `761040df0`. The canonical verification and completion record is
[in the retirement log](../done/RETIREMENT-LOG.md#2026-10-17--src-workflow-follow-up).

Only PROC-036's formatter-limit decision, separate family commits and blame-ignore
list remain. Keep this task and its raw evidence active until those are resolved.
