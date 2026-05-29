"""Reference worked example task: Poisson-disk sampling. Wraps the validated trusted scorer
(analyzer.py) and supplies the mock samplers + offline stubs. Copy this directory as a
template when adding a new target (e.g. an NPR filter scored by FLIP/SSIM vs a reference)."""
from __future__ import annotations
import math
import numpy as np
from coscientist.task import Task
from .analyzer import analyze


# ---------------------------------------------------------------- mock samplers
def _bridson(r, domain, seed, k=30):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = domain
    W, H = x1 - x0, y1 - y0
    cell = r / math.sqrt(2)
    gw, gh = int(math.ceil(W / cell)), int(math.ceil(H / cell))
    grid = -np.ones((gw, gh), dtype=int)
    pts, active, work = [], [], 0

    def gi(p):
        return int((p[0] - x0) / cell), int((p[1] - y0) / cell)

    p0 = (x0 + rng.random() * W, y0 + rng.random() * H)
    pts.append(p0); active.append(0)
    cx, cy = gi(p0); grid[cx, cy] = 0
    while active:
        ai = int(rng.integers(len(active)))
        base = pts[active[ai]]
        placed = False
        for _ in range(k):
            work += 1
            ang = rng.random() * 2 * math.pi
            rad = r * (1 + rng.random())
            q = (base[0] + math.cos(ang) * rad, base[1] + math.sin(ang) * rad)
            if not (x0 <= q[0] < x1 and y0 <= q[1] < y1):
                continue
            gx, gy = gi(q)
            ok = True
            for ix in range(max(0, gx - 2), min(gw, gx + 3)):
                for iy in range(max(0, gy - 2), min(gh, gy + 3)):
                    j = grid[ix, iy]
                    if j >= 0 and (pts[j][0]-q[0])**2 + (pts[j][1]-q[1])**2 < r*r:
                        ok = False; break
                if not ok:
                    break
            if ok:
                pts.append(q); grid[gx, gy] = len(pts)-1
                active.append(len(pts)-1); placed = True; break
        if not placed:
            active.pop(ai)
    return np.array(pts), work


def _jittered_grid(r, domain, seed):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = domain
    s = r * 1.6
    j = 0.10 * s
    gx = np.arange(x0 + s/2, x1, s)
    gy = np.arange(y0 + s/2, y1, s)
    mx, my = np.meshgrid(gx, gy)
    pts = np.stack([mx.ravel(), my.ravel()], 1) + rng.uniform(-j, j, (mx.size, 2))
    return np.clip(pts, [x0, y0], [x1, y1]), pts.shape[0]


def _white_noise(r, domain, seed, n):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = domain
    return rng.random((n, 2)) * [x1 - x0, y1 - y0] + [x0, y0], n


class PoissonTask(Task):
    name = "poisson"

    def _p(self, cfg, key, default):
        return cfg.task_params.get(key, default)

    def experiment_summary(self, cfg) -> str:
        return (f"domain={self._p(cfg,'domain',[0,0,1,1])}, conflict radius "
                f"r={self._p(cfg,'radius',0.02)}, target_count={self._p(cfg,'target_count',4096)}, "
                f"periodic={self._p(cfg,'periodic',False)}, seed={self._p(cfg,'seed',1)}")

    def default_fitness_weights(self) -> dict:
        return {"blue_noise_quality_heuristic": 1.0, "coverage_radius_over_r": -0.30,
                "low_frequency_energy": -0.50, "samples_per_sec": 0.0}

    def harness_args(self, cfg) -> list[str]:
        return ["--radius", str(self._p(cfg, "radius", 0.02)),
                "--target", str(self._p(cfg, "target_count", 4096)),
                "--seed", str(self._p(cfg, "seed", 1))]

    def score(self, cfg, artifact_path: str) -> dict:
        return analyze(artifact_path, self._p(cfg, "radius", 0.02),
                       self._p(cfg, "domain", [0, 0, 1, 1]),
                       self._p(cfg, "periodic", False))

    # ---- offline ----
    def offline_candidates(self, cfg, n, kind):
        methods = ["bridson", "grid", "random", "bridson"]
        return [{"summary": f"{methods[i % 4]} sampler",
                 "rationale": f"offline stub: {methods[i % 4]}",
                 "source": f"// offline {methods[i % 4]}",
                 "params": {"method": methods[i % 4]}} for i in range(n)]

    def offline_evolve(self, cfg, parents, n):
        out = []
        for p in parents[:n]:
            m = p["manifest"].get("params", {}).get("method", "bridson")
            base = "bridson" if m == "random" else m
            out.append({"summary": f"{base} tuned", "rationale": "more candidate attempts",
                        "source": "// tuned", "params": {"method": base, "tuned": True}})
        return out

    def offline_meta(self, cfg):
        return ("- white-noise candidates fail the conflict-radius gate; stop proposing them\n"
                "- grid candidates pass the gate but score ~0 on blue-noise quality "
                "(anisotropic spectrum); prefer dart-throwing / relaxation methods")

    def offline_mock_run(self, cfg, manifest, artifact_path):
        r = self._p(cfg, "radius", 0.02)
        domain = self._p(cfg, "domain", [0, 0, 1, 1])
        seed = self._p(cfg, "seed", 1)
        params = manifest.get("params", {})
        method = params.get("method", "bridson")
        if method == "grid":
            pts, work = _jittered_grid(r, domain, seed)
        elif method == "random":
            pts, work = _white_noise(r, domain, seed, self._p(cfg, "target_count", 4096))
        else:
            pts, work = _bridson(r, domain, seed, k=40 if params.get("tuned") else 30)
        np.savetxt(artifact_path, pts, fmt="%.6f")
        time_ms = max(0.05, work * 1e-3)
        return {"build": {"ok": True, "log": "mock"},
                "run": {"ok": True, "timed_out": False, "gpu_reset": False, "log": "mock"},
                "artifact": artifact_path,
                "performance": {"samples": int(len(pts)), "time_ms": round(time_ms, 3),
                                "samples_per_sec": round(len(pts)/(time_ms/1000.0), 1),
                                "vram_mb": 8.0}}


TASK = PoissonTask()
