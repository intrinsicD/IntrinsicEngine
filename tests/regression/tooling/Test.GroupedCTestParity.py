#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from collections import Counter
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from types import ModuleType
from typing import Iterable, Mapping, Sequence


def _load_gate_routing() -> ModuleType:
    path = Path(__file__).with_name("Test.TestGateRouting.py")
    spec = importlib.util.spec_from_file_location("intrinsic_test_gate_routing", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load gate-routing helpers from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


GATE = _load_gate_routing()
AGGREGATE = "IntrinsicCpuTests"
GROUPED_TARGETS = frozenset(GATE.GROUPED_PURE_CTEST_TARGETS)
VALID_STATUSES = frozenset({"passed", "skipped", "failed", "error", "disabled"})
MAX_REPORT_AGE_SECONDS = 3600
Identity = tuple[str, str]


class ParityError(RuntimeError):
    pass


@dataclass(frozen=True)
class Registration:
    build_dir: Path
    physical_names: frozenset[str]
    cases: Mapping[str, tuple[str, ...]]
    representations: Mapping[str, str]
    ctest_cases: Mapping[str, Identity]
    wrappers: Mapping[str, str]

    @property
    def identities(self) -> frozenset[Identity]:
        return frozenset(
            (target, case) for target, cases in self.cases.items() for case in cases
        )


def _sample(values: Iterable[object], limit: int = 6) -> str:
    rendered = sorted(str(value) for value in values)
    suffix = f", ... (+{len(rendered) - limit})" if len(rendered) > limit else ""
    return f"[{', '.join(repr(value) for value in rendered[:limit])}{suffix}]"


def _digest(rows: Iterable[Sequence[str]]) -> str:
    digest = hashlib.sha256()
    for row in sorted(tuple(row) for row in rows):
        digest.update(json.dumps(row, separators=(",", ":")).encode())
        digest.update(b"\n")
    return digest.hexdigest()


def _registration_records(
    document: Mapping[str, object],
    build_dir: Path,
    source_targets: frozenset[str],
    aggregate_members: frozenset[str],
) -> tuple[frozenset[str], dict[str, str], dict[str, Identity], dict[str, str]]:
    path_to_target = {
        GATE._resolved_path(str(candidate)): target
        for target in source_targets
        for candidate in GATE._candidate_binary_paths(build_dir, target)
    }
    physical_names: set[str] = set()
    representations: dict[str, str] = {}
    ctest_cases: dict[str, Identity] = {}
    wrappers: dict[str, str] = {}
    all_names: set[str] = set()
    for record in document["tests"]:
        if not isinstance(record, dict) or not isinstance(record.get("name"), str):
            raise ParityError("CTest document contains a malformed test record")
        name = record["name"]
        if name in all_names:
            raise ParityError(f"CTest document repeats test name {name!r}")
        all_names.add(name)
        if GATE._cpu(GATE._ctest_labels(record)):
            physical_names.add(name)
        command = record.get("command")
        if not isinstance(command, list) or not command or not isinstance(command[0], str):
            continue
        target = path_to_target.get(GATE._resolved_path(command[0]))
        if target not in aggregate_members:
            continue
        if name == f"{target}.Grouped":
            kind = "grouped"
            wrappers[target] = name
        elif "--gtest_also_run_disabled_tests" in command:
            kind = "discovered"
            filters = [
                value.removeprefix("--gtest_filter=")
                for value in command
                if isinstance(value, str) and value.startswith("--gtest_filter=")
            ]
            if len(filters) != 1 or not filters[0]:
                raise ParityError(f"{name!r} does not have one raw GoogleTest filter")
            if name in ctest_cases:
                raise ParityError(f"CTest display name {name!r} is duplicated")
            ctest_cases[name] = (target, filters[0])
        else:
            continue
        previous = representations.setdefault(target, kind)
        if previous != kind:
            raise ParityError(
                f"{target} mixes {previous!r} and {kind!r} representations"
            )
    return frozenset(physical_names), representations, ctest_cases, wrappers


def _load_registration(build_dir: Path, grouped: bool) -> Registration:
    build_dir = build_dir.resolve()
    actual_grouped = GATE._cache_bool(build_dir, "INTRINSIC_GROUP_PURE_CTEST")
    if actual_grouped != grouped:
        expected = "ON" if grouped else "OFF"
        raise ParityError(f"{build_dir}: INTRINSIC_GROUP_PURE_CTEST must be {expected}")
    _target_count, case_count, _source_count = GATE.reconcile(build_dir, AGGREGATE)
    inventory = build_dir / "test-inventories"
    targets = GATE._read_target_registry(inventory / "RegisteredTestTargets.tsv")
    sources = GATE._read_source_registry(inventory / "RegisteredTestSources.tsv")
    members = GATE._read_aggregate(inventory / f"{AGGREGATE}.txt")
    source_targets = GATE._classify_registered_targets(targets, sources)
    document = GATE._read_ctest_document(build_dir)
    expected_grouped = GROUPED_TARGETS if grouped else frozenset()
    cases_by_target = GATE._registered_gtest_cases_by_target(
        document,
        build_dir,
        source_targets,
        targets,
        AGGREGATE,
        members,
        expected_grouped,
    )
    selected_targets = frozenset(members.intersection(source_targets))
    cases = {
        target: tuple(cases_by_target[target]) for target in sorted(selected_targets)
    }
    physical, representations, ctest_cases, wrappers = _registration_records(
        document, build_dir, source_targets, selected_targets
    )
    identities = [
        (target, case)
        for target, target_cases in cases.items()
        for case in target_cases
    ]
    if len(identities) != len(set(identities)):
        raise ParityError("logical GoogleTest identities are duplicated")
    if len(identities) != case_count:
        raise ParityError(
            f"{build_dir}: gate routing counted {case_count} cases, "
            f"cross-tree inventory found {len(identities)}"
        )
    if set(representations) != set(selected_targets):
        raise ParityError(
            f"{build_dir}: missing producer representations "
            f"{_sample(selected_targets - representations.keys())}"
        )
    return Registration(
        build_dir,
        physical,
        cases,
        representations,
        ctest_cases,
        wrappers,
    )


def _compare_registrations(
    individual: Registration, grouped: Registration
) -> dict[str, object]:
    missing = individual.identities - grouped.identities
    extra = grouped.identities - individual.identities
    if missing or extra:
        raise ParityError(
            "logical registration mismatch; "
            f"missing={_sample(missing)}, extra={_sample(extra)}"
        )

    changed = {
        target
        for target in individual.representations
        if individual.representations[target] != grouped.representations.get(target)
    }
    if changed != GROUPED_TARGETS:
        raise ParityError(
            "only the canonical grouped producers may change representation; "
            f"missing={_sample(GROUPED_TARGETS - changed)}, "
            f"extra={_sample(changed - GROUPED_TARGETS)}"
        )
    for target, kind in individual.representations.items():
        expected = "grouped" if target in GROUPED_TARGETS else "discovered"
        if kind != "discovered" or grouped.representations.get(target) != expected:
            raise ParityError(f"{target}: invalid individual/grouped representation")

    grouped_identities = frozenset(
        identity
        for identity in individual.identities
        if identity[0] in GROUPED_TARGETS
    )
    if frozenset(individual.ctest_cases.values()) != individual.identities:
        raise ParityError("individual CTest records do not cover every logical case")
    expected_grouped_ctest = individual.identities - grouped_identities
    if frozenset(grouped.ctest_cases.values()) != expected_grouped_ctest:
        raise ParityError("grouped CTest records do not exactly cover unaffected cases")
    if set(grouped.wrappers) != set(GROUPED_TARGETS):
        raise ParityError("grouped CTest wrapper set is incomplete")

    replaced_names = frozenset(
        name
        for name, identity in individual.ctest_cases.items()
        if identity[0] in GROUPED_TARGETS
    )
    expected_physical = (
        individual.physical_names - replaced_names
    ) | frozenset(grouped.wrappers.values())
    if grouped.physical_names != expected_physical:
        raise ParityError(
            "physical CTest selection does not match replacement-only grouping; "
            f"missing={_sample(expected_physical - grouped.physical_names)}, "
            f"extra={_sample(grouped.physical_names - expected_physical)}"
        )
    return {
        "mode": "registration",
        "aggregate": AGGREGATE,
        "logical_cases": len(individual.identities),
        "logical_digest": _digest(individual.identities),
        "grouped_logical_cases": len(grouped_identities),
        "grouped_producers": sorted(GROUPED_TARGETS),
        "individual_physical_ctest_records": len(individual.physical_names),
        "grouped_physical_ctest_records": len(grouped.physical_names),
        "physical_process_reduction": (
            len(individual.physical_names) - len(grouped.physical_names)
        ),
    }


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _case_status(testcase: ElementTree.Element, context: str) -> str:
    status = (testcase.get("status") or "run").lower()
    result = (testcase.get("result") or "completed").lower()
    if status in {"disabled", "notrun"} or result in {"suppressed", "notrun"}:
        return "disabled"
    terminal = [
        _local_name(child.tag)
        for child in testcase
        if _local_name(child.tag) in {"failure", "error", "skipped"}
    ]
    if len(set(terminal)) > 1:
        raise ParityError(f"{context}: has conflicting terminal statuses {terminal!r}")
    if terminal:
        return {"failure": "failed", "error": "error", "skipped": "skipped"}[
            terminal[0]
        ]
    if status == "run" and result == "completed":
        return "passed"
    raise ParityError(f"{context}: unknown status={status!r}, result={result!r}")


def _read_ctest_junit(
    path: Path, expected_names: frozenset[str]
) -> dict[str, str]:
    root = ElementTree.parse(path).getroot()
    if _local_name(root.tag) not in {"testsuite", "testsuites"}:
        raise ParityError(f"{path}: expected a JUnit testsuite root")
    statuses: dict[str, str] = {}
    for testcase in root.iter():
        if _local_name(testcase.tag) != "testcase":
            continue
        name = testcase.get("name")
        if not name:
            raise ParityError(f"{path}: JUnit testcase omits its name")
        if name in statuses:
            raise ParityError(f"{path}: JUnit repeats testcase {name!r}")
        statuses[name] = _case_status(testcase, f"{path}:{name}")
    missing = expected_names - statuses.keys()
    extra = statuses.keys() - expected_names
    if missing or extra:
        raise ParityError(
            f"{path}: CTest JUnit selection mismatch; "
            f"missing={_sample(missing)}, extra={_sample(extra)}"
        )
    return statuses


def _validate_report_freshness(xml_path: Path, junit_mtime: float) -> None:
    xml_mtime = xml_path.stat().st_mtime
    age = junit_mtime - xml_mtime
    if age > MAX_REPORT_AGE_SECONDS or age < -5:
        raise ParityError(f"{xml_path}: stale grouped GoogleTest XML")
    root = ElementTree.parse(xml_path).getroot()
    timestamp = root.get("timestamp")
    if not timestamp:
        raise ParityError(f"{xml_path}: grouped GoogleTest XML omits timestamp")
    try:
        started_at = datetime.fromisoformat(timestamp).timestamp()
    except ValueError as error:
        raise ParityError(f"{xml_path}: invalid timestamp {timestamp!r}") from error
    duration = xml_mtime - started_at
    if duration < -5 or duration > 180:
        raise ParityError(f"{xml_path}: stale grouped GoogleTest XML content")


def _read_gtest_xml(
    path: Path, target: str, expected: frozenset[Identity], junit_mtime: float
) -> dict[Identity, str]:
    _validate_report_freshness(path, junit_mtime)
    root = ElementTree.parse(path).getroot()
    if _local_name(root.tag) != "testsuites":
        raise ParityError(f"{path}: expected a GoogleTest testsuites root")
    statuses: dict[Identity, str] = {}
    cases = [
        testcase
        for testcase in root.iter()
        if _local_name(testcase.tag) == "testcase"
    ]
    try:
        declared_count = int(root.get("tests", ""))
    except ValueError as error:
        raise ParityError(f"{path}: invalid GoogleTest tests count") from error
    if declared_count != len(cases):
        raise ParityError(
            f"{path}: declares {declared_count} tests but contains {len(cases)}"
        )
    for testcase in cases:
        classname = testcase.get("classname")
        name = testcase.get("name")
        if not classname or not name:
            raise ParityError(f"{path}: GoogleTest testcase requires classname/name")
        identity = (target, f"{classname}.{name}")
        if identity in statuses:
            raise ParityError(f"{path}: repeats GoogleTest case {identity[1]!r}")
        statuses[identity] = _case_status(testcase, f"{path}:{identity[1]}")
    missing = expected - statuses.keys()
    extra = statuses.keys() - expected
    if missing or extra:
        raise ParityError(
            f"{path}: grouped logical case mismatch; "
            f"missing={_sample(missing)}, extra={_sample(extra)}"
        )
    return statuses


def _map_discovered_statuses(
    junit: Mapping[str, str], registrations: Mapping[str, Identity]
) -> dict[Identity, str]:
    statuses: dict[Identity, str] = {}
    for display_name, identity in registrations.items():
        if identity in statuses:
            raise ParityError(f"raw GoogleTest identity is registered twice: {identity!r}")
        statuses[identity] = junit[display_name]
    return statuses


def _compare_execution(
    individual: Registration,
    grouped: Registration,
    individual_junit: Path,
    grouped_junit: Path,
    grouped_gtest_dir: Path,
) -> dict[str, object]:
    registration_report = _compare_registrations(individual, grouped)
    actual_files = frozenset(path.name for path in grouped_gtest_dir.glob("*.xml"))
    expected_files = frozenset(f"{target}.xml" for target in GROUPED_TARGETS)
    if actual_files != expected_files:
        raise ParityError(
            "grouped GoogleTest XML file set mismatch; "
            f"missing={_sample(expected_files - actual_files)}, "
            f"extra={_sample(actual_files - expected_files)}"
        )
    individual_junit_statuses = _read_ctest_junit(
        individual_junit, individual.physical_names
    )
    grouped_junit_statuses = _read_ctest_junit(
        grouped_junit, grouped.physical_names
    )
    unchanged_names = individual.physical_names.intersection(grouped.physical_names)
    unchanged_mismatches = {
        name: (individual_junit_statuses[name], grouped_junit_statuses[name])
        for name in unchanged_names
        if individual_junit_statuses[name] != grouped_junit_statuses[name]
    }
    if unchanged_mismatches:
        raise ParityError(
            "unchanged physical CTest status mismatch: "
            f"{_sample(unchanged_mismatches.items())}"
        )
    individual_statuses = _map_discovered_statuses(
        individual_junit_statuses, individual.ctest_cases
    )
    grouped_statuses = _map_discovered_statuses(
        grouped_junit_statuses, grouped.ctest_cases
    )
    for target, wrapper in grouped.wrappers.items():
        if grouped_junit_statuses[wrapper] != "passed":
            raise ParityError(
                f"grouped wrapper {wrapper!r} did not pass: "
                f"{grouped_junit_statuses[wrapper]}"
            )
        expected = frozenset(
            identity for identity in individual.identities if identity[0] == target
        )
        grouped_statuses.update(
            _read_gtest_xml(
                grouped_gtest_dir / f"{target}.xml",
                target,
                expected,
                grouped_junit.stat().st_mtime,
            )
        )
    if set(individual_statuses) != set(individual.identities):
        raise ParityError("individual execution omits registered logical cases")
    if set(grouped_statuses) != set(grouped.identities):
        raise ParityError("grouped execution omits registered logical cases")
    mismatches = {
        identity: (individual_statuses[identity], grouped_statuses[identity])
        for identity in individual_statuses
        if individual_statuses[identity] != grouped_statuses[identity]
    }
    if mismatches:
        raise ParityError(f"logical execution status mismatch: {_sample(mismatches.items())}")
    counts = Counter(individual_statuses.values())
    unchanged_counts = Counter(
        individual_junit_statuses[name] for name in unchanged_names
    )
    if not set(counts).issubset(VALID_STATUSES):
        raise ParityError("execution produced an unsupported logical status")
    return {
        **registration_report,
        "mode": "execution",
        "status_counts": {
            status: counts.get(status, 0) for status in sorted(VALID_STATUSES)
        },
        "status_digest": _digest(
            (*identity, status) for identity, status in individual_statuses.items()
        ),
        "unchanged_physical_status_counts": {
            status: unchanged_counts.get(status, 0)
            for status in sorted(VALID_STATUSES)
        },
        "unchanged_physical_status_digest": _digest(
            (name, individual_junit_statuses[name]) for name in unchanged_names
        ),
    }


class GroupedCTestParitySelfTests(unittest.TestCase):
    def _registration_pair(self, root: Path) -> tuple[Registration, Registration]:
        grouped_cases = {
            target: (f"Types/{target}Suite.Case/0",)
            for target in sorted(GROUPED_TARGETS)
        }
        cases = {**grouped_cases, "Unaffected": ("Values/Suite.Case/0",)}
        individual_names = {
            f"{target}.Pretty": (target, target_cases[0])
            for target, target_cases in grouped_cases.items()
        }
        individual_names["PrettyValue"] = ("Unaffected", "Values/Suite.Case/0")
        individual = Registration(
            root / "individual",
            frozenset({"Manual.Check", *individual_names}),
            cases,
            {target: "discovered" for target in cases},
            individual_names,
            {},
        )
        grouped_wrappers = {target: f"{target}.Grouped" for target in GROUPED_TARGETS}
        grouped = Registration(
            root / "grouped",
            frozenset({"Manual.Check", "PrettyValue", *grouped_wrappers.values()}),
            cases,
            {
                target: ("grouped" if target in GROUPED_TARGETS else "discovered")
                for target in cases
            },
            {"PrettyValue": ("Unaffected", "Values/Suite.Case/0")},
            grouped_wrappers,
        )
        return individual, grouped

    @staticmethod
    def _write_junit(path: Path, names: Iterable[str]) -> None:
        root = ElementTree.Element("testsuites")
        for name in names:
            ElementTree.SubElement(
                root, "testcase", {"name": name, "status": "run", "result": "completed"}
            )
        ElementTree.ElementTree(root).write(path, encoding="utf-8")

    def _write_grouped_xml(self, path: Path, case: str) -> None:
        classname, name = case.split(".", maxsplit=1)
        root = ElementTree.Element(
            "testsuites",
            {"tests": "1", "timestamp": datetime.now().isoformat(timespec="milliseconds")},
        )
        ElementTree.SubElement(
            root,
            "testcase",
            {
                "classname": classname,
                "name": name,
                "status": "run",
                "result": "completed",
            },
        )
        ElementTree.ElementTree(root).write(path, encoding="utf-8")

    def test_execution_parity_and_report_failures(self) -> None:
        for child, expected in (
            (None, "passed"),
            ("skipped", "skipped"),
            ("failure", "failed"),
            ("error", "error"),
        ):
            testcase = ElementTree.Element("testcase")
            if child:
                ElementTree.SubElement(testcase, child)
            self.assertEqual(_case_status(testcase, "case"), expected)
        disabled = ElementTree.Element("testcase", {"status": "notrun", "result": "suppressed"})
        self.assertEqual(_case_status(disabled, "case"), "disabled")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            individual, grouped = self._registration_pair(root)
            invalid = dict(grouped.representations)
            invalid["Unaffected"] = "grouped"
            with self.assertRaisesRegex(ParityError, "canonical grouped producers"):
                _compare_registrations(
                    individual,
                    Registration(
                        grouped.build_dir,
                        grouped.physical_names,
                        grouped.cases,
                        invalid,
                        grouped.ctest_cases,
                        grouped.wrappers,
                    ),
                )
            report_dir = root / "archived-grouped-gtest"
            report_dir.mkdir(parents=True)
            for target in GROUPED_TARGETS:
                self._write_grouped_xml(
                    report_dir / f"{target}.xml", individual.cases[target][0]
                )
            individual_junit = root / "individual.xml"
            grouped_junit = root / "grouped.xml"
            self._write_junit(individual_junit, individual.physical_names)
            self._write_junit(grouped_junit, grouped.physical_names)
            report = _compare_execution(
                individual, grouped, individual_junit, grouped_junit, report_dir
            )
            self.assertEqual(report["status_counts"]["passed"], len(individual.identities))
            output = root / "artifacts/parity.json"
            _print_report(report, output)
            self.assertEqual(json.loads(output.read_text()), report)
            tree = ElementTree.parse(grouped_junit)
            manual = next(
                case
                for case in tree.getroot().iter("testcase")
                if case.get("name") == "Manual.Check"
            )
            ElementTree.SubElement(manual, "skipped")
            tree.write(grouped_junit, encoding="utf-8")
            with self.assertRaisesRegex(
                ParityError, "unchanged physical CTest status mismatch"
            ):
                _compare_execution(
                    individual, grouped, individual_junit, grouped_junit, report_dir
                )
            self._write_junit(grouped_junit, grouped.physical_names)
            duplicate = next(iter(GROUPED_TARGETS))
            tree = ElementTree.parse(report_dir / f"{duplicate}.xml")
            tree.getroot().append(tree.getroot().find("testcase"))
            tree.getroot().set("tests", "2")
            tree.write(report_dir / f"{duplicate}.xml", encoding="utf-8")
            with self.assertRaisesRegex(ParityError, "repeats GoogleTest case"):
                _compare_execution(
                    individual, grouped, individual_junit, grouped_junit, report_dir
                )
            self._write_grouped_xml(
                report_dir / f"{duplicate}.xml", individual.cases[duplicate][0]
            )
            stale = report_dir / f"{duplicate}.xml"
            old = grouped_junit.stat().st_mtime - MAX_REPORT_AGE_SECONDS - 1
            os.utime(stale, (old, old))
            with self.assertRaisesRegex(ParityError, "stale grouped GoogleTest XML"):
                _compare_execution(
                    individual, grouped, individual_junit, grouped_junit, report_dir
                )


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prove individual/grouped CTest logical registration and execution parity."
    )
    parser.add_argument("--self-test", action="store_true")
    subparsers = parser.add_subparsers(dest="command")
    registration_parser = subparsers.add_parser("registration")
    execution_parser = subparsers.add_parser("execution")
    for command_parser in (registration_parser, execution_parser):
        command_parser.add_argument("--individual-build-dir", type=Path, required=True)
        command_parser.add_argument("--grouped-build-dir", type=Path, required=True)
        command_parser.add_argument("--output", type=Path)
    execution_parser.add_argument("--individual-junit", type=Path, required=True)
    execution_parser.add_argument("--grouped-junit", type=Path, required=True)
    execution_parser.add_argument("--grouped-gtest-dir", type=Path, required=True)
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        if arguments.command is not None:
            parser.error("--self-test cannot be combined with a command")
    elif arguments.command is None:
        parser.error("choose registration or execution")
    return arguments


def _run_self_tests() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GroupedCTestParitySelfTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def _print_report(report: Mapping[str, object], output: Path | None) -> None:
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                encoding="utf-8",
                dir=output.parent,
                prefix=f".{output.name}.",
                delete=False,
            ) as handle:
                json.dump(report, handle, indent=2, sort_keys=True)
                handle.write("\n")
                temporary = Path(handle.name)
            os.replace(temporary, output)
            temporary = None
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    fields = " ".join(
        f"{key}={json.dumps(value, separators=(',', ':'))}"
        for key, value in report.items()
        if key != "grouped_producers"
    )
    print(f"grouped CTest parity: ok: {fields}")


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.self_test:
        return _run_self_tests()
    try:
        individual = _load_registration(arguments.individual_build_dir, grouped=False)
        grouped = _load_registration(arguments.grouped_build_dir, grouped=True)
        if arguments.command == "registration":
            report = _compare_registrations(individual, grouped)
        else:
            report = _compare_execution(
                individual,
                grouped,
                arguments.individual_junit,
                arguments.grouped_junit,
                arguments.grouped_gtest_dir,
            )
    except (
        ElementTree.ParseError,
        GATE.ReconciliationError,
        OSError,
        ParityError,
        subprocess.SubprocessError,
    ) as error:
        print(f"grouped CTest parity: error: {error}", file=sys.stderr)
        return 1
    _print_report(report, arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
