#!/usr/bin/env python3
"""Zero-dependency launcher for agentkit.

Run directly without installing anything:

    python3 tools/agentkit/agentkit.py <command> [options]

Or install it (optional) and use the `agentkit` console command:

    pip install ./tools/agentkit
    agentkit <command> [options]

This launcher only puts the bundled ``src/`` package directory on ``sys.path``
and hands off to :func:`agentkit.cli.main`. It deliberately imports nothing
beyond the standard library.
"""
from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SRC = os.path.join(_HERE, "src")
if _SRC not in sys.path:
    sys.path.insert(0, _SRC)

from agentkit.cli import main  # noqa: E402  (path set up above)

if __name__ == "__main__":
    sys.exit(main())
