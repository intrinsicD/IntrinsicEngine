#!/usr/bin/env python3
"""ARCH-022 slice 2, S2d: measures R4, the time from saving a plugin edit to the reloaded plugin running.

Starts the Sandbox like spike_driver.py (isolated socket, built-in config, the driver's LSan settings), saves
diagnostics_read as diagnostics.json, and loads the plugin once. Then, per sample (generation g = --generation-base +
sample index):
  t0  the save begins: <build>/plugins/spike/generated/SpikeVersion.inc is rewritten with generation g (written,
      flushed, fsynced, closed; save_s is that write alone);
  t1  `cmake --build <build> --target IntrinsicSpikePlugin` returned 0 (600 s timeout);
  t2  plugin_reload of <build>/plugins/libIntrinsicSpikePlugin.so answered, committed with generation g;
  t3  spike_version answers generation g.
Phases in wall-clock seconds (time.monotonic): build = t1 - t0, reload = t2 - t1, answer = t3 - t2, total = t3 - t0;
compile_s and link_s are the plugin's compile and link steps from the tree's .ninja_log.
A sample is valid only if all of these hold:
  - the build's Ninja edges (its "[n/m] description" lines) include the plugin's compile and link, and every edge is
    the plugin's: its description names "plugins/" or the glob re-check (no host edge);
  - the reload reply has generation g, its config report restored ["spike"] and rejected nothing;
  - the previous generation's close record is new and clean: closed, unmapped, every callable count 0;
  - spike_version answers generation g.
A build that exceeds its timeout or a reload whose reply does not come within the socket's 60 s is recorded as a
failed sample with its elapsed time. The first failed sample stops the session.
Samples: --warmup (not summarized) then --samples measured ones. After them, two null probes, each --probes times:
  rebuild_unchanged  `cmake --build` of the plugin target with nothing changed; valid if the plugin file is unchanged
                     (sha256) and no plugin compile or link edge ran;
  reload_unchanged   plugin_reload of the current build, with no edit and no build; valid if it answers the last
                     generation and the close before it is clean.
A sample also needs finite compile_s and link_s, and its previous-generation close must be a new record (one more
close than before the reload), so an older record is never reused; the reload probe checks the same.
Session validity: the Sandbox binary's sha256 is the same after the samples as before them. Recorded before the
quit: the Sandbox's VmHWM and the largest finished child process's max RSS (the builds), in KiB.
Before and after every sample and probe it records /proc/loadavg and the "some avg10" of /proc/pressure/cpu and
/proc/pressure/memory. A failed step or a wrong generation fails that sample, and the run stops there.
Writes, in <out> (which must not exist): samples.jsonl (one object per sample or probe), summary.json (the checks and
the summaries), report.html (generated from samples.jsonl and the decision rule), and the session's logs.
Summaries per phase over the measured samples: median, minimum, maximum. Decision rule on total, target 10 s (R4):
  met         every measured total < 10 s;
  median_met  median total < 10 s, but at least one total >= 10 s;
  not_met     median total >= 10 s.
summary.json holds the verdict only when every check passed; otherwise its verdict is UNEVALUATED.
Usage: python3 plugins/spike/r4_driver.py --build build/dev --out <new dir> [--samples 10] [--warmup 1]
       [--probes 3] [--generation-base 100] [--icd <VK_DRIVER_FILES json>]
"""

from __future__ import annotations

import hashlib
import html
import json
import math
import os
import re
import resource
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spike_driver as s1  # noqa: E402  (shared session helpers)

TARGET_S = 10.0
MEMORY: dict = {}  # peak memory of the session, filled before the quit
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


PLUGIN_OBJECT = "plugins/spike/CMakeFiles/IntrinsicSpikePlugin.dir/SpikePlugin.cpp.o"
PLUGIN_LIBRARY = "plugins/libIntrinsicSpikePlugin.so"
EDGE = re.compile(r"^\[\d+/\d+\] (.+)$", re.MULTILINE)


def plugin_edge(description: str) -> bool:
    return "plugins/" in description or "globbed directories" in description


ACTIVE_GROUP: list[subprocess.Popen] = []  # the build whose process group is running, if any


def stop_active_group(signum: int, _frame) -> None:
    """On SIGTERM (the outer `timeout`): kill and reap the running build's group, which is outside the driver's own
    process group, then exit."""
    for process in ACTIVE_GROUP:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    sys.exit(128 + signum)


def run_group(command: list[str], timeout_s: float) -> tuple[bool, int | None, str]:
    """Runs command in its own process group; (timed out, return code, combined output). On the timeout the whole
    group (cmake, ninja, compilers) is killed and reaped."""
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    ACTIVE_GROUP.append(process)
    try:
        output, _ = process.communicate(timeout=timeout_s)
        return False, process.returncode, output or ""
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        return True, None, output or ""
    finally:
        ACTIVE_GROUP.remove(process)


def build(target_build: Path, log: Path) -> tuple[bool, list[str]]:
    """(succeeded, the Ninja edge descriptions it ran); past 600 s the build is killed and counts as failed."""
    timed_out, code, output = run_group(["cmake", "--build", str(target_build), "--target", "IntrinsicSpikePlugin"], 600)
    with log.open("a") as file:
        file.write(output + ("\n[r4_driver] build timed out after 600 s\n" if timed_out else ""))
    return not timed_out and code == 0, EDGE.findall(output)


def step_seconds(target_build: Path, output: str) -> float | None:
    """The latest .ninja_log entry's duration for one output (v5: start, end, mtime, output, hash; milliseconds)."""
    seconds = None
    for line in (target_build / ".ninja_log").read_text().splitlines():
        fields = line.split("\t")
        if len(fields) >= 4 and fields[3] == output:
            seconds = (int(fields[1]) - int(fields[0])) / 1000.0
    return seconds


def closes(agent: s1.Agent) -> list[dict]:
    ok, value, _, _ = agent.call("plugin_host_identity")
    return value.get("closes", []) if ok and isinstance(value, dict) else []


def new_clean_close(count_before: int, after: list[dict], generation: int) -> bool:
    """Exactly one close happened since count_before, of this generation, closed and unmapped with nothing alive;
    an older record (a reload that closed nothing) does not count."""
    return len(after) == count_before + 1 and after[-1].get("generation") == generation and s1.clean_close(after[-1])


def finite(value: object) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(value)


def generation_of(reply: object) -> object:
    return reply.get("generation") if isinstance(reply, dict) else None


def sample(agent: s1.Agent, args, index: int, kind: str, previous: int) -> dict:
    header = args.build / "plugins/spike/generated/SpikeVersion.inc"
    plugin = args.build / "plugins/libIntrinsicSpikePlugin.so"
    generation = args.generation_base + index
    record: dict = {"index": index, "kind": kind, "generation": generation, "load_before": host_load()}
    closes_before = len(closes(agent))
    t0 = time.monotonic()
    save(header, generation)
    t0_end = time.monotonic()
    built, edges = build(args.build, args.out / "build.log")
    t1 = time.monotonic()
    ok, reply, error, ok_version, version = False, None, "build_failed", False, None
    if built:
        try:
            ok, reply, _, error = agent.call("plugin_reload", {"path": str(plugin)})
        except OSError as failure:  # no reply within the socket's 60 s
            error = f"{type(failure).__name__}: {failure}"
    t2 = time.monotonic()
    if ok:
        ok_version, version, _, _ = agent.call("spike_version")
    t3 = time.monotonic()
    closes_after = closes(agent) if ok else []
    config = reply.get("config", {}) if isinstance(reply, dict) else {}
    record.update(save_s=t0_end - t0, build=t1 - t0, reload=t2 - t1, answer=t3 - t2, total=t3 - t0,
                  compile_s=step_seconds(args.build, PLUGIN_OBJECT), link_s=step_seconds(args.build, PLUGIN_LIBRARY),
                  edges=edges, built=built, reload_ok=ok, reload_generation=generation_of(reply), reload_error=error,
                  reload_config=config, previous_close=closes_after[-1] if closes_after else {}, version=version,
                  load_after=host_load())
    record["checks"] = {
        "plugin_edges_only": built and any(PLUGIN_OBJECT in e for e in edges) and any(PLUGIN_LIBRARY in e for e in edges)
        and all(plugin_edge(e) for e in edges),
        "reload_generation": ok and generation_of(reply) == generation,
        "config_restored": config.get("restored") == ["spike"] and config.get("rejected") == [],
        "previous_close_clean": new_clean_close(closes_before, closes_after, previous),
        "version": ok_version and version == {"generation": generation},
        "steps_recorded": finite(record["compile_s"]) and finite(record["link_s"]),
    }
    record["ok"] = all(record["checks"].values())
    return record


def probe(agent: s1.Agent, args, name: str, index: int, generation: int) -> dict:
    record: dict = {"index": index, "kind": name, "load_before": host_load()}
    plugin = args.build / "plugins/libIntrinsicSpikePlugin.so"
    before = hashlib.sha256(plugin.read_bytes()).hexdigest()
    start = time.monotonic()
    if name == "rebuild_unchanged":
        built, edges = build(args.build, args.out / "build.log")
        record["seconds"] = time.monotonic() - start
        record["edges"] = edges
        record["ok"] = built and hashlib.sha256(plugin.read_bytes()).hexdigest() == before and \
            not any(PLUGIN_OBJECT in e or PLUGIN_LIBRARY in e for e in edges)
    else:
        count_before = len(closes(agent))
        start = time.monotonic()  # the reload alone, as in a sample's reload phase
        ok, reply, _, error = agent.call("plugin_reload", {"path": str(plugin)})
        record["seconds"] = time.monotonic() - start
        record["reload_error"] = error
        record["ok"] = ok and generation_of(reply) == generation and new_clean_close(count_before, closes(agent), generation)
    record["load_after"] = host_load()
    return record


def summarize(records: list[dict]) -> dict:
    measured = [r for r in records if r["kind"] == "measured"]
    summary = {phase: {"median": statistics.median(r[phase] for r in measured),
                       "min": min(r[phase] for r in measured), "max": max(r[phase] for r in measured)}
               for phase in PHASES} if measured else {}
    for step in ("compile_s", "link_s"):  # the plugin's own steps (a valid sample has both)
        values = [r.get(step) for r in measured]
        if values and all(finite(v) for v in values):
            summary[step] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
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
        + "".join(f"<td>{r[p]:.2f}</td>" if isinstance(r.get(p), (int, float)) else "<td></td>"
                  for p in ("build", "compile_s", "link_s", "reload", "answer", "total"))
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
<h2>Every sample and probe</h2><table><tr><th>kind</th><th>index</th><th>status</th><th>build</th><th>compile</th>
<th>link</th><th>reload</th>
<th>answer</th><th>total</th><th>probe s</th><th>loadavg before</th><th>PSI cpu</th><th>PSI memory</th></tr>{rows}</table>
</body></html>
""")


def sandbox_sha(args) -> str:
    return hashlib.sha256((args.build / "bin/ExtrinsicSandbox").read_bytes()).hexdigest()


def run(agent: s1.Agent, checks: s1.Checks, args, records: list[dict]) -> None:
    _, diagnostics, _, _ = agent.call("diagnostics_read")
    (args.out / "diagnostics.json").write_text(json.dumps(diagnostics, indent=2) + "\n")
    ok, reply, _, error = agent.call("plugin_load", {"path": str(args.build / "plugins/libIntrinsicSpikePlugin.so")})
    if not checks.check("setup: the plugin loaded", ok, [reply, error]):
        return
    host_before = sandbox_sha(args)
    index, previous = 0, generation_of(reply)
    for kind, count in (("warmup", args.warmup), ("measured", args.samples)):
        for _ in range(count):
            record = sample(agent, args, index, kind, previous)
            records.append(record)
            with (args.out / "samples.jsonl").open("a") as file:
                file.write(json.dumps(record) + "\n")
            index, previous = index + 1, record["generation"]
            if not record["ok"]:
                checks.check(f"sample {record['index']} ({kind}) passed every sample check", False, record["checks"])
                agent.quit()  # end the session now, so the exit checks still judge a clean exit
                return
    checks.check(f"samples: {args.warmup} warmup and {args.samples} measured, each passing every sample check", True)
    # Validity: a plugin build that relinked the Sandbox would put a host rebuild into every total.
    checks.check("validity: the Sandbox binary did not change during the samples", sandbox_sha(args) == host_before)
    for name in ("rebuild_unchanged", "reload_unchanged"):
        for i in range(args.probes):
            record = probe(agent, args, name, i, previous)
            records.append(record)
            with (args.out / "samples.jsonl").open("a") as file:
                file.write(json.dumps(record) + "\n")
        checks.check(f"probe: {name} ran {args.probes} times", all(r["ok"] for r in records if r["kind"] == name))
    # Peak memory: the Sandbox's high-water mark, and the largest process of any build (cmake, ninja, compilers).
    _, identity, _, _ = agent.call("plugin_host_identity")
    status = Path(f"/proc/{identity.get('pid')}/status").read_text() if isinstance(identity, dict) else ""
    hwm = next((int(line.split()[1]) for line in status.splitlines() if line.startswith("VmHWM:")), None)
    MEMORY.update(sandbox_vmhwm_kib=hwm, build_max_rss_kib=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    checks.check("exit: app_quit accepted", agent.quit())


def main() -> int:
    extra = {"--samples": 10, "--warmup": 1, "--probes": 3, "--generation-base": 100}
    values = dict(extra)
    for flag in extra:
        if flag in sys.argv:
            at = sys.argv.index(flag)
            values[flag] = int(sys.argv[at + 1])
            del sys.argv[at:at + 2]
    args = s1.arguments(__doc__)
    args.samples, args.warmup, args.probes = values["--samples"], values["--warmup"], values["--probes"]
    args.generation_base = values["--generation-base"]
    signal.signal(signal.SIGTERM, stop_active_group)
    checks = s1.Checks()
    records: list[dict] = []
    s1.launch(args, checks, lambda agent: run(agent, checks, args, records))
    summary = summarize(records)
    code = s1.finish(checks, args.out)
    if code != 0 and "verdict" in summary:
        summary["verdict"] = "UNEVALUATED"
    report(records, summary, args.out, valid=code == 0)
    data = json.loads((args.out / "summary.json").read_text())
    data["summary"], data["memory"] = summary, MEMORY
    (args.out / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
