#!/usr/bin/env python3
from __future__ import annotations
import argparse
import pathlib
import re
import sys
from datetime import date

MODULE_RE = re.compile(r"^\s*export\s+module\s+([^;\s]+)\s*;", re.MULTILINE)


def collect_modules(root: pathlib.Path):
    rows = []
    for p in sorted(root.rglob('*.cppm')):
        text = p.read_text(encoding='utf-8')
        m = MODULE_RE.search(text)
        if not m:
            continue
        module = m.group(1)
        library = p.parts[1] if len(p.parts) > 1 else "(root)"
        rows.append((module, str(p).replace('\\', '/'), library))
    return rows


def render(rows):
    lines = []
    lines.append('# src_new Module Inventory (auto-generated)')
    lines.append('')
    lines.append(f'_Generated on {date.today().isoformat()} by `tools/generate_src_new_module_inventory.py`._')
    lines.append('')
    lines.append('| Module | File | Library |')
    lines.append('|---|---|---|')
    for mod, file, lib in rows:
        lines.append(f'| `{mod}` | `{file}` | `{lib}` |')
    lines.append('')
    lines.append(f'Total modules: **{len(rows)}**')
    lines.append('')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default='src_new')
    ap.add_argument('--out', default='docs/architecture/src_new_module_inventory.md')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()

    rows = collect_modules(pathlib.Path(args.root))
    content = render(rows)
    out = pathlib.Path(args.out)

    if args.check:
        if not out.exists():
            print(f'missing inventory file: {out}', file=sys.stderr)
            return 1
        existing = out.read_text(encoding='utf-8')
        if existing != content:
            print(f'inventory drift detected: {out}', file=sys.stderr)
            return 2
        print(f'inventory up-to-date: {out}')
        return 0

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(content, encoding='utf-8')
    print(f'wrote {out} ({len(rows)} modules)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
