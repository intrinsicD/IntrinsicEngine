# Task Archive

Frozen history: retired tasks swept out of [`tasks/done/`](../done/README.md)
to keep the working set (`active/`, `backlog/`, recent `done/`) small. Nothing
here is deleted knowledge — these files, plus the append-only
[`RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md) and the git history, are the
record of the agentic development of this repository.

## Rules

- **Archived tasks are read-only history.** Do not edit, reopen, or re-gate
  them. Follow-up work gets a new task with a new ID.
- **IDs stay authoritative.** Archived IDs participate in duplicate-ID
  detection and resolve `depends_on` references (`validate_tasks.py` /
  `generate_session_brief.py` treat archive IDs as done), so an archived ID
  can never be reallocated and retired dependencies keep unblocking backlog
  tasks.
- **No format validation.** Per-file format rules may evolve past frozen
  history; archived files are exempt from `validate_tasks.py` format checks.

## Sweep policy

Retirement is unchanged: completed tasks retire to `tasks/done/` with
completion date and commit/PR reference, and the narrative is appended to
`tasks/done/RETIREMENT-LOG.md`. Periodically (or when `tasks/done/` grows
noisy), sweep retired task files here with `git mv` and rewrite inbound links
(`tools/docs/check_doc_links.py` must stay clean). The initial sweep moved all
661 tasks retired up to 2026-07-14.
