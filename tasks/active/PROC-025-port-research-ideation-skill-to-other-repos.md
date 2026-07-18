---
id: PROC-025
theme: H
depends_on: []
---
# PROC-025 — Port the research-ideation skill to structsplat and prospect at IntrinsicEngine-parity quality

## Goal
- Land the `transformational-research-skill-kit` research-ideation skill in the
  `structsplat` and `prospect` repositories at the same quality bar reached in
  IntrinsicEngine: repo-native placement and registration, an **accurate**
  rewritten `repository-context.md`, provenance/license, and a passing
  structural-gate verification — not a generic drop-in.

## Non-goals
- No re-installation in IntrinsicEngine; that landed at commit `94691f1`
  (`tools/agents/skills/intrinsicengine-research-ideation/`) and is the exemplar.
- No change to the kit's methodology (the SKILL.md workflow, novelty taxonomy,
  lanes, prior-art audit, and killing-experiment discipline are good as-is).
- Do **not** use `install.py`'s `AGENTS.md` auto-append, and do not create an
  unexpected top-level directory that trips the target repo's hygiene checks.

## Context
- Status: active on 2026-07-18; owner: Codex; IntrinsicEngine branch: `main`;
  target branches: `proc/025-research-ideation-refresh`. Next verification:
  finish the two preserved repository-context refreshes, run each repository's
  focused structural gates, then commit and push each designated branch.
- The earlier remote-scoped session could not reach `structsplat` or
  `prospect`. Local intake now confirms that both full skills already landed
  on `origin/main`, so this task must refresh and verify them rather than
  reinstalling or duplicating their registration.
- StructSplat's original landing is
  `ff88a119ce8257edfccc037ac9f98c70168d30de` at
  `.claude/skills/structsplat-research-ideation/`, exposed through the native
  `.agents/skills/` symlink. Its dedicated worktree
  `/tmp/structsplat-proc025` preserves an uncommitted post-`BENCH-007`
  frontier refresh; the dirty primary worktree is out of scope.
- Prospect's original landing is
  `afcf8f304433a96fd044eff63a25e71ac511112c` at
  `.agents/skills/prospect-research-ideation/`. Its dedicated worktree
  `/tmp/prospect-proc025` preserves the explicit license/author/version edit
  and must fast-forward to clean local `main` at
  `24a8147bb15b22949c58c00aa2191fbef3692ba0` before the final WM-001 context
  refresh. The primary worktree and its six owner commits are otherwise out
  of scope.
- The kit ships profiles for both repos (`repo-profiles/structsplat.md`,
  `repo-profiles/prospect.md`) and an `install.py` that auto-detects them, but
  the bundled profiles are **starting hypotheses with real errors** — the
  IntrinsicEngine profile claimed "C++20" and "Vulkan/OpenGL/DirectX" when the
  repo is C++23 / Vulkan-only. Treat every profile claim as unverified.

### Portable playbook (how to reach IntrinsicEngine-parity quality)

Follow these steps in the target repo; they are exactly what produced `94691f1`:

1. **Get the kit** (`transformational-research-skill-kit` v1.0.0; MIT, first-party
   by the repo owner). Extract to a scratch dir, not the repo.
2. **Discover the target repo's native skill-surface convention before placing
   anything.** IntrinsicEngine uses `tools/agents/skills/<name>/` with
   `.claude/skills` + `.codex/skills` symlinks, an `intrinsicengine-core` routing
   table, and a skills `README.md` with explicit skill/discipline counts. Another
   repo may use a plain `.agents/skills/`, `~/.claude/skills`, or a different
   index. Match the repo; do not assume the kit's default `.agents/skills/` layout.
3. **Place the skill at the repo-native location with a repo-prefixed name**
   (e.g. `<repo>-research-ideation`). Copy the hand-authored `references/`,
   `assets/`, `evals/`, and `scripts/validate_portfolio.py`. Drop the Codex-only
   `agents/openai.yaml` launcher unless the repo wants it.
4. **Rewrite `references/repository-context.md` from scratch against the actual
   tree** — the highest-leverage step. Capture the real toolchain (language
   standard, compiler, build system, GPU/graphics API), the real
   module/layer/dependency structure, the method/benchmark/parity or
   reference-vs-optimized contract if one exists, the genuine research frontier
   (open tasks/issues), the repo's invariants, and where a selected idea goes
   (issue/task/ADR/benchmark). Verify every claim against files, not the bundled
   profile.
5. **Adapt `SKILL.md`:** set the frontmatter `name` to the repo-prefixed name;
   keep `license: MIT` + author; add a short provenance note and a "repository
   fit" note that routes a selected candidate into *that repo's*
   implementation/issue/task/ADR/benchmark workflow. Keep the honesty spine
   (never claim absolute novelty; ideation only; never fabricate prior art or
   results).
6. **Register the skill the repo-native way** — routing table / skills index /
   README, plus any skill-count or discipline-count fields — instead of running
   the installer's `AGENTS.md` merge. If the repo genuinely wants the AGENTS
   snippet, place it by hand and fix its `.agents/skills/...` path to the real
   location.
7. **Provenance/license:** note MIT + author in the `SKILL.md` and the skills
   index. Only add a `THIRD_PARTY_LICENSES` entry if the repo treats
   owner-authored skills as third-party (for the owner it is first-party).
8. **Verify with the repo's structural gates:** documentation-link check,
   skill-mirror `--check` if the repo has one, task/policy checks, root/top-level
   hygiene (confirm no unexpected new top-level entry), and
   `python3 .../scripts/validate_portfolio.py --self-test` plus `py_compile`.
9. **Commit + push on the repo's designated branch** with a message that lists
   the adaptation deltas and the verification actually run.

## Required changes
- [ ] Adapt and land the research-ideation skill in `structsplat` following the
      playbook (repo-native placement, accurate `repository-context.md`,
      registration, provenance, verification).
- [ ] Adapt and land the research-ideation skill in `prospect` following the
      playbook.
- [ ] In each target repo, record the landing commit and note any repo-specific
      deviations from the IntrinsicEngine exemplar.

## Tests
- [ ] In each target repo, its documentation-link / skill-sync / task-policy /
      hygiene gates pass, and `validate_portfolio.py --self-test` passes.

## Docs
- [ ] Each target repo's skills index/README reflects the new skill and any
      count fields.

## Acceptance criteria
- [ ] `structsplat` and `prospect` each carry the research-ideation skill at the
      IntrinsicEngine quality bar: repo-native location + name, an accurate
      (verified-against-the-tree) `repository-context.md`, correct registration,
      provenance/license, and passing structural gates.
- [ ] No `AGENTS.md` auto-append and no unexpected top-level directory in either
      repo.

## Verification
```bash
# Run inside each target repo after adaptation (adjust to that repo's tooling):
python3 <skill>/scripts/validate_portfolio.py --self-test
# plus that repo's doc-link, skill-sync (--check), task-policy, and hygiene gates.
```

## Forbidden changes
- Using `install.py` with the `AGENTS.md` merge, or the generic `.agents/skills/`
  layout when the target repo has its own skill-surface convention.
- Shipping the kit's bundled profile unverified as `repository-context.md`.
- Copying IntrinsicEngine's `intrinsicengine-*`-named/`tools/agents/skills/`
  placement into a repo that does not use that convention.
- Modifying the target repo's production/research code as part of installing an
  ideation skill.
