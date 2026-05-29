"""Harness layer -- task-unspecific. Two paths, one interface:

  * real:  writes the candidate source, invokes cfg.harness_cmd (the engine harness host)
           with a hard timeout and the always-present args plus task.harness_args(), reads
           its harness-result JSON (contract.md S2), then runs the task's trusted scorer on
           the produced artifact and merges.
  * mock:  delegates to task.offline_mock_run() to synthesize an artifact + perf, so the
           full loop runs with no GPU/engine.

Scoring (gate + quality) is always done by the task's trusted scorer, never by candidate
code, in both paths -- that separation is the core safety invariant.
"""
from __future__ import annotations
import os, json, subprocess


def _real_run(cfg, task, manifest, workdir):
    src_path = os.path.join(workdir, f"{manifest['id']}.src")
    with open(src_path, "w") as f:
        f.write(manifest.get("source", ""))
    manifest_path = os.path.join(workdir, f"{manifest['id']}.manifest.json")
    artifact_path = os.path.join(workdir, f"{manifest['id']}.artifact")
    result_path = os.path.join(workdir, f"{manifest['id']}.harness.json")
    m = dict(manifest)
    m["source"] = {"path": src_path, "lang": manifest.get("kind")}
    with open(manifest_path, "w") as f:
        json.dump(m, f)

    cmd = cfg.harness_cmd.split() + [
        "--manifest", manifest_path, "--points", artifact_path,
        "--out", result_path, "--timeout-ms", str(cfg.harness_timeout_ms),
    ] + task.harness_args(cfg)
    try:
        subprocess.run(cmd, timeout=cfg.harness_timeout_ms / 1000.0 + 10, check=False)
    except subprocess.TimeoutExpired:
        return {"build": {"ok": True, "log": ""},
                "run": {"ok": False, "timed_out": True, "gpu_reset": False,
                        "log": "harness wall-clock timeout"},
                "artifact": None, "performance": {}}
    if os.path.exists(result_path):
        with open(result_path) as f:
            return json.load(f)
    return {"build": {"ok": False, "log": "no harness result produced"},
            "run": {"ok": False, "timed_out": False, "log": ""},
            "artifact": None, "performance": {}}


def run_candidate(cfg, task, manifest) -> dict:
    workdir = os.path.join(cfg.workdir, cfg.goal_id)
    os.makedirs(workdir, exist_ok=True)

    if cfg.offline or not cfg.harness_cmd:
        artifact_path = os.path.join(workdir, f"{manifest['id']}.artifact")
        hres = task.offline_mock_run(cfg, manifest, artifact_path)
    else:
        hres = _real_run(cfg, task, manifest, workdir)

    if hres.get("artifact") and os.path.exists(hres["artifact"]) and \
            hres.get("run", {}).get("ok"):
        score = task.score(cfg, hres["artifact"])
        hres["gate"] = score.get("gate", {"passed": False})
        hres["quality"] = score.get("quality")
    else:
        hres["gate"] = {"passed": False, "reasons": ["no artifact / run failed"]}
        hres["quality"] = None
    hres["id"] = manifest["id"]
    return hres
