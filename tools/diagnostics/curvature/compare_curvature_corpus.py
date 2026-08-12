#!/usr/bin/env python3
"""Run local, non-claim Intrinsic/PMP curvature differentials on OBJ files."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import tempfile
import time
from typing import Iterable


_HEADER = struct.Struct("<8s8Q2d")
_RECORD = struct.Struct("<3f2d9f")
_MINIMUM_RELIABLE_TRIANGLE_QUALITY = 3.5e-4


def _finite(value: float) -> bool:
    return math.isfinite(value)


def _read_probe(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < _HEADER.size:
        raise ValueError("truncated probe header")
    unpacked = _HEADER.unpack_from(data)
    magic = unpacked[0].decode("ascii")
    counts = unpacked[1:9]
    load_ms, compute_ms = unpacked[9:11]
    vertex_count = counts[0]
    expected = _HEADER.size + vertex_count * _RECORD.size
    if len(data) != expected:
        raise ValueError(
            f"probe size mismatch: expected {expected}, got {len(data)}")

    positions: list[tuple[float, float, float]] = []
    minimum: list[float] = []
    maximum: list[float] = []
    direction1: list[tuple[float, float, float]] = []
    direction2: list[tuple[float, float, float]] = []
    geometric_normal: list[tuple[float, float, float]] = []
    offset = _HEADER.size
    for _ in range(vertex_count):
        values = _RECORD.unpack_from(data, offset)
        offset += _RECORD.size
        positions.append(values[0:3])
        minimum.append(values[3])
        maximum.append(values[4])
        direction1.append(values[5:8])
        direction2.append(values[8:11])
        geometric_normal.append(values[11:14])

    if not all(
        _finite(value)
        for channel in (minimum, maximum)
        for value in channel
    ):
        raise ValueError("probe contains non-finite scalar curvature")
    if not all(
        _finite(component)
        for channel in (positions, direction1, direction2, geometric_normal)
        for value in channel
        for component in value
    ):
        raise ValueError("probe contains non-finite vector data")
    return {
        "magic": magic,
        "vertex_count": counts[0],
        "edge_count": counts[1],
        "face_count": counts[2],
        "boundary_vertex_count": counts[3],
        "rejected_triangle_count": counts[4],
        "supported_vertex_count": counts[5],
        "nonzero_vertex_count": counts[6],
        "directions_available": bool(counts[7]),
        "load_ms": load_ms,
        "compute_ms": compute_ms,
        "positions": positions,
        "minimum": minimum,
        "maximum": maximum,
        "direction1": direction1,
        "direction2": direction2,
        "geometric_normal": geometric_normal,
    }


def _position_index(token: bytes, vertex_count: int) -> int | None:
    position = token.split(b"/", 1)[0]
    if not position:
        return None
    try:
        raw = int(position)
    except ValueError:
        return None
    index = raw - 1 if raw > 0 else vertex_count + raw
    return index if 0 <= index < vertex_count else None


def _normalize_obj(source: Path, output: Path, scale: float) -> dict:
    positions: list[tuple[float, float, float]] = []
    polygons: list[list[int]] = []
    malformed_rows = 0
    with source.open("rb") as stream:
        for line in stream:
            stripped = line.lstrip()
            if stripped.startswith(b"v "):
                fields = stripped.split()
                if len(fields) < 4:
                    malformed_rows += 1
                    continue
                try:
                    position = tuple(float(value) * scale for value in fields[1:4])
                except ValueError:
                    malformed_rows += 1
                    continue
                if not all(math.isfinite(value) for value in position):
                    malformed_rows += 1
                    continue
                positions.append(position)  # type: ignore[arg-type]
            elif stripped.startswith(b"f "):
                fields = stripped.split()[1:]
                face = [_position_index(value, len(positions)) for value in fields]
                if len(face) < 3 or any(value is None for value in face):
                    malformed_rows += 1
                    continue
                polygons.append([int(value) for value in face])

    triangles: list[tuple[int, int, int]] = []
    degenerate_triangles = 0
    for face in polygons:
        for corner in range(1, len(face) - 1):
            triangle = (face[0], face[corner], face[corner + 1])
            if len(set(triangle)) != 3:
                degenerate_triangles += 1
                continue
            triangles.append(triangle)
    if not positions or not triangles:
        raise ValueError("no usable position/triangle data")

    with output.open("w", encoding="ascii", newline="\n") as stream:
        for x, y, z in positions:
            stream.write(f"v {x:.9g} {y:.9g} {z:.9g}\n")
        for a, b, c in triangles:
            stream.write(f"f {a + 1} {b + 1} {c + 1}\n")
    return {
        "source_vertex_count": len(positions),
        "source_polygon_count": len(polygons),
        "normalized_triangle_count": len(triangles),
        "malformed_row_count": malformed_rows,
        "degenerate_triangle_count": degenerate_triangles,
        "triangles": triangles,
    }


def _relative_l2(actual: Iterable[float], reference: Iterable[float]) -> float:
    pairs = list(zip(actual, reference, strict=True))
    numerator = math.fsum((a - b) * (a - b) for a, b in pairs)
    denominator = math.fsum(b * b for _, b in pairs)
    return math.sqrt(numerator / denominator) if denominator > 0.0 else math.sqrt(numerator)


def _scalar_metrics(actual: list[float], reference: list[float]) -> dict:
    count = len(reference)
    rms = math.sqrt(math.fsum(value * value for value in reference) / count)
    zero_tolerance = max(1.0e-30, rms * 1.0e-6)
    sign_population = [
        (a, b)
        for a, b in zip(actual, reference, strict=True)
        if abs(a) > zero_tolerance and abs(b) > zero_tolerance
    ]
    max_abs = max(abs(a - b) for a, b in zip(actual, reference, strict=True))
    return {
        "relative_l2": _relative_l2(actual, reference),
        "max_abs": max_abs,
        "max_abs_over_reference_rms": max_abs / rms if rms > 0.0 else max_abs,
        "reference_rms": rms,
        "sign_population": len(sign_population),
        "sign_disagreement_count": sum(
            (a > 0.0) != (b > 0.0) for a, b in sign_population),
    }


def _masked_scalar_metrics(
    actual: list[float], reference: list[float], mask: list[bool]
) -> dict | None:
    selected_actual = [
        value for value, keep in zip(actual, mask, strict=True) if keep]
    selected_reference = [
        value for value, keep in zip(reference, mask, strict=True) if keep]
    if not selected_actual:
        return None
    return _scalar_metrics(selected_actual, selected_reference)


def _sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(a: tuple[float, float, float]) -> float:
    return math.sqrt(_dot(a, a))


def _triangle_quality_summary(
    positions: list[tuple[float, float, float]],
    triangles: list[tuple[int, int, int]],
) -> tuple[dict, list[bool]]:
    qualities: list[float] = []
    reliable: list[bool] = []
    degenerate = 0
    ill_conditioned = 0
    for a, b, c in triangles:
        edge01 = _sub(positions[b], positions[a])
        edge02 = _sub(positions[c], positions[a])
        edge12 = _sub(positions[c], positions[b])
        twice_area = _norm(_cross(edge01, edge02))
        maximum_edge_squared = max(
            _dot(edge01, edge01),
            _dot(edge02, edge02),
            _dot(edge12, edge12),
        )
        if (
            not math.isfinite(twice_area)
            or not math.isfinite(maximum_edge_squared)
            or twice_area <= 0.0
            or maximum_edge_squared <= 0.0
        ):
            degenerate += 1
            reliable.append(False)
            continue
        quality = twice_area / maximum_edge_squared
        qualities.append(quality)
        is_reliable = (
            math.isfinite(quality)
            and quality > _MINIMUM_RELIABLE_TRIANGLE_QUALITY
        )
        if not is_reliable:
            ill_conditioned += 1
        reliable.append(is_reliable)
    return ({
        "definition": "twice_area_over_maximum_edge_squared",
        "reliability_threshold": _MINIMUM_RELIABLE_TRIANGLE_QUALITY,
        "degenerate_triangle_count": degenerate,
        "ill_conditioned_triangle_count": ill_conditioned,
        "minimum_non_degenerate_quality": min(qualities, default=0.0),
        "reliable_triangle_count": sum(reliable),
    }, reliable)


def _quality_uncontaminated_vertex_mask(
    vertex_count: int,
    triangles: list[tuple[int, int, int]],
    reliable_triangles: list[bool],
    propagation_rings: int = 4,
) -> list[bool]:
    adjacency = [set() for _ in range(vertex_count)]
    contaminated: set[int] = set()
    for (a, b, c), reliable in zip(
        triangles, reliable_triangles, strict=True
    ):
        adjacency[a].update((b, c))
        adjacency[b].update((a, c))
        adjacency[c].update((a, b))
        if not reliable:
            contaminated.update((a, b, c))
    frontier = set(contaminated)
    for _ in range(propagation_rings):
        frontier = {
            neighbor
            for vertex in frontier
            for neighbor in adjacency[vertex]
            if neighbor not in contaminated
        }
        contaminated.update(frontier)
    return [index not in contaminated for index in range(vertex_count)]


def _direction_metrics(probe: dict) -> dict:
    unit_error: list[float] = []
    orthogonality: list[float] = []
    tangency: list[float] = []
    for index, (direction1, direction2) in enumerate(
        zip(probe["direction1"], probe["direction2"], strict=True)
    ):
        length1 = _norm(direction1)
        length2 = _norm(direction2)
        normal = probe["geometric_normal"][index]
        normal_length = _norm(normal)
        if length1 <= 1.0e-12 or length2 <= 1.0e-12 or normal_length <= 1.0e-20:
            continue
        unit_error.extend((abs(length1 - 1.0), abs(length2 - 1.0)))
        orthogonality.append(abs(_dot(direction1, direction2)) / (length1 * length2))
        tangency.extend((
            abs(_dot(direction1, normal)) / (length1 * normal_length),
            abs(_dot(direction2, normal)) / (length2 * normal_length),
        ))

    def rms(values: list[float]) -> float:
        return math.sqrt(math.fsum(value * value for value in values) / len(values)) if values else 0.0

    return {
        "valid_direction_vertex_count": len(orthogonality),
        "unit_length_error_rms": rms(unit_error),
        "orthogonality_abs_dot_rms": rms(orthogonality),
        "normal_abs_dot_rms": rms(tangency),
        "unit_length_error_max": max(unit_error, default=0.0),
        "orthogonality_abs_dot_max": max(orthogonality, default=0.0),
        "normal_abs_dot_max": max(tangency, default=0.0),
    }


def _run(command: list[str], timeout: float) -> tuple[int, str, float]:
    begin = time.monotonic()
    completed = subprocess.run(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )
    return completed.returncode, completed.stderr[-2000:], time.monotonic() - begin


def _compare_model(
    source: Path,
    root: Path,
    intrinsic: Path,
    pmp: Path,
    repetitions: int,
    timeout: float,
    work: Path,
    scale: float = 1.0,
) -> dict:
    relative = source.relative_to(root).as_posix()
    key = hashlib.sha256(f"{relative}:{scale}".encode()).hexdigest()[:16]
    normalized = work / f"{key}.obj"
    intrinsic_result = work / f"{key}.intrinsic.bin"
    pmp_result = work / f"{key}.pmp.bin"
    row = {
        "path": relative,
        "source_bytes": source.stat().st_size,
        "scale": scale,
    }
    try:
        normalization = _normalize_obj(source, normalized, scale)
        triangles = normalization.pop("triangles")
        row["normalization"] = normalization
    except (OSError, ValueError) as error:
        row.update(status="normalization_failed", diagnostic=str(error))
        return row

    commands = (
        ("intrinsic", intrinsic, intrinsic_result),
        ("pmp", pmp, pmp_result),
    )
    for name, executable, output in commands:
        try:
            code, stderr, wall_seconds = _run(
                [str(executable), str(normalized), str(output), str(repetitions)],
                timeout,
            )
        except subprocess.TimeoutExpired:
            row.update(status=f"{name}_timeout", diagnostic=f"timeout after {timeout}s")
            return row
        row[f"{name}_process_seconds"] = wall_seconds
        if code != 0:
            row.update(status=f"{name}_failed", diagnostic=stderr, exit_code=code)
            return row

    try:
        actual = _read_probe(intrinsic_result)
        reference = _read_probe(pmp_result)
    except (OSError, ValueError) as error:
        row.update(status="result_invalid", diagnostic=str(error))
        return row

    row["intrinsic"] = {
        key: value for key, value in actual.items()
        if key not in {
            "positions", "minimum", "maximum", "direction1", "direction2",
            "geometric_normal"}
    }
    row["pmp"] = {
        key: value for key, value in reference.items()
        if key not in {
            "positions", "minimum", "maximum", "direction1", "direction2",
            "geometric_normal"}
    }
    topology_fields = (
        "vertex_count", "edge_count", "face_count", "boundary_vertex_count")
    mismatches = {
        field: [actual[field], reference[field]]
        for field in topology_fields
        if actual[field] != reference[field]
    }
    if actual["positions"] != reference["positions"]:
        mismatches["position_slots"] = [len(actual["positions"]), len(reference["positions"])]
    if mismatches:
        row.update(status="topology_mismatch", topology_mismatches=mismatches)
        return row

    mesh_quality, reliable_triangles = _triangle_quality_summary(
        actual["positions"], triangles)
    row["mesh_quality"] = mesh_quality
    minimum = _scalar_metrics(actual["minimum"], reference["minimum"])
    maximum = _scalar_metrics(actual["maximum"], reference["maximum"])
    actual_mean = [0.5 * (a + b) for a, b in zip(actual["minimum"], actual["maximum"], strict=True)]
    reference_mean = [0.5 * (a + b) for a, b in zip(reference["minimum"], reference["maximum"], strict=True)]
    actual_gaussian = [a * b for a, b in zip(actual["minimum"], actual["maximum"], strict=True)]
    reference_gaussian = [a * b for a, b in zip(reference["minimum"], reference["maximum"], strict=True)]
    # Published directions are zero exactly where the tensor estimator has no
    # reliable support. This subset separates faithful PMP parity on supported
    # vertices from deliberate fail-closed zeros around malformed faces. Full-
    # field metrics remain present and are never replaced by this diagnostic.
    supported_mask = [
        _norm(direction1) > 0.5 and _norm(direction2) > 0.5
        for direction1, direction2 in zip(
            actual["direction1"], actual["direction2"], strict=True)
    ]
    uncontaminated_mask = _quality_uncontaminated_vertex_mask(
        actual["vertex_count"], triangles, reliable_triangles)
    row.update(
        status="compared",
        scalar_metrics={
            "minimum": minimum,
            "maximum": maximum,
            "mean": _scalar_metrics(actual_mean, reference_mean),
            "gaussian": _scalar_metrics(actual_gaussian, reference_gaussian),
        },
        direction_supported_subset={
            "vertex_count": sum(supported_mask),
            "scalar_metrics": {
                "minimum": _masked_scalar_metrics(
                    actual["minimum"], reference["minimum"], supported_mask),
                "maximum": _masked_scalar_metrics(
                    actual["maximum"], reference["maximum"], supported_mask),
                "mean": _masked_scalar_metrics(
                    actual_mean, reference_mean, supported_mask),
                "gaussian": _masked_scalar_metrics(
                    actual_gaussian, reference_gaussian, supported_mask),
            },
        },
        quality_uncontaminated_subset={
            "definition": (
                "vertices_more_than_four_adjacency_rings_from_a_"
                "triangle_at_or_below_the_quality_threshold"
            ),
            "vertex_count": sum(uncontaminated_mask),
            "scalar_metrics": {
                "minimum": _masked_scalar_metrics(
                    actual["minimum"], reference["minimum"],
                    uncontaminated_mask),
                "maximum": _masked_scalar_metrics(
                    actual["maximum"], reference["maximum"],
                    uncontaminated_mask),
                "mean": _masked_scalar_metrics(
                    actual_mean, reference_mean, uncontaminated_mask),
                "gaussian": _masked_scalar_metrics(
                    actual_gaussian, reference_gaussian,
                    uncontaminated_mask),
            },
        },
        direction_invariants=_direction_metrics(actual),
    )
    return row


def _bucket(size: int) -> int:
    if size < 100_000:
        return 0
    if size < 1_000_000:
        return 1
    if size < 10_000_000:
        return 2
    if size < 100_000_000:
        return 3
    return 4


def _select_models(
    root: Path,
    maximum: int,
    explicit: list[str],
    max_source_bytes: int,
) -> list[Path]:
    all_files = [
        path for path in root.rglob("*.obj")
        if not path.name.startswith("._") and path.is_file()
    ]
    files = [path for path in all_files if path.stat().st_size <= max_source_bytes]
    by_relative = {path.relative_to(root).as_posix(): path for path in all_files}
    selected: list[Path] = []
    for name in explicit:
        candidate = by_relative.get(name)
        if candidate is None:
            candidate = next((path for path in all_files if path.name == name), None)
        if candidate is None:
            raise ValueError(f"explicit model not found: {name}")
        if candidate not in selected:
            selected.append(candidate)

    buckets: list[list[Path]] = [[] for _ in range(5)]
    for path in files:
        buckets[_bucket(path.stat().st_size)].append(path)
    weights = (0.15, 0.25, 0.30, 0.25, 0.05)
    remaining = max(0, maximum - len(selected))
    quotas = [round(remaining * weight) for weight in weights]
    quotas[2] += remaining - sum(quotas)
    for paths, quota in zip(buckets, quotas, strict=True):
        paths.sort(key=lambda path: hashlib.sha256(
            path.relative_to(root).as_posix().encode()).digest())
        for path in paths[:quota]:
            if path not in selected:
                selected.append(path)
    if len(selected) < maximum:
        leftovers = sorted(
            (path for path in files if path not in selected),
            key=lambda path: hashlib.sha256(
                path.relative_to(root).as_posix().encode()).digest())
        selected.extend(leftovers[: maximum - len(selected)])
    return selected[:maximum]


def _aggregate(rows: list[dict]) -> dict:
    compared = [row for row in rows if row["status"] == "compared"]
    failures: dict[str, int] = {}
    for row in rows:
        failures[row["status"]] = failures.get(row["status"], 0) + 1
    if not compared:
        return {"status_counts": failures, "compared_count": 0}

    def values(channel: str, metric: str) -> list[float]:
        return [row["scalar_metrics"][channel][metric] for row in compared]

    conditioned = [
        row for row in compared
        if row["mesh_quality"]["degenerate_triangle_count"] == 0
        and row["mesh_quality"]["ill_conditioned_triangle_count"] == 0
    ]

    def conditioned_values(channel: str, metric: str) -> list[float]:
        return [row["scalar_metrics"][channel][metric] for row in conditioned]

    supported = [
        row for row in compared
        if row["direction_supported_subset"]["vertex_count"] > 0
    ]

    def supported_values(channel: str, metric: str) -> list[float]:
        return [
            row["direction_supported_subset"]["scalar_metrics"][channel][metric]
            for row in supported
            if row["direction_supported_subset"]["scalar_metrics"][channel]
            is not None
        ]

    uncontaminated = [
        row for row in compared
        if row["quality_uncontaminated_subset"]["vertex_count"] > 0
    ]

    def uncontaminated_values(channel: str, metric: str) -> list[float]:
        return [
            row["quality_uncontaminated_subset"]["scalar_metrics"][channel][metric]
            for row in uncontaminated
            if row["quality_uncontaminated_subset"]["scalar_metrics"][channel]
            is not None
        ]

    ratios = [
        row["intrinsic"]["compute_ms"] / row["pmp"]["compute_ms"]
        for row in compared if row["pmp"]["compute_ms"] > 0.0
    ]
    return {
        "status_counts": failures,
        "compared_count": len(compared),
        "compared_vertex_count": sum(row["intrinsic"]["vertex_count"] for row in compared),
        "minimum_relative_l2_median": statistics.median(values("minimum", "relative_l2")),
        "minimum_relative_l2_max": max(values("minimum", "relative_l2")),
        "maximum_relative_l2_median": statistics.median(values("maximum", "relative_l2")),
        "maximum_relative_l2_max": max(values("maximum", "relative_l2")),
        "mean_relative_l2_max": max(values("mean", "relative_l2")),
        "gaussian_relative_l2_max": max(values("gaussian", "relative_l2")),
        "well_conditioned_model_count": len(conditioned),
        "well_conditioned_minimum_relative_l2_max": max(
            conditioned_values("minimum", "relative_l2"), default=0.0),
        "well_conditioned_maximum_relative_l2_max": max(
            conditioned_values("maximum", "relative_l2"), default=0.0),
        "direction_supported_vertex_count": sum(
            row["direction_supported_subset"]["vertex_count"]
            for row in compared),
        "direction_supported_minimum_relative_l2_max": max(
            supported_values("minimum", "relative_l2"), default=0.0),
        "direction_supported_maximum_relative_l2_max": max(
            supported_values("maximum", "relative_l2"), default=0.0),
        "quality_uncontaminated_vertex_count": sum(
            row["quality_uncontaminated_subset"]["vertex_count"]
            for row in compared),
        "quality_uncontaminated_minimum_relative_l2_max": max(
            uncontaminated_values("minimum", "relative_l2"), default=0.0),
        "quality_uncontaminated_maximum_relative_l2_max": max(
            uncontaminated_values("maximum", "relative_l2"), default=0.0),
        "degenerate_triangle_count": sum(
            row["mesh_quality"]["degenerate_triangle_count"]
            for row in compared),
        "ill_conditioned_triangle_count": sum(
            row["mesh_quality"]["ill_conditioned_triangle_count"]
            for row in compared),
        "intrinsic_to_pmp_compute_ratio_median": statistics.median(ratios),
        "intrinsic_to_pmp_compute_ratio_min": min(ratios),
        "intrinsic_to_pmp_compute_ratio_max": max(ratios),
        "direction_unit_error_rms_max": max(
            row["direction_invariants"]["unit_length_error_rms"] for row in compared),
        "direction_orthogonality_rms_max": max(
            row["direction_invariants"]["orthogonality_abs_dot_rms"] for row in compared),
        "direction_tangency_rms_max": max(
            row["direction_invariants"]["normal_abs_dot_rms"] for row in compared),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-root", type=Path, required=True)
    parser.add_argument("--intrinsic", type=Path, required=True)
    parser.add_argument("--pmp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-models", type=int, default=96)
    parser.add_argument("--model", action="append", default=[])
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--max-source-bytes", type=int, default=100_000_000)
    args = parser.parse_args()
    if args.max_models <= 0 or args.repetitions <= 0 or args.max_source_bytes <= 0:
        parser.error("model and repetition counts must be positive")
    if not math.isfinite(args.scale) or args.scale <= 0.0:
        parser.error("scale must be finite and positive")

    root = args.dataset_root.resolve()
    intrinsic = args.intrinsic.resolve()
    pmp = args.pmp.resolve()
    models = _select_models(
        root, args.max_models, args.model, args.max_source_bytes)
    rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="intrinsic-curvature-corpus-") as directory:
        work = Path(directory)
        for index, model in enumerate(models, start=1):
            row = _compare_model(
                model, root, intrinsic, pmp, args.repetitions,
                args.timeout, work, args.scale)
            rows.append(row)
            print(
                f"[{index}/{len(models)}] {row['status']}: {row['path']}",
                flush=True)

    payload = {
        "schema_version": 1,
        "claim_eligible": False,
        "evidence_class": "local-development-diagnostic",
        "dataset_root": str(root),
        "dataset_obj_count": sum(
            1 for path in root.rglob("*.obj")
            if not path.name.startswith("._") and path.is_file()),
        "selection": {
            "maximum_models": args.max_models,
            "explicit_models": args.model,
            "selected_count": len(models),
            "scale": args.scale,
            "repetitions": args.repetitions,
            "timeout_seconds": args.timeout,
            "max_source_bytes": args.max_source_bytes,
        },
        "aggregate": _aggregate(rows),
        "models": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload["aggregate"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
