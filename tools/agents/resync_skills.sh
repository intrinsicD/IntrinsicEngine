#!/bin/bash
# Thin wrapper kept for muscle memory; the sync logic lives in sync_skills.py.
exec python3 "$(dirname "$0")/sync_skills.py" --write "$@"
