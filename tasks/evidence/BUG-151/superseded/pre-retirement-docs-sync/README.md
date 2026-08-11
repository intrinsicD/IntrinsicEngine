# Superseded pre-retirement docs-sync receipt

The first strict docs-sync receipt ran while the BUG-151 retirement files were
still uncommitted. `check_docs_sync.py --diff-mode` intentionally evaluates a
committed Git diff, so it saw the tool change but not the pending task and
canonical documentation surface and exited 1. The retirement surface was then
committed at `2d13da5c`; the canonical `docs-sync` receipt in `commands/` was
rerun against that exact surface. The failed receipt and its raw logs remain
here as sequencing history and are not completion evidence.
