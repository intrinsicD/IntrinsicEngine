# Deferred hints

The single append-only ledger for improvement-tier hints the operator deferred
(`docs/agent/prompt/prompt.md` §"Deferred-hint ledger"). One file for the whole
repository — never one file per hint, never parallel note trees.

Entry format:

    - [ ] YYYY-MM-DD <area> — <one-line hint> (<file or module>)

Hygiene (applied by audit sweeps, not CI):

- Hints are filed only with operator consent, never automatically.
- Resolved or obsolete entries are deleted, not checked off — git history is
  the archive.
- An entry older than 30 days is promoted to a real task file or dropped at
  the next audit sweep.
- Keep this file under ~100 lines; past that, triage oldest first.

## Open hints

(none)
