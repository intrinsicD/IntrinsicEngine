#!/usr/bin/env python3
"""ARCH-022 S2d: combines the R4 runs of one link variant (their samples.jsonl) into one summary and report, with the
same summaries and decision rule as r4_driver.py. Warmups are excluded. The pool is valid only if every run passed
its checks and holds exactly the frozen counts (1 warmup, 5 measured, 3 of each probe; --expect W,M,P overrides);
an invalid pool's verdict is UNEVALUATED.
Usage: python3 plugins/spike/r4_report.py [--expect 1,5,3] <out dir> <run dir> [<run dir> ...]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import r4_driver  # noqa: E402


def main() -> int:
    argv = sys.argv[1:]
    expect = (1, 5, 3)
    if argv[:1] == ["--expect"]:
        expect, argv = tuple(int(x) for x in argv[1].split(",")), argv[2:]
    out, runs = Path(argv[0]), [Path(arg) for arg in argv[1:]]
    out.mkdir(parents=True)
    failed = {str(run): json.loads((run / "summary.json").read_text())["failed"] for run in runs}
    records, index = [], 0
    for run in runs:
        run_records = [json.loads(line) for line in (run / "samples.jsonl").read_text().splitlines()]
        counts = [sum(r["kind"] == kind for r in run_records) for kind in
                  ("warmup", "measured", "rebuild_unchanged", "reload_unchanged")]
        if counts != [expect[0], expect[1], expect[2], expect[2]]:
            failed[str(run)] = failed[str(run)] + [f"counts {counts} differ from the frozen {list(expect)}"]
        for record in run_records:
            record["run"], record["index"] = run.name, index
            records.append(record)
            index += 1
    summary = r4_driver.summarize(records)
    if any(failed.values()) and "verdict" in summary:
        summary["verdict"] = "UNEVALUATED"
    (out / "summary.json").write_text(json.dumps({"runs": [str(r) for r in runs], "run_failures": failed,
                                                  "valid": not any(failed.values()), "summary": summary}, indent=2) + "\n")
    r4_driver.report(records, summary, out, valid=not any(failed.values()))
    print(json.dumps({"valid": not any(failed.values()), "verdict": summary.get("verdict")}))
    return 0 if not any(failed.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
