"""Trivial, domain-free task. A "candidate" just carries a synthetic objective and a validity
flag; the mock harness writes them to an artifact and the scorer reads them back. This exists
so the whole pipeline -- generation, gate, tournament, Elo, evolution, meta-review -- runs with
zero domain code, no API key and no GPU. It is the canonical minimal Task example."""
from __future__ import annotations
import json
from coscientist.task import Task


class DemoTask(Task):
    name = "demo"

    def experiment_summary(self, cfg) -> str:
        return "synthetic objective in [0,1]; gate passes when the candidate is marked valid"

    def default_fitness_weights(self) -> dict:
        return {"objective": 1.0}

    def score(self, cfg, artifact_path: str) -> dict:
        with open(artifact_path) as f:
            m = json.load(f)
        valid = bool(m.get("valid", True))
        gate = {"passed": valid, "reasons": [] if valid else ["marked invalid"]}
        quality = {"objective": float(m.get("objective", 0.0))} if valid else None
        return {"gate": gate, "quality": quality}

    # ---- offline stubs ----
    def offline_candidates(self, cfg, n, kind):
        spread = [(0.30, True), (0.60, True), (0.50, False), (0.45, True)]
        out = []
        for i in range(n):
            o, v = spread[i % len(spread)]
            out.append({"summary": f"demo obj={o} valid={v}", "rationale": "offline demo",
                        "source": "", "params": {"objective": o, "valid": v}})
        return out

    def offline_evolve(self, cfg, parents, n):
        out = []
        for p in parents[:n]:
            o = p["manifest"].get("params", {}).get("objective", 0.0)
            o2 = min(1.0, round(o + 0.1, 2))
            out.append({"summary": f"demo evolved obj={o2}", "rationale": "tuned up",
                        "source": "", "params": {"objective": o2, "valid": True}})
        return out

    def offline_mock_run(self, cfg, manifest, artifact_path):
        p = manifest.get("params", {})
        with open(artifact_path, "w") as f:
            json.dump({"objective": p.get("objective", 0.0),
                       "valid": p.get("valid", True)}, f)
        return {"build": {"ok": True, "log": "mock"},
                "run": {"ok": True, "timed_out": False, "gpu_reset": False, "log": "mock"},
                "artifact": artifact_path, "performance": {"time_ms": 1.0}}

    def offline_meta(self, cfg):
        return "- candidates marked invalid fail the gate; the loop should evolve valid ones up"


TASK = DemoTask()
