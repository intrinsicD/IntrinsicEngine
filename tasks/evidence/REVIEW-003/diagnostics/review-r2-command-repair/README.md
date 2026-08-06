# REVIEW-003 revision-2 command repair

The first `review-r2-structural-gates` receipt stopped after its initial passing
checks because it used the obsolete `validate_tasks.py tasks --require-tasks`
invocation. The current validator accepts `--root tasks --strict`.

The failed receipt and logs are retained here as diagnostic provenance. The
required `commands/review-r2-structural-gates.json` receipt uses the current
invocation and must pass before revision 2 is reviewed.
