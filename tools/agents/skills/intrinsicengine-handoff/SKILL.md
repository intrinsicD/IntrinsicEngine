---
name: intrinsicengine-handoff
description: Compact the current conversation into a handoff document so another agent (or this agent in a fresh session) can continue the work without re-reading the whole thread. Output goes to the OS temporary directory, never into the repo. Use when the user says "handoff", "compact this", "summarize for next session", or when context is about to be summarized and the next agent needs a clean starting point.
argument-hint: "What will the next session focus on?"
---

# IntrinsicEngine Handoff

Write a handoff document summarising the current conversation so a fresh agent
can pick up. Save to the OS temporary directory (`$TMPDIR`, falling back to
`/tmp` on Linux, `%TEMP%` on Windows). Name it
`<tmpdir>/intrinsicengine-handoff-<timestamp>.md` and tell the user the
absolute path.

**Never write handoff documents into the repository.** Not into
`tasks/active/`, not into `tasks/done/`, not into `docs/`, not into a stray
`NOTES.md` at the repo root. The agent-output audit (`intrinsicengine-review`
row 7, "ceremony without shipped value") will flag any in-repo planning doc
that is not a task file. Handoff lives outside the tracked tree.

## What to include

Aim for ≤ 400 lines. Be specific. The next agent must be able to start cold
without scrolling back.

1. **Branch and working tree state**
   - Current branch name.
   - Output of `git status --short --branch`.
   - Output of `git log --oneline -5`.
   - Any uncommitted changes worth flagging.

2. **Task context** (do NOT duplicate the task file — link it)
   - The `tasks/active/<TASK-ID>.md` file (or `tasks/backlog/...`) that
     owns this work, by path.
   - Which `Required changes` checkboxes are now `- [x]` and which remain.
   - The current slice within the slice plan, if there is one.

3. **What was done in this session**
   - Concrete files edited/created (paths).
   - Commits made (subject lines).
   - Tests run and result (ctest label, pattern, pass/fail count).

4. **What is in-flight**
   - The exact next action — file, function, test name.
   - Any half-applied edit that would confuse a fresh agent.
   - Open questions that block progress.

5. **What did NOT work and why** (so the next agent doesn't redo it)
   - Hypotheses tried and ruled out, with the evidence that ruled them out.
   - Approaches abandoned, with the reason (don't make the next agent
     rediscover that approach X violates layering invariants).

6. **References** — link, don't copy
   - PRDs, plans, ADRs, GitHub issues, prior commits, prior diagnose
     post-mortems. Reference by path or URL. Do not inline their content.

7. **Suggested skills for the next session**
   - One or more of: `intrinsicengine-core`, `intrinsicengine-task-workflow`,
     `intrinsicengine-review`, `intrinsicengine-method`,
     `intrinsicengine-benchmark`, `intrinsicengine-docs-sync`,
     `intrinsicengine-diagnose`, `intrinsicengine-zoom-out`.
   - Pick the ones that match the touched scope; don't list all of them.

## Rules

- **Redact any sensitive information** — API keys, passwords, PII, internal
  hostnames. Most IntrinsicEngine sessions don't surface secrets, but check
  before writing.
- **No duplication.** If a PRD, ADR, task file, issue, or commit already
  captures something, link to it by path or URL. The handoff is glue, not a
  re-copy.
- **No speculation as fact.** If you're guessing why something failed, label
  it as a hypothesis. The next agent will trust you and may not re-test.
- **Treat the document as throwaway.** It exists for one purpose: bridge one
  session boundary. The next agent should be able to delete it after reading.

## If the user passed arguments

Treat them as a description of what the next session will focus on, and tilt
the document toward that focus — drop or compress sections that aren't
relevant to the named next-session goal.
