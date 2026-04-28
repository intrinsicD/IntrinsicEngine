#!/usr/bin/env python3
"""Compatibility wrapper retained during tools path migration (RORG-071)."""

from pathlib import Path
import runpy
import sys

TARGET = Path(__file__).resolve().parent / "repo" / "generate_module_inventory.py"
sys.argv[0] = str(TARGET)
runpy.run_path(str(TARGET), run_name="__main__")
