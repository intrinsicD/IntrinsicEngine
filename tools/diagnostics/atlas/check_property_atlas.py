#!/usr/bin/env python3
"""Replay the native property-region atlas on fixed, repository-owned inputs.

This is an explicit correctness campaign. Timings are diagnostic, not a speed
claim. It reuses the native atlas runner and canonical benchmark sealer; frozen
METHOD-045 input geometry and region labels remain untouched.
"""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import yaml

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/benchmark"))
from seal_benchmark_results import canonicalize


def controls():
    from controls import fold

    vertices, faces = fold(17)
    regions = (vertices[faces].mean(axis=1)[:, 2] > 0).astype(int)
    yield "folded_grid", vertices, faces, regions
    vertices = vertices.copy()
    vertices[:, 0] += vertices[:, 2]
    vertices[:, 2] = 0
    yield "planar_grid", vertices, faces, regions
    yield "disconnected_triangles", np.array([
        [0, 0, 0], [1, 0, 0], [0, 1, 0],
        [3, 0, 0], [4, 0, 0], [3, 1, 0]], dtype=float), np.array([
            [0, 1, 2], [3, 4, 5]], dtype=int), np.array([0, 0])


def corpus():
    directory = ROOT / "ara/evidence/diagnostics/method045/baselines"
    for name in ("sculpt", "frog", "fandisk", "dolphin"):
        source = json.loads(gzip.decompress((directory / (name + ".geometry.json.gz")).read_bytes()))
        yield name, np.asarray(source["positions"]), np.asarray(source["triangles"]), np.loadtxt(
            directory / (name + ".labels"), dtype=np.uint32)


def audit(source, result):
    """Recompute orientation, density and metric eigenvalues from published UVs."""
    faces = np.asarray(source["faces"], dtype=np.int64)
    positions = np.asarray(source["vertices"], dtype=np.float64)
    corners = np.asarray(result.get("source_corner_uvs", []), dtype=np.float64)
    charts = np.asarray(result.get("source_face_charts", []), dtype=np.int64)
    if corners.shape != (3 * len(faces), 2) or charts.shape != (len(faces),):
        return {"passed": False, "reason": "source corner/face coverage mismatch"}
    corners = corners.reshape((-1, 3, 2))
    if not np.isfinite(corners).all() or np.any(corners < 0) or np.any(corners > 1):
        return {"passed": False, "reason": "nonfinite or out-of-bounds UV"}
    pixel = corners * np.array([result["atlas_width"], result["atlas_height"]])
    e1 = positions[faces[:, 1]] - positions[faces[:, 0]]
    e2 = positions[faces[:, 2]] - positions[faces[:, 0]]
    length = np.linalg.norm(e1, axis=1)
    double_area = np.linalg.norm(np.cross(e1, e2), axis=1)
    x = np.einsum("ij,ij->i", e1, e2) / length
    y = double_area / length
    q1, q2 = pixel[:, 1] - pixel[:, 0], pixel[:, 2] - pixel[:, 0]
    signed = q1[:, 0] * q2[:, 1] - q1[:, 1] * q2[:, 0]
    if np.any(signed <= 0) or np.any(double_area <= 0):
        return {"passed": False, "reason": "non-positive source or UV area"}
    density = np.sqrt(signed.sum() / double_area.sum())
    a = q1 / length[:, None] / density
    b = (q2 - q1 * (x / length)[:, None]) / y[:, None] / density
    aa = np.einsum("ij,ij->i", a, a)
    bb = np.einsum("ij,ij->i", b, b)
    ab = np.einsum("ij,ij->i", a, b)
    hi = np.sqrt((aa + bb + np.hypot(aa - bb, 2 * ab)) / 2)
    # Use the determinant for the small singular value to avoid cancellation.
    determinant = signed / double_area / (density * density)
    lo = determinant / hi
    conformal = hi / lo
    area = np.maximum(determinant, 1 / determinant)
    weights = double_area / double_area.sum()
    labels = np.asarray(source["face_regions"])
    crossing = sum(len(np.unique(labels[charts == chart])) != 1 for chart in np.unique(charts))
    passed = (crossing == 0 and np.isfinite(conformal).all() and np.isfinite(area).all()
              and conformal.max() <= source["max_conformal_distortion"] * (1 + 1e-6)
              and area.max() <= source["max_area_distortion"] * (1 + 1e-6))
    order = np.argsort(conformal)
    p95 = conformal[order[np.searchsorted(np.cumsum(weights[order]), 0.95)]]
    return {"passed": bool(passed), "region_crossings": int(crossing),
            "max_conformal": float(conformal.max()), "mean_conformal": float(weights @ conformal),
            "p95_conformal": float(p95), "max_area": float(area.max()),
            "mean_area": float(weights @ area), "texels_per_unit": float(density),
            "overlap_check": "independent native BVH validator; no Python all-pairs pass"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--runner", type=Path, default=ROOT / "build/ci/bin/IntrinsicUvAtlasMeshDiagnostic")
    parser.add_argument("--corpus", action="store_true")
    parser.add_argument("--cases", nargs="+")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    results_dir = args.output / "results"
    results_dir.mkdir()
    with args.runner.open("rb") as binary:
        runner_sha256 = hashlib.file_digest(binary, "sha256").hexdigest()
    # Retain the exact build-input patch, including new files. A digest alone
    # cannot reconstruct a dirty run after the worktree has changed.
    source_paths = ["src", "tests", "assets/shaders", "methods", "benchmarks", "cmake", "tools",
                    "CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "vcpkg-configuration.json"]
    diff = subprocess.check_output(["git", "diff", "HEAD", "--binary", "--", *source_paths], cwd=ROOT)
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z", "--", *source_paths], cwd=ROOT)
    for name in sorted(filter(None, untracked.decode().split("\0"))):
        addition = subprocess.run(["git", "diff", "--no-index", "--binary", "--", "/dev/null", name],
                                  cwd=ROOT, capture_output=True, check=False)
        if addition.returncode not in (0, 1):
            raise RuntimeError(addition.stderr.decode())
        diff += addition.stdout
    diff_sha256 = hashlib.sha256(diff).hexdigest()
    (args.output / "source.patch").write_bytes(diff)
    manifest_path = ROOT / "benchmarks/geometry/manifests/geometry_property_guided_atlas_reference.yaml"
    manifest = yaml.safe_load(manifest_path.read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    (args.output / "source-provenance.json").write_text(json.dumps(dict(
        base_revision=revision, source_paths=source_paths, patch_sha256=diff_sha256,
        runner_sha256=runner_sha256, command=sys.argv), indent=2) + "\n")
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    rows = []
    sources = list(controls()) + (list(corpus()) if args.corpus else [])
    for name, vertices, faces, regions in sources:
        if args.cases and name not in args.cases:
            continue
        for method, objective in [("fast-staged", value) for value in ("none", "angle", "area", "both")] + [("xatlas", "angle")]:
            cell = name + "-" + method + "-" + objective
            directory = args.output / cell
            directory.mkdir()
            source = dict(vertices=vertices.tolist(), faces=faces.tolist(), face_regions=regions.tolist(),
                          distortion=objective, resolution=1024, padding=2, max_conformal_distortion=10.0,
                          max_area_distortion=10.0, max_charts=16384, max_iterations=40)
            input_path, output_path = directory / "input.json", directory / "native.json"
            input_path.write_text(json.dumps(source, allow_nan=False) + "\n")
            try:
                call = subprocess.run([str(args.runner.resolve()), str(input_path.resolve()), str(output_path.resolve()), method],
                                      capture_output=True, text=True, timeout=args.timeout)
                (directory / "stdout.log").write_text(call.stdout)
                (directory / "stderr.log").write_text(call.stderr)
                result = json.loads(output_path.read_text()) if output_path.exists() else {}
                check = audit(source, result) if call.returncode == 0 else {"passed": False, "reason": result.get("diagnostic", "runner failed")}
            except subprocess.TimeoutExpired:
                result, check = {}, {"passed": False, "reason": "bounded campaign timeout"}
            passed = bool(check["passed"] and result.get("validation", {}).get("passed"))
            raw = dict(benchmark_id=manifest["benchmark_id"], method=manifest["method"], dataset=manifest["dataset"],
                       backend="cpu_reference", commit=revision, status="passed" if passed else "failed",
                       metrics=dict(quality_error_linf=0.0 if passed else 1.0,
                                    runtime_ms=result.get("runtime_seconds", 0.0) * 1000),
                       diagnostics=dict(case=cell, chart_count=result.get("chart_count", 0), requested_method=method, requested_distortion=objective,
                                        runner_sha256=runner_sha256, source_sha256=hashlib.sha256(input_path.read_bytes()).hexdigest(),
                                        independent_audit=check, native_status=result.get("status", "not_completed"),
                                        limitation="One debug correctness run; timings do not establish a performance advantage."))
            sealed = canonicalize(raw, manifest_path=manifest_path, manifest=manifest, manifests_root=ROOT / "benchmarks",
                                  run_id=args.output.name + "-" + cell, attempt_id="attempt-001",
                                  source_state="dirty_worktree" if dirty else "clean_commit", source_revision=revision,
                                  claim_eligible=False, snapshot_sha256=None, diff_sha256=diff_sha256 if dirty else None)
            (results_dir / (cell + ".json")).write_text(json.dumps(sealed, indent=2, allow_nan=False) + "\n")
            row = {"case": cell, "passed": passed, "status": result.get("status"), "audit": check}
            rows.append(row)
            (args.output / "summary.json").write_text(json.dumps(rows, indent=2, allow_nan=False) + "\n")
            print(cell, "passed" if passed else "FAILED", result.get("chart_count"), flush=True)
    return 0 if rows and all(row["passed"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
