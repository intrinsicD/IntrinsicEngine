#!/usr/bin/env python3
"""Report the last recorded date of each on-demand audit sweep.

The output and drift audit sweeps (see ``docs/agent/review.md`` §"Audit
sweeps") run on demand — preferably overnight — with no enforced cadence
(2026-08-14 pair-workflow decision). Reports land at
``docs/reports/<YYYY-MM-DD>-agent-output-audit.md`` and
``docs/reports/<YYYY-MM-DD>-drift-audit.md``.

This checker reports last-report ages on request; it never gates PR merges
and is wired into no workflow. Default mode reports and exits 0; ``--strict``
exits nonzero past the age thresholds for operators who want a reminder at
their own chosen cadence.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
from pathlib import Path

CADENCES = (
    ("agent-output audit", re.compile(r"^(\d{4}-\d{2}-\d{2})-agent-output-audit\.md$")),
    ("drift audit", re.compile(r"^(\d{4}-\d{2}-\d{2})-drift-audit\.md$")),
)

DEFAULT_MAX_AGE_DAYS = {"agent-output audit": 14, "drift audit": 42}


def newest_report_date(reports_dir: Path, pattern: re.Pattern[str]) -> tuple[dt.date | None, Path | None]:
    newest: tuple[dt.date, Path] | None = None
    if not reports_dir.is_dir():
        return None, None
    for path in reports_dir.iterdir():
        m = pattern.match(path.name)
        if not m:
            continue
        try:
            date = dt.date.fromisoformat(m.group(1))
        except ValueError:
            continue
        if newest is None or date > newest[0]:
            newest = (date, path)
    return (newest[0], newest[1]) if newest else (None, None)


def cadence_status(
    reports_dir: Path, today: dt.date | None = None
) -> list[tuple[str, dt.date | None, Path | None]]:
    """Return (cadence name, newest report date, newest report path) rows."""
    return [
        (name, *newest_report_date(reports_dir, pattern)) for name, pattern in CADENCES
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Report audit cadence lapses.")
    parser.add_argument("--root", default=None, help="Repository root path.")
    parser.add_argument(
        "--reports-dir", default=None, help="Reports directory (default: <root>/docs/reports)."
    )
    parser.add_argument(
        "--max-age-agent-output-days",
        type=int,
        default=DEFAULT_MAX_AGE_DAYS["agent-output audit"],
    )
    parser.add_argument(
        "--max-age-drift-days", type=int, default=DEFAULT_MAX_AGE_DAYS["drift audit"]
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit nonzero on a lapse (local use only; never wire into PR gates).",
    )
    args = parser.parse_args()

    repo_root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[2]
    reports_dir = Path(args.reports_dir) if args.reports_dir else repo_root / "docs" / "reports"
    max_age = {
        "agent-output audit": args.max_age_agent_output_days,
        "drift audit": args.max_age_drift_days,
    }
    today = dt.date.today()

    lapses = 0
    for name, date, path in cadence_status(reports_dir):
        limit = max_age[name]
        if date is None:
            print(f"[audit-cadence] {name}: OVERDUE — no report found in {reports_dir}")
            lapses += 1
        elif (today - date).days > limit:
            print(
                f"[audit-cadence] {name}: OVERDUE — last report {date} "
                f"({(today - date).days}d ago, limit {limit}d): {path}"
            )
            lapses += 1
        else:
            print(f"[audit-cadence] {name}: ok — last report {date}: {path}")

    if lapses and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
