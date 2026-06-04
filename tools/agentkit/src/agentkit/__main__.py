"""Enable ``python -m agentkit`` when ``src/`` is on the path."""
from __future__ import annotations

import sys

from agentkit.cli import main

if __name__ == "__main__":
    sys.exit(main())
