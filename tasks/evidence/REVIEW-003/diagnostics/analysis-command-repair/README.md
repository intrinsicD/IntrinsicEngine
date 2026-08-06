# Right-sizing count probe repair

The first `right-sizing-surface-counts` analysis receipt exited 1 because the
shell ran with `pipefail` and treated an expected zero-match `rg` test-reference
query as an error before `wc -l` could report zero. The repaired command wraps
that query with `|| true`; it changes no repository data and is the required
receipt retained under `commands/`.

The first `agent-window-largest-commits` probe likewise exited 141 because
`head` closed a `sort` pipeline under `pipefail`. The replacement uses `sed`
to retain the same first-40 result without terminating its producer early.

The first successful `agent-window-added-comment-sample` probe included
preprocessor directives because its broad line-prefix expression treated `#`
as a comment marker. The replacement excludes directives and limits its sample
to production sources; the misleading receipt is diagnostic-only here.
