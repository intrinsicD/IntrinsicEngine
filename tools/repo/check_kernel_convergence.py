#!/usr/bin/env python3
"""Enforce the Runtime.Engine kernel-convergence no-backsliding policy."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


POLICY_RELATIVE_PATH = Path("tools/repo/kernel_convergence_policy.json")
IMPORT_DIRECTIVE_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?:(export)\s+)?import\s+"
    r"(\"[^\"\n]*\"|<[^>\n]+>|[^;\s]+)\s*;"
)
ENGINE_CLASS_RE = re.compile(r"\bexport\s+class\s+Engine\s*\{")
GETTER_RE = re.compile(r"\b(Get[A-Z][A-Za-z0-9_]*)\s*\(")
ACCESS_RE = re.compile(r"(public|private|protected)\s*:")
IMPORT_KEYWORD_RE = re.compile(r"\bimport\b")
ATTRIBUTE_RE = re.compile(r"\[\[[^\]]*\]\]")


class PolicyError(RuntimeError):
    """The policy or inspected source is malformed."""


@dataclass(frozen=True)
class GetterSurface:
    name: str
    return_type: str


@dataclass(frozen=True)
class Snapshot:
    plain_imports: tuple[str, ...]
    domain_imports: tuple[str, ...]
    export_imports: tuple[str, ...]
    public_getters: tuple[GetterSurface, ...]

    @property
    def public_getter_names(self) -> tuple[str, ...]:
        return tuple(getter.name for getter in self.public_getters)


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise PolicyError(f"{name} must be an object")
    return value


def _string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise PolicyError(f"{name} must be a non-empty string")
    return value


def _nonnegative_int(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise PolicyError(f"{name} must be a non-negative integer")
    return value


def _string_list(value: Any, name: str) -> list[str]:
    if not isinstance(value, list):
        raise PolicyError(f"{name} must be an array")
    result: list[str] = []
    for index, item in enumerate(value):
        result.append(_string(item, f"{name}[{index}]"))
    if len(result) != len(set(result)):
        raise PolicyError(f"{name} contains duplicate entries")
    return result


def _normalize_cpp_type(value: str) -> str:
    normalized = " ".join(value.split())
    normalized = re.sub(r"\s*::\s*", "::", normalized)
    normalized = re.sub(r"\s*([&*<>,])\s*", r"\1", normalized)
    return normalized


def _owning_type(return_type: str) -> str:
    owning = return_type.strip()
    while owning.startswith("const ") or owning.startswith("volatile "):
        owning = owning.split(" ", 1)[1].strip()
    owning = re.sub(r"(?:&&|[&*])+$", "", owning).strip()
    while owning.endswith(" const") or owning.endswith(" volatile"):
        owning = owning.rsplit(" ", 1)[0].strip()
    return owning


def _getter_policy_list(value: Any, name: str) -> list[dict[str, str]]:
    if not isinstance(value, list):
        raise PolicyError(f"{name} must be an array")

    result: list[dict[str, str]] = []
    names: set[str] = set()
    for index, item in enumerate(value):
        entry_name = f"{name}[{index}]"
        entry = _mapping(item, entry_name)
        expected_keys = {"name", "return_type", "owning_type", "owning_import"}
        if set(entry) != expected_keys:
            raise PolicyError(
                f"{entry_name} must contain exactly {sorted(expected_keys)}"
            )
        getter_name = _string(entry.get("name"), f"{entry_name}.name")
        if not re.fullmatch(r"Get[A-Z][A-Za-z0-9_]*", getter_name):
            raise PolicyError(f"{entry_name}.name is not an Engine GetX name")
        if getter_name in names:
            raise PolicyError(f"{name} contains duplicate getter {getter_name}")
        names.add(getter_name)

        return_type = _normalize_cpp_type(
            _string(entry.get("return_type"), f"{entry_name}.return_type")
        )
        owning_type = _normalize_cpp_type(
            _string(entry.get("owning_type"), f"{entry_name}.owning_type")
        )
        if _owning_type(return_type) != owning_type:
            raise PolicyError(
                f"{entry_name}.owning_type does not match its return_type"
            )
        result.append(
            {
                "name": getter_name,
                "return_type": return_type,
                "owning_type": owning_type,
                "owning_import": _string(
                    entry.get("owning_import"), f"{entry_name}.owning_import"
                ),
            }
        )
    return result


def _strip_comments_and_literals(source: str) -> str:
    """Replace comments and quoted literals with spaces, preserving newlines."""

    out = list(source)
    index = 0
    length = len(source)
    while index < length:
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            if end < 0:
                end = length
            for pos in range(index, end):
                out[pos] = " "
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise PolicyError("unterminated block comment in Engine interface")
            for pos in range(index, end + 2):
                if source[pos] != "\n":
                    out[pos] = " "
            index = end + 2
            continue
        if source[index] in {'"', "'"}:
            quote = source[index]
            start = index
            index += 1
            escaped = False
            while index < length:
                char = source[index]
                if char == "\n" and quote == '"' and not escaped:
                    raise PolicyError("unterminated string literal in Engine interface")
                if char == quote and not escaped:
                    index += 1
                    break
                if char == "\\" and not escaped:
                    escaped = True
                else:
                    escaped = False
                index += 1
            else:
                raise PolicyError("unterminated quoted literal in Engine interface")
            prefix = "".join(out[max(0, start - 256) : start])
            is_header_import_target = bool(
                quote == '"'
                and re.search(
                    r"(?:^|;)\s*(?:export\s+)?import\s*$",
                    prefix,
                    re.MULTILINE,
                )
            )
            if not is_header_import_target:
                for pos in range(start, index):
                    if source[pos] != "\n":
                        out[pos] = " "
            continue
        index += 1
    return "".join(out)


def _engine_class_body(clean_source: str) -> str:
    match = ENGINE_CLASS_RE.search(clean_source)
    if match is None:
        raise PolicyError("export class Engine definition was not found")

    open_brace = clean_source.find("{", match.start())
    depth = 0
    for index in range(open_brace, len(clean_source)):
        char = clean_source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return clean_source[open_brace + 1 : index]
            if depth < 0:
                break
    raise PolicyError("export class Engine definition has unbalanced braces")


def _public_top_level_declarations(class_body: str) -> list[str]:
    """Return public Engine declarations without inline-function bodies."""

    declarations: list[str] = []
    current: list[str] = []
    access = "private"
    depth = 0
    index = 0
    while index < len(class_body):
        if depth == 0:
            match = ACCESS_RE.match(class_body, index)
            if match is not None:
                current.clear()
                access = match.group(1)
                index = match.end()
                continue

        char = class_body[index]
        if depth == 0 and char == "{":
            declaration = "".join(current).strip()
            if access == "public" and declaration:
                declarations.append(declaration)
            current.clear()
            depth += 1
        elif depth == 0 and char == ";":
            if access == "public":
                current.append(char)
                declaration = "".join(current).strip()
                if declaration:
                    declarations.append(declaration)
            current.clear()
        elif depth == 0:
            if access == "public":
                current.append(char)
        elif char == "}":
            depth -= 1
            if depth < 0:
                raise PolicyError("unexpected closing brace in Engine class body")
        index += 1

    if depth != 0:
        raise PolicyError("unbalanced nested braces in Engine class body")
    return declarations


def _public_getters(class_body: str) -> tuple[GetterSurface, ...]:
    getters: list[GetterSurface] = []
    seen: set[str] = set()
    for declaration in _public_top_level_declarations(class_body):
        matches = list(GETTER_RE.finditer(declaration))
        if not matches:
            continue
        if len(matches) != 1:
            raise PolicyError(
                "ambiguous public Engine getter declaration: "
                + " ".join(declaration.split())
            )
        match = matches[0]
        name = match.group(1)
        if name in seen:
            raise PolicyError(f"overloaded public Engine getter is unsupported: {name}")
        seen.add(name)

        prefix = ATTRIBUTE_RE.sub(" ", declaration[: match.start()])
        prefix = re.sub(
            r"^(?:(?:virtual|static|inline|constexpr|consteval|friend)\s+)+",
            "",
            prefix.strip(),
        )
        return_type = _normalize_cpp_type(prefix)
        if not return_type:
            raise PolicyError(f"cannot determine return type for public Engine getter {name}")
        getters.append(GetterSurface(name=name, return_type=return_type))
    return tuple(sorted(getters, key=lambda getter: getter.name))


def _is_substrate(module: str, prefixes: list[str], exact: set[str]) -> bool:
    return module in exact or any(module.startswith(prefix) for prefix in prefixes)


def inspect_engine(source: str, prefixes: list[str], exact: set[str]) -> Snapshot:
    clean = _strip_comments_and_literals(source)
    plain: list[str] = []
    exported: list[str] = []
    directive_matches = list(IMPORT_DIRECTIVE_RE.finditer(clean))
    for match in directive_matches:
        export_token, target = match.groups()
        if target.startswith(('"', "<")):
            raise PolicyError(
                f"unsupported header-unit import in Engine interface: {target}"
            )
        (exported if export_token else plain).append(target)
    recognized_spans = [match.span() for match in directive_matches]
    for keyword in IMPORT_KEYWORD_RE.finditer(clean):
        if not any(start <= keyword.start() < end for start, end in recognized_spans):
            line_start = clean.rfind("\n", 0, keyword.start()) + 1
            line_end = clean.find("\n", keyword.end())
            if line_end < 0:
                line_end = len(clean)
            snippet = " ".join(clean[line_start:line_end].split())
            raise PolicyError(
                f"unsupported import declaration in Engine interface: {snippet}"
            )
    plain_imports = tuple(plain)
    export_imports = tuple(exported)
    domain_imports = tuple(
        sorted(
            module
            for module in plain_imports
            if not _is_substrate(module, prefixes, exact)
        )
    )
    getters = _public_getters(_engine_class_body(clean))
    return Snapshot(
        plain_imports=plain_imports,
        domain_imports=domain_imports,
        export_imports=tuple(sorted(export_imports)),
        public_getters=getters,
    )


def _load_policy(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise PolicyError(f"policy file is missing: {path}") from error
    except (OSError, json.JSONDecodeError) as error:
        raise PolicyError(f"cannot read policy {path}: {error}") from error
    return _mapping(payload, "policy")


def _validate_open_debt_owner(root: Path, owner: str) -> None:
    if not re.fullmatch(r"[A-Z]+-[0-9]+[A-Z]*", owner):
        raise PolicyError(f"temporary_debt.owner is not a task ID: {owner}")

    matches: list[Path] = []
    id_re = re.compile(rf"^id:\s*{re.escape(owner)}\s*$", re.MULTILINE)
    for state in ("active", "backlog"):
        state_root = root / "tasks" / state
        if not state_root.is_dir():
            continue
        for task_path in state_root.rglob("*.md"):
            try:
                text = task_path.read_text(encoding="utf-8")
            except (OSError, UnicodeError) as error:
                raise PolicyError(f"cannot read debt-owner task {task_path}: {error}") from error
            if id_re.search(text):
                matches.append(task_path)
    if len(matches) != 1:
        rendered = ", ".join(str(path.relative_to(root)) for path in matches) or "none"
        raise PolicyError(
            f"temporary_debt.owner {owner} must resolve exactly once under "
            f"tasks/active or tasks/backlog; matches: {rendered}"
        )


def _validate_policy(policy: dict[str, Any], root: Path) -> dict[str, Any]:
    if policy.get("schema_version") != 2:
        raise PolicyError("schema_version must be 2")
    _string(policy.get("engine_interface"), "engine_interface")

    substrate = _mapping(policy.get("substrate_imports"), "substrate_imports")
    prefixes = _string_list(substrate.get("prefixes"), "substrate_imports.prefixes")
    exact = _string_list(substrate.get("exact"), "substrate_imports.exact")
    overlap = set(prefixes) & set(exact)
    if overlap:
        raise PolicyError(f"substrate prefix/exact overlap: {sorted(overlap)}")

    reference = _mapping(policy.get("reference_snapshot"), "reference_snapshot")
    _string(reference.get("date"), "reference_snapshot.date")
    _string(reference.get("metric"), "reference_snapshot.metric")
    reference_plain = _nonnegative_int(
        reference.get("plain_import_count"), "reference_snapshot.plain_import_count"
    )
    reference_domain = _nonnegative_int(
        reference.get("domain_import_count"), "reference_snapshot.domain_import_count"
    )
    reference_getters = _string_list(
        reference.get("public_getter_names"),
        "reference_snapshot.public_getter_names",
    )
    reference_getter_count = _nonnegative_int(
        reference.get("public_getter_count"),
        "reference_snapshot.public_getter_count",
    )
    if reference_getter_count != len(reference_getters):
        raise PolicyError("reference_snapshot.public_getter_count does not match its list")

    current = _mapping(policy.get("current_snapshot"), "current_snapshot")
    _string(current.get("date"), "current_snapshot.date")
    _string(current.get("metric"), "current_snapshot.metric")
    current_plain = _nonnegative_int(
        current.get("plain_import_count"), "current_snapshot.plain_import_count"
    )
    current_plain_imports = _string_list(
        current.get("plain_imports"), "current_snapshot.plain_imports"
    )
    if current_plain != len(current_plain_imports):
        raise PolicyError("current_snapshot.plain_import_count does not match its list")
    current_domain = _nonnegative_int(
        current.get("domain_import_count"), "current_snapshot.domain_import_count"
    )
    current_domains = _string_list(
        current.get("domain_imports"), "current_snapshot.domain_imports"
    )
    if current_domain != len(current_domains):
        raise PolicyError("current_snapshot.domain_import_count does not match its list")
    current_exports = _string_list(
        current.get("export_imports"), "current_snapshot.export_imports"
    )
    current_export_count = _nonnegative_int(
        current.get("export_import_count"), "current_snapshot.export_import_count"
    )
    if current_export_count != len(current_exports):
        raise PolicyError("current_snapshot.export_import_count does not match its list")
    current_getters = _getter_policy_list(
        current.get("public_getters"), "current_snapshot.public_getters"
    )
    current_getter_count = _nonnegative_int(
        current.get("public_getter_count"), "current_snapshot.public_getter_count"
    )
    if current_getter_count != len(current_getters):
        raise PolicyError("current_snapshot.public_getter_count does not match its list")
    for getter in current_getters:
        if getter["owning_import"] not in current_plain_imports:
            raise PolicyError(
                "current_snapshot.public_getters owning_import is not an exact "
                f"plain import: {getter['name']} -> {getter['owning_import']}"
            )

    expected_debt_plain = max(0, current_plain - reference_plain)
    expected_debt_domain = max(0, current_domain - reference_domain)
    current_getter_names = [getter["name"] for getter in current_getters]
    expected_debt_getters = sorted(
        set(current_getter_names) - set(reference_getters)
    )
    has_expected_debt = bool(
        expected_debt_plain or expected_debt_domain or expected_debt_getters
    )
    debt_value = policy.get("temporary_debt")
    debt: dict[str, Any] | None
    if not has_expected_debt:
        if debt_value is not None:
            raise PolicyError(
                "temporary_debt must be null when the current snapshot has no excess"
            )
        debt = None
    else:
        debt = _mapping(debt_value, "temporary_debt")
        debt_plain = _nonnegative_int(
            debt.get("plain_imports"), "temporary_debt.plain_imports"
        )
        debt_domain = _nonnegative_int(
            debt.get("domain_imports"), "temporary_debt.domain_imports"
        )
        debt_getters = _string_list(
            debt.get("getter_names"), "temporary_debt.getter_names"
        )
        debt_owner = _string(debt.get("owner"), "temporary_debt.owner")
        if debt_plain != expected_debt_plain:
            raise PolicyError("temporary_debt.plain_imports does not match snapshot delta")
        if debt_domain != expected_debt_domain:
            raise PolicyError("temporary_debt.domain_imports does not match snapshot delta")
        if sorted(debt_getters) != expected_debt_getters:
            raise PolicyError("temporary_debt.getter_names does not match snapshot delta")
        _validate_open_debt_owner(root, debt_owner)

    return {
        "prefixes": prefixes,
        "exact": set(exact),
        "reference": reference,
        "current": current,
        "current_plain_imports": current_plain_imports,
        "current_getters": current_getters,
        "debt": debt,
    }


def _describe_delta(label: str, observed: list[str], expected: list[str]) -> list[str]:
    observed_counter = Counter(observed)
    expected_counter = Counter(expected)
    added = sorted((observed_counter - expected_counter).elements())
    stale = sorted((expected_counter - observed_counter).elements())
    messages: list[str] = []
    if added:
        messages.append(f"unexpected {label}: {', '.join(added)}")
    if stale:
        messages.append(
            f"stale {label} policy entries (ratchet the policy with this reduction): "
            + ", ".join(stale)
        )
    return messages


def check(root: Path) -> int:
    policy_path = root / POLICY_RELATIVE_PATH
    policy = _load_policy(policy_path)
    validated = _validate_policy(policy, root)

    interface_relative = Path(_string(policy.get("engine_interface"), "engine_interface"))
    interface_path = root / interface_relative
    try:
        source = interface_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise PolicyError(f"cannot read Engine interface {interface_path}: {error}") from error

    snapshot = inspect_engine(source, validated["prefixes"], validated["exact"])
    current = validated["current"]
    reference = validated["reference"]
    debt = validated["debt"]

    findings: list[str] = []
    expected_plain_count = _nonnegative_int(
        current.get("plain_import_count"), "current_snapshot.plain_import_count"
    )
    if len(snapshot.plain_imports) != expected_plain_count:
        direction = "increased" if len(snapshot.plain_imports) > expected_plain_count else "decreased"
        findings.append(
            f"plain import count {direction}: observed {len(snapshot.plain_imports)}, "
            f"policy {expected_plain_count}"
        )
    findings.extend(
        _describe_delta(
            "plain imports",
            list(snapshot.plain_imports),
            validated["current_plain_imports"],
        )
    )
    findings.extend(
        _describe_delta(
            "domain imports",
            list(snapshot.domain_imports),
            _string_list(current.get("domain_imports"), "current_snapshot.domain_imports"),
        )
    )
    findings.extend(
        _describe_delta(
            "export imports",
            list(snapshot.export_imports),
            _string_list(current.get("export_imports"), "current_snapshot.export_imports"),
        )
    )
    expected_getters = {
        getter["name"]: getter for getter in validated["current_getters"]
    }
    observed_getters = {getter.name: getter for getter in snapshot.public_getters}
    findings.extend(
        _describe_delta(
            "public Engine GetX names",
            list(observed_getters),
            list(expected_getters),
        )
    )
    for getter_name in sorted(set(observed_getters) & set(expected_getters)):
        observed_return = observed_getters[getter_name].return_type
        expected_return = expected_getters[getter_name]["return_type"]
        if observed_return != expected_return:
            findings.append(
                f"public Engine getter {getter_name} return type changed: "
                f"observed {observed_return}, policy {expected_return} "
                f"(owning type {expected_getters[getter_name]['owning_type']}, "
                f"import {expected_getters[getter_name]['owning_import']})"
            )

    print(f"[check_kernel_convergence] Engine interface: {interface_relative}")
    print(
        "[check_kernel_convergence] Observed: "
        f"plain_imports={len(snapshot.plain_imports)} "
        f"domain_imports={len(snapshot.domain_imports)} "
        f"export_imports={len(snapshot.export_imports)} "
        f"public_getter_names={len(snapshot.public_getter_names)}"
    )
    print(
        "[check_kernel_convergence] Fixed reference "
        f"{reference['date']} ({reference['metric']}): "
        f"plain_imports={reference['plain_import_count']} "
        f"domain_imports={reference['domain_import_count']} "
        f"public_getter_names={reference['public_getter_count']}"
    )
    if debt is None:
        print("[check_kernel_convergence] Temporary debt: none")
    else:
        print(
            "[check_kernel_convergence] Temporary debt "
            f"owner={debt['owner']}: +{debt['plain_imports']} plain, "
            f"+{debt['domain_imports']} domain, getters={debt['getter_names']}"
        )

    if findings:
        for finding in findings:
            print(f"[check_kernel_convergence] ERROR: {finding}")
        return 1

    print("[check_kernel_convergence] Kernel convergence policy matches exactly.")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="repository root")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="accepted for consistency; convergence mismatches always fail closed",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        return check(args.root.resolve())
    except PolicyError as error:
        print(f"[check_kernel_convergence] FATAL: {error}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
