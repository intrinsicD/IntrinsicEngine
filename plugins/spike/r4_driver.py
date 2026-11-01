#!/usr/bin/env python3
"""ARCH-022 slice 2, S2d: measures R4, the time from saving a plugin edit to the reloaded plugin running.

Starts the Sandbox like spike_driver.py (isolated socket, built-in config, the driver's LSan settings) and loads the
plugin once. Then, per sample (generation g = 100 + sample index):
  t0  the edit is saved: <build>/plugins/spike/generated/SpikeVersion.inc is rewritten with generation g
      (written, flushed, fsynced, closed);
  t1  `cmake --build <build> --target IntrinsicSpikePlugin` returned 0;
  t2  plugin_reload of <build>/plugins/libIntrinsicSpikePlugin.so answered, committed with generation g;
  t3  spike_version answers generation g.
Phases in wall-clock seconds (time.monotonic): build = t1 - t0, reload = t2 - t1, answer = t3 - t2, total = t3 - t0.
Samples: --warmup (not summarized) then --samples measured ones. After them, two null probes, each --probes times:
  rebuild_unchanged  `cmake --build` of the plugin target with nothing changed;
  reload_unchanged   plugin_reload of the current build, with no edit and no build.
Validity: the Sandbox binary's sha256 is the same after the samples as before them (no host rebuild in a total).
Before and after every sample and probe it records /proc/loadavg and the "some avg10" of /proc/pressure/cpu and
/proc/pressure/memory. A failed step or a wrong generation fails that sample, and the run stops there.
Writes, in <out> (which must not exist): samples.jsonl (one object per sample or probe), summary.json (the checks and
the summaries), report.html (generated from samples.jsonl and the decision rule), and the session's logs.
Summaries per phase over the measured samples: median, minimum, maximum. Decision rule on total, target 10 s (R4):
  met         every measured total < 10 s;
  median_met  median total < 10 s, but at least one total >= 10 s;
  not_met     median total >= 10 s.
Usage: python3 plugins/spike/r4_driver.py --build build/spike --out <new dir> [--samples 10] [--warmup 1]
       [--probes 3] [--icd <VK_DRIVER_FILES json>]
"""

from __future__ import annotations

import hashlib
import html
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spike_driver as s1  # noqa: E402  (shared session helpers)

TARGET_S = 10.0
PHASES = ("build", "reload", "answer", "total")


def host_load() -> dict:
    def some_avg10(name: str) -> float | None:
        try:
            line = Path(f"/proc/pressure/{name}").read_text().splitlines()[0]
            return float(line.split("avg10=")[1].split()[0])
        except (OSError, IndexError, ValueError):
            return None
    return {"loadavg": Path("/proc/loadavg").read_text().split()[:3], "psi_cpu_some_avg10": some_avg10("cpu"),
            "psi_memory_some_avg10": some_avg10("memory")}


def save(header: Path, generation: int) -> None:
    with header.open("w") as file:
        file.write(f"#define SPIKE_GENERATION {generation}\n#define SPIKE_FAIL_REGISTER 0\n")
        file.flush()
        os.fsync(file.fileno())


def build(target_build: Path, log: Path) -> bool:
    result = subprocess.run(["cmake", "--build", str(target_build), "--target", "IntrinsicSpikePlugin"],
                            capture_output=True, text=True, timeout=600)
    with log.open("a") as file:
        file.write(result.stdout + result.stderr)
    return result.returncode == 0


def generation_of(reply: object) -> object:
    return reply.get("generation") if isinstance(reply, dict) else None


def sample(agent: s1.Agent, args, index: int, kind: str) -> dict:
    header = args.build / "plugins/spike/generated/SpikeVersion.inc"
    plugin = args.build / "plugins/libIntrinsicSpikePlugin.so"
    generation = 100 + index
    record: dict = {"index": index, "kind": kind, "generation": generation, "load_before": host_load()}
    t0 = time.monotonic()
    save(header, generation)
    t0_end = time.monotonic()
    built = build(args.build, args.out / "build.log")
    t1 = time.monotonic()
    ok, reply, _, error = agent.call("plugin_reload", {"path": str(plugin)}) if built else (False, "", [], "build_failed")
    t2 = time.monotonic()
    ok_version, version, _, _ = agent.call("spike_version") if ok else (False, None, [], "")
    t3 = time.monotonic()
    record.update(save_s=t0_end - t0, build=t1 - t0, reload=t2 - t1, answer=t3 - t2, total=t3 - t0,
                  built=built, reload_ok=ok, reload_generation=generation_of(reply), reload_error=error,
                  version=version, load_after=host_load())
    record["ok"] = built and ok and generation_of(reply) == generation and ok_version and version == {"generation": generation}
    return record


def probe(agent: s1.Agent, args, name: str, index: int) -> dict:
    record: dict = {"index": index, "kind": name, "load_before": host_load()}
    start = time.monotonic()
    if name == "rebuild_unchanged":
        record["ok"] = build(args.build, args.out / "build.log")
    else:
        ok, reply, _, error = agent.call("plugin_reload", {"path": str(args.build / "plugins/libIntrinsicSpikePlugin.so")})
        record["ok"] = ok and generation_of(reply) is not None
        record["reload_error"] = error
    record["seconds"] = time.monotonic() - start
    record["load_after"] = host_load()
    return record


def summarize(records: list[dict]) -> dict:
    measured = [r for r in records if r["kind"] == "measured"]
    summary = {phase: {"median": statistics.median(r[phase] for r in measured),
                       "min": min(r[phase] for r in measured), "max": max(r[phase] for r in measured)}
               for phase in PHASES} if measured else {}
    for name in ("rebuild_unchanged", "reload_unchanged"):
        values = [r["seconds"] for r in records if r["kind"] == name]
        if values:
            summary[name] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
    if measured:
        totals = [r["total"] for r in measured]
        summary["verdict"] = ("met" if max(totals) < TARGET_S else
                              "median_met" if statistics.median(totals) < TARGET_S else "not_met")
    return summary


def report(records: list[dict], summary: dict, out: Path, valid: bool = True) -> None:
    """report.html from the records: one stacked bar per sample (build, reload, answer), the 10 s target line, the
    table of every sample and probe, and the summaries with the frozen decision rule beside them. An invalid run (a
    failed check) shows UNEVALUATED instead of a verdict."""
    timed = [r for r in records if r["kind"] in ("warmup", "measured")]
    scale = max([TARGET_S] + [r["total"] for r in timed]) * 1.1
    width, bar, gap, top, height = 760, 26, 10, 20, 220
    colors = {"build": "#4c78a8", "reload": "#f58518", "answer": "#54a24b"}
    bars = []
    for i, r in enumerate(timed):
        x, y = 60 + i * (bar + gap), top + height
        for phase in ("build", "reload", "answer"):
            h = r[phase] / scale * height
            y -= h
            bars.append(f'<rect x="{x}" y="{y:.1f}" width="{bar}" height="{h:.1f}" fill="{colors[phase]}">'
                        f'<title>{r["kind"]} {r["index"]}: {phase} {r[phase]:.2f} s</title></rect>')
        bars.append(f'<text x="{x + bar / 2}" y="{top + height + 14}" font-size="11" text-anchor="middle">'
                    f'{"w" if r["kind"] == "warmup" else r["index"]}</text>')
    target_y = top + height - TARGET_S / scale * height
    svg = (f'<svg viewBox="0 0 {width} {top + height + 40}" role="img" aria-label="Per-sample phases against the '
           f'10 s target"><line x1="50" x2="{width}" y1="{target_y:.1f}" y2="{target_y:.1f}" stroke="currentColor" '
           f'stroke-dasharray="4 3"/><text x="52" y="{target_y - 4:.1f}" font-size="11">R4 target 10 s</text>'
           f'<line x1="50" x2="50" y1="{top}" y2="{top + height}" stroke="currentColor"/>' + "".join(bars) + "</svg>")
    rows = "".join(
        f"<tr><td>{r['kind']}</td><td>{r['index']}</td><td>{'ok' if r['ok'] else 'FAILED'}</td>"
        + "".join(f"<td>{r[p]:.2f}</td>" if p in r else "<td></td>" for p in ("build", "reload", "answer", "total"))
        + f"<td>{r.get('seconds', 0):.2f}</td><td>{html.escape(' '.join(r['load_before']['loadavg']))}</td>"
        f"<td>{r['load_before']['psi_cpu_some_avg10']}</td><td>{r['load_before']['psi_memory_some_avg10']}</td></tr>"
        for r in records)
    summary_rows = "".join(f"<tr><td>{name}</td><td>{v['median']:.2f}</td><td>{v['min']:.2f}</td><td>{v['max']:.2f}</td></tr>"
                           for name, v in summary.items() if isinstance(v, dict))
    (out / "report.html").write_text(f"""<!doctype html><html><head><meta charset="utf-8"><title>R4 baseline</title>
<style>body{{font-family:system-ui,sans-serif;margin:16px;color:#222;background:#fff}}table{{border-collapse:collapse}}
td,th{{border:1px solid #ccc;padding:3px 6px;font-size:13px;text-align:right}}svg{{max-width:100%;height:auto}}</style>
</head><body><h1>ARCH-022 R4: save to reloaded plugin running</h1>
<p>Decision rule (frozen in the packet): met if every measured total &lt; 10 s; median_met if the median total &lt; 10 s
but one total &ge; 10 s; not_met if the median total &ge; 10 s. <b>Verdict: {html.escape(str(summary.get("verdict")) if valid
else "UNEVALUATED (a check failed)")}</b>.</p>
<h2>Phases per sample (build blue, reload orange, answer green)</h2>{svg}
<h2>Summaries over the measured samples and the null probes (s)</h2>
<table><tr><th>phase</th><th>median</th><th>min</th><th>max</th></tr>{summary_rows}</table>
<h2>Every sample and probe</h2><table><tr><th>kind</th><th>index</th><th>status</th><th>build</th><th>reload</th>
<th>answer</th><th>total</th><th>probe s</th><th>loadavg before</th><th>PSI cpu</th><th>PSI memory</th></tr>{rows}</table>
</body></html>
""")


def sandbox_sha(args) -> str:
    return hashlib.sha256((args.build / "bin/ExtrinsicSandbox").read_bytes()).hexdigest()


def run(agent: s1.Agent, checks: s1.Checks, args, records: list[dict]) -> None:
    ok, reply, _, error = agent.call("plugin_load", {"path": str(args.build / "plugins/libIntrinsicSpikePlugin.so")})
    if not checks.check("setup: the plugin loaded", ok, [reply, error]):
        return
    host_before = sandbox_sha(args)
    index = 0
    for kind, count in (("warmup", args.warmup), ("measured", args.samples)):
        for _ in range(count):
            record = sample(agent, args, index, kind)
            records.append(record)
            with (args.out / "samples.jsonl").open("a") as file:
                file.write(json.dumps(record) + "\n")
            index += 1
            if not record["ok"]:
                checks.check(f"sample {record['index']} ({kind}) reached the new generation", False, record)
                return
    checks.check(f"samples: {args.warmup} warmup and {args.samples} measured, each running the new generation", True)
    # Validity: a plugin build that relinked the Sandbox would put a host rebuild into every total.
    checks.check("validity: the Sandbox binary did not change during the samples", sandbox_sha(args) == host_before)
    for name in ("rebuild_unchanged", "reload_unchanged"):
        for i in range(args.probes):
            record = probe(agent, args, name, i)
            records.append(record)
            with (args.out / "samples.jsonl").open("a") as file:
                file.write(json.dumps(record) + "\n")
        checks.check(f"probe: {name} ran {args.probes} times", all(r["ok"] for r in records if r["kind"] == name))
    checks.check("exit: app_quit accepted", agent.quit())


def main() -> int:
    extra = {"--samples": 10, "--warmup": 1, "--probes": 3}
    values = dict(extra)
    for flag in extra:
        if flag in sys.argv:
            at = sys.argv.index(flag)
            values[flag] = int(sys.argv[at + 1])
            del sys.argv[at:at + 2]
    args = s1.arguments(__doc__)
    args.samples, args.warmup, args.probes = values["--samples"], values["--warmup"], values["--probes"]
    checks = s1.Checks()
    records: list[dict] = []
    s1.launch(args, checks, lambda agent: run(agent, checks, args, records))
    summary = summarize(records)
    code = s1.finish(checks, args.out)
    report(records, summary, args.out, valid=code == 0)
    data = json.loads((args.out / "summary.json").read_text())
    data["summary"] = summary
    (args.out / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
