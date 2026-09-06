#!/usr/bin/env python3
"""Run the declared local OBJ cohort with source/input hashes and bounded processes."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

COHORT = ("sculpt", "frog", "fandisk", "bunny10k", "dolphin", "elephant", "bumpy_torus", "genus3", "office_chair", "sphere", "cube", "saddle2")

def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--dataset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--meshes", nargs="+", choices=COHORT, default=list(COHORT))
    parser.add_argument("--modes", nargs="+", choices=("multicut", "regional", "contrast", "clean", "clean_medium", "clean_coarse", "curves", "local"), default=["multicut", "local"])
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; use a new append-only run directory")
    args.output.mkdir(parents=True)
    runner = args.runner.resolve()
    source_root = Path(__file__).resolve().parents[2]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source_root, text=True).strip()
    changed = subprocess.check_output(["git", "status", "--porcelain"], cwd=source_root, text=True)
    source_paths = ["src/geometry/CurvatureBoundaryGraph.cpp", "src/geometry/CurvatureBoundaryGraph.hpp", "src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut.cpp", "src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut.cppm", "benchmarks/runners/CurvatureBoundaryMeshRunner.cpp"]
    record = {"schema": 1, "candidates": {"multicut": "boundary_multicut_isotropic_v2", "regional": "curvature_region_scale_v2", "contrast": "boundary_feature_contrast_v1", "clean": "boundary_feature_area_cleanup_v1", "clean_medium": "boundary_feature_area_cleanup_v1", "clean_coarse": "boundary_feature_area_cleanup_v1", "curves": "boundary_feature_curve_coverage_v1", "local": "method_039_local_patch_unadopted"}, "source_revision": revision, "source_dirty": bool(changed), "source_hashes": {p: digest(source_root / p) for p in source_paths}, "runner_sha256": digest(runner), "claim_eligible": False, "timeout_seconds": args.timeout, "inputs": {}, "runs": []}
    # Bind every input before the first candidate output, including missing files.
    for name in args.meshes:
        path = args.dataset_root / (name + ".obj")
        record["inputs"][name] = {"path": str(path.resolve()), "sha256": digest(path) if path.is_file() else None}
    index = args.output / "cohort.json"
    index.write_text(json.dumps(record, indent=2) + "\n")
    for name in args.meshes:
        for mode in args.modes:
            prefix = args.output / (name + "-" + mode)
            command = [str(runner), str(args.dataset_root / (name + ".obj")), str(prefix), mode]
            started = time.monotonic()
            with prefix.with_suffix(".stdout.log").open("w") as out, prefix.with_suffix(".stderr.log").open("w") as err:
                try:
                    result = subprocess.run(command, stdout=out, stderr=err, timeout=args.timeout, check=False)
                    exit_code = result.returncode
                except subprocess.TimeoutExpired:
                    exit_code = "timeout"
            row = {"mesh": name, "mode": mode, "command": command, "exit_code": exit_code, "elapsed_seconds": time.monotonic() - started}
            payload_path = prefix.with_suffix(".json")
            if payload_path.exists():
                try:
                    payload = json.loads(payload_path.read_text())
                    row["result"] = payload
                    actual = payload.get("diagnostics", {}).get("implementation")
                    expected = record["candidates"][mode]
                    if exit_code == 0 and actual != expected:
                        row["identity_mismatch"] = {"expected": expected, "actual": actual}
                except ValueError:
                    row["malformed_result"] = True
            row["output_sha256"] = {suffix: digest(Path(str(prefix) + suffix))
                for suffix in (".json", ".labels", ".edges", ".geometry.json")
                if Path(str(prefix) + suffix).is_file()}
            record["runs"].append(row)
            index.write_text(json.dumps(record, indent=2) + "\n")
            d = row.get("result", {}).get("diagnostics", {})
            print(json.dumps({"mesh": name, "mode": mode, "exit": exit_code, "regions": row.get("result", {}).get("metrics", {}).get("population_count"), "partition_ms": d.get("partition_ms"), "status": row.get("result", {}).get("diagnostic", row.get("result", {}).get("status"))}), flush=True)
    record["runner_unchanged"] = digest(runner) == record["runner_sha256"]
    record["inputs_unchanged"] = all(
        (digest(Path(value["path"])) if Path(value["path"]).is_file() else None) == value["sha256"]
        for value in record["inputs"].values())
    index.write_text(json.dumps(record, indent=2) + "\n")
    valid = all(row["exit_code"] == 0 and "identity_mismatch" not in row
                and row.get("result", {}).get("status") == "passed"
                for row in record["runs"])
    return 0 if valid and record["runner_unchanged"] and record["inputs_unchanged"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
