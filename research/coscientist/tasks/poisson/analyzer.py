#!/usr/bin/env python3
"""
poisson_analyzer.py -- the trusted fitness function for Poisson-disk sampling candidates.

This is the *judge* in the co-scientist loop, and it is deliberately fixed and trusted:
candidate algorithms NEVER run inside it. A candidate (shader or C++ module) is executed
by the harness host, which dumps the resulting point set as an artifact. This script reads
that artifact and produces an objective, reproducible score. Keeping the judge separate
from the untrusted candidate is the single most important guardrail in the whole system:
it is what stops the loop from "discovering" fast garbage and believing it succeeded.

Two stages, run strictly in order. Quality is only computed if the correctness gate passes,
because quality numbers for an invalid point set are meaningless.

  Stage 1 -- Correctness gate (hard pass/fail):
      * every point inside the domain
      * Poisson-disk property: no two points closer than the conflict radius r
    A candidate that fails the gate is rejected outright, regardless of how fast it is.

  Stage 2 -- Quality + packing metrics (only for valid sets):
      * normalized minimum distance (margin above r)
      * coverage radius / largest empty disk (gap detection, proxy for maximality)
      * blue-noise spectrum: low-frequency energy and radial anisotropy

Performance (samples/sec, time-to-N, VRAM) is measured by the harness, not here. Pass a
--perf JSON file to merge those numbers into the final result so the orchestrator gets one
complete record per candidate.

Usage:
    poisson_analyzer.py --points pts.npy --radius 0.01 --domain 0 0 1 1 [--periodic] \
        [--perf perf.json] [--out result.json]
    poisson_analyzer.py --selftest      # generate a good and a bad set, score both

Point file formats: .npy (Nx2 float array) or whitespace/comma text, one "x y" per line.
"""
from __future__ import annotations
import argparse, json, sys, math
import numpy as np

try:
    from scipy.spatial import cKDTree
except ImportError:
    sys.exit("scipy is required: pip install scipy numpy")


# --------------------------------------------------------------------------- I/O
def load_points(path: str) -> np.ndarray:
    if path.endswith(".npy"):
        pts = np.load(path)
    else:
        pts = np.loadtxt(path, delimiter="," if path.endswith(".csv") else None)
    pts = np.atleast_2d(np.asarray(pts, dtype=np.float64))
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError(f"expected an Nx2 point set, got shape {pts.shape}")
    return pts


# ---------------------------------------------------------------- correctness gate
def correctness_gate(pts, r, domain, periodic):
    xmin, ymin, xmax, ymax = domain
    L = np.array([xmax - xmin, ymax - ymin])
    n = len(pts)

    report = {"n_points": int(n), "passed": False, "reasons": []}
    if n == 0:
        report["reasons"].append("empty point set")
        return report
    if not np.all(np.isfinite(pts)):
        report["reasons"].append("non-finite coordinates present")
        return report

    eps = 1e-9
    in_domain = np.all((pts[:, 0] >= xmin - eps) & (pts[:, 0] <= xmax + eps) &
                       (pts[:, 1] >= ymin - eps) & (pts[:, 1] <= ymax + eps))
    if not in_domain:
        n_out = int(np.sum((pts[:, 0] < xmin - eps) | (pts[:, 0] > xmax + eps) |
                           (pts[:, 1] < ymin - eps) | (pts[:, 1] > ymax + eps)))
        report["reasons"].append(f"{n_out} point(s) outside the domain")

    # min-distance check via spatial tree (periodic = toroidal wrap)
    local = pts - [xmin, ymin]
    if periodic:
        local = np.mod(local, L)
        tree = cKDTree(local, boxsize=L)
    else:
        tree = cKDTree(local)

    # any pair strictly closer than r (small tolerance to forgive fp noise)
    tol = r * 1e-6
    pairs = tree.query_pairs(r - tol)
    n_violations = len(pairs)
    if n_violations:
        report["reasons"].append(f"{n_violations} pair(s) closer than r")

    # nearest-neighbour distance for diagnostics
    dist, _ = tree.query(local, k=2)
    min_nn = float(np.min(dist[:, 1])) if n > 1 else float("inf")
    report["min_nn_distance"] = min_nn
    report["min_nn_over_r"] = min_nn / r if math.isfinite(min_nn) else None
    report["n_distance_violations"] = int(n_violations)
    report["in_domain"] = bool(in_domain)

    report["passed"] = bool(in_domain and n_violations == 0)
    return report, tree, local, L


# --------------------------------------------------------------------- quality
def coverage_radius(tree, domain, L, periodic, n_queries=20000, seed=0):
    """Largest empty disk: max over query points of distance to nearest sample.
    A maximal blue-noise set keeps this close to r; large values mean gaps."""
    rng = np.random.default_rng(seed)
    q = rng.random((n_queries, 2)) * L
    d, _ = tree.query(q, k=1)
    return float(np.max(d)), float(np.mean(d))


def blue_noise_spectrum(pts, domain, L, grid=256):
    """Standard point-process power spectrum via splat-to-grid + FFT, radially averaged.
    Returns diagnostics that distinguish blue noise (low energy near DC, isotropic)
    from white/clustered noise (flat or low-frequency-heavy, possibly anisotropic)."""
    xmin, ymin, _, _ = domain
    u = (pts[:, 0] - xmin) / L[0]
    v = (pts[:, 1] - ymin) / L[1]
    counts, _, _ = np.histogram2d(u, v, bins=grid, range=[[0, 1], [0, 1]])
    counts = counts - counts.mean()                      # remove DC
    power = np.abs(np.fft.fftshift(np.fft.fft2(counts))) ** 2

    c = grid // 2
    yy, xx = np.indices(power.shape)
    rr = np.sqrt((xx - c) ** 2 + (yy - c) ** 2)
    rr_int = rr.astype(int)
    nbins = c
    radial = np.array([power[rr_int == k].mean() if np.any(rr_int == k) else 0.0
                       for k in range(1, nbins)])  # skip DC bin
    if radial.size == 0 or radial.max() == 0:
        return {"principal_frequency": None, "low_frequency_energy": None,
                "radial_anisotropy": None}

    # principal frequency = first strong peak in the lower half of the spectrum
    half = max(2, nbins // 2)
    f_principal = int(np.argmax(radial[:half])) + 1

    # low-frequency energy: power well below the principal ring vs power at the ring.
    # Blue noise has a clear trough near DC, so this ratio is small (<~0.2).
    lo_band = radial[: max(1, f_principal // 2)]
    ring = radial[max(1, int(0.8 * f_principal)): max(2, int(1.2 * f_principal) + 1)]
    ring_mean = ring.mean() if ring.size else radial.max()
    low_frequency_energy = float(lo_band.mean() / ring_mean) if ring_mean > 0 else None

    # anisotropy: angular variation of power in the principal ring (isotropic -> ~0)
    ring_mask = (rr_int >= max(1, int(0.8 * f_principal))) & \
                (rr_int <= max(2, int(1.2 * f_principal)))
    ring_vals = power[ring_mask]
    radial_anisotropy = float(ring_vals.std() / ring_vals.mean()) if ring_vals.size and ring_vals.mean() > 0 else None

    return {"principal_frequency": f_principal,
            "low_frequency_energy": low_frequency_energy,
            "radial_anisotropy": radial_anisotropy}


def quality_metrics(pts, tree, local, domain, L, r, periodic):
    area = L[0] * L[1]
    cov_max, cov_mean = coverage_radius(tree, domain, L, periodic)
    spec = blue_noise_spectrum(pts, domain, L)

    # convenience scalar in [0,1]; heuristic, documented. The orchestrator should
    # prefer the raw components below and pick its own weighting per research goal.
    lfe = spec["low_frequency_energy"]
    ani = spec["radial_anisotropy"]
    bn = None
    if lfe is not None and ani is not None:
        bn = float(max(0.0, 1.0 - min(lfe, 1.0)) * max(0.0, 1.0 - min(ani, 1.0)))

    return {
        "n_points": int(len(pts)),
        "sample_density": float(len(pts) / area),
        "coverage_radius": cov_max,
        "coverage_radius_over_r": cov_max / r,
        "mean_coverage_distance_over_r": cov_mean / r,
        "spectrum": spec,
        "blue_noise_quality_heuristic": bn,
    }


# --------------------------------------------------------------------- top-level
def analyze(points_path, r, domain, periodic, perf_path=None):
    pts = load_points(points_path)
    gate = correctness_gate(pts, r, domain, periodic)
    if isinstance(gate, dict):                 # early-out (empty / non-finite)
        result = {"gate": gate, "quality": None}
    else:
        gate_report, tree, local, L = gate
        quality = (quality_metrics(pts, tree, local, domain, L, r, periodic)
                   if gate_report["passed"] else None)
        result = {"gate": gate_report, "quality": quality}

    result["config"] = {"radius": r, "domain": domain, "periodic": periodic}
    if perf_path:
        with open(perf_path) as f:
            result["performance"] = json.load(f)
    return result


def _selftest():
    domain = [0.0, 0.0, 1.0, 1.0]
    r = 0.02
    rng = np.random.default_rng(1)

    # GOOD: jittered grid with jitter small enough to guarantee the min-distance property
    s = r * 1.6
    j = 0.10 * s                                   # min spacing >= s - 2j = 0.8s = 1.28r
    g = np.arange(s / 2, 1.0, s)
    gx, gy = np.meshgrid(g, g)
    good = np.stack([gx.ravel(), gy.ravel()], 1)
    good += rng.uniform(-j, j, good.shape)
    good = np.clip(good, 0, 1)

    # BAD: pure white noise -- same count, will violate the conflict radius
    bad = rng.random((len(good), 2))

    np.save("/tmp/_good.npy", good)
    np.save("/tmp/_bad.npy", bad)
    for name, path in [("jittered-grid (should PASS)", "/tmp/_good.npy"),
                       ("white-noise (should FAIL)", "/tmp/_bad.npy")]:
        res = analyze(path, r, domain, periodic=False)
        print(f"\n=== {name} ===")
        print(json.dumps(res, indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--points")
    ap.add_argument("--radius", type=float)
    ap.add_argument("--domain", type=float, nargs=4, metavar=("XMIN", "YMIN", "XMAX", "YMAX"))
    ap.add_argument("--periodic", action="store_true", help="toroidal / wrapping domain")
    ap.add_argument("--perf", help="optional harness perf JSON to merge in")
    ap.add_argument("--out", help="write result JSON here (default: stdout)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        _selftest(); return
    if not (a.points and a.radius and a.domain):
        ap.error("--points, --radius and --domain are required (or use --selftest)")

    res = analyze(a.points, a.radius, a.domain, a.periodic, a.perf)
    out = json.dumps(res, indent=2)
    if a.out:
        with open(a.out, "w") as f:
            f.write(out)
        print(f"wrote {a.out}  (gate: {'PASS' if res['gate'].get('passed') else 'FAIL'})")
    else:
        print(out)


if __name__ == "__main__":
    main()
