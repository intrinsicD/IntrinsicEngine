#!/usr/bin/env python3
"""Compatibility entrypoint for the canonical root-hygiene checker."""

from check_root_hygiene import main


if __name__ == "__main__":
    raise SystemExit(main())
