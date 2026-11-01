#!/usr/bin/env python3
"""ARCH-022 S2d: combines the R4 runs of one link variant (their samples.jsonl) into one summary and report, with the
same summaries and decision rule as r4_driver.py. Warmups are excluded; every run must have passed its checks.
Usage: python3 plugins/spike/r4_report.py <out dir> <run dir> [<run dir> ...]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import r4_driver  # noqa: E402


def main() -> int:
    out, runs = Path(sys.argv[1]), [Path(arg) for arg in sys.argv[2:]]
    out.mkdir(parents=True)
    failed = {str(run): json.loads((run / "summary.json").read_text())["failed"] for run in runs}
    records, index = [], 0
    for run in runs:
        for line in (run / "samples.jsonl").read_text().splitlines():
            record = json.loads(line)
            record["run"], record["index"] = run.name, index
            records.append(record)
            index += 1
    summary = r4_driver.summarize(records)
    (out / "summary.json").write_text(json.dumps({"runs": [str(r) for r in runs], "run_failures": failed,
                                                  "valid": not any(failed.values()), "summary": summary}, indent=2) + "\n")
    r4_driver.report(records, summary, out, valid=not any(failed.values()))
    print(json.dumps({"valid": not any(failed.values()), "verdict": summary.get("verdict")}))
    return 0 if not any(failed.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
