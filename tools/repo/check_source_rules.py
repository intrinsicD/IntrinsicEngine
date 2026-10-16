#!/usr/bin/env python3
"""Check source-structure rules: required, forbidden, ordered or counted text and retired paths.

Rules live in tools/repo/source_rules.py. Each rule keeps the GoogleTest suite and case it
replaced, so `--suite` selects the rules that one ctest registration owns. `--self-check`
mutates every rule's input in memory and fails if any rule would still pass, proving that
each requirement, prohibition, order, count and retired path actually bites.
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence

SOURCE_EXTENSIONS = (".cpp", ".cppm", ".hpp", ".h", ".inl")


@dataclass(frozen=True)
class Rule:
    suite: str
    name: str
    # Explicit files are concatenated into one text; each must exist.
    files: tuple[str, ...] = ()
    # Directory trees whose files are checked one by one (forbid only).
    trees: tuple[str, ...] = ()
    exts: tuple[str, ...] = SOURCE_EXTENSIONS
    exclude: tuple[str, ...] = ()
    # Text between the first `begin` and the next `end` after it; both must exist. An empty
    # `begin` starts at the file start.
    slice: tuple[str, str] | None = None
    # Remove whitespace from text and tokens before matching.
    compact: bool = False
    require: tuple[str, ...] = ()
    forbid: tuple[str, ...] = ()
    # Each token found after the previous match (chained `find(token, previous)`).
    order: tuple[str, ...] = ()
    # First occurrences strictly increasing (independent `find(token)` from the start).
    first_order: tuple[str, ...] = ()
    count: tuple[tuple[str, int], ...] = ()
    absent: tuple[str, ...] = ()
    # Tree rules: the files containing each token must be exactly the listed files.
    owners: tuple[tuple[str, tuple[str, ...]], ...] = ()
    # File rules: no line may start a C++ module directive (the files stay textual glue).
    no_module_directive: bool = False

    @property
    def label(self) -> str:
        return f"{self.suite}.{self.name}"


Reader = Callable[[Path], str | None]

MODULE_DIRECTIVES = ("module;", "module ", "export module ", "import ", "export import ")


def _compact(text: str) -> str:
    return "".join(text.split())


def _token(rule: Rule, token: str) -> str:
    return _compact(token) if rule.compact else token


def _span(text: str, markers: tuple[str, str]) -> tuple[int, int] | None:
    """Start and end of the first `begin` marker up to the next `end` marker after it."""
    begin = text.find(markers[0])
    if begin < 0:
        return None
    end = text.find(markers[1], begin + len(markers[0]))
    return None if end < 0 else (begin, end)


def _read(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def _tree_files(rule: Rule, root: Path) -> list[Path]:
    return sorted(
        path for tree in rule.trees for path in (root / tree).rglob("*")
        if path.is_file() and path.suffix in rule.exts
        and not any(path.relative_to(root).as_posix().startswith(prefix) for prefix in rule.exclude))


def check_rule(rule: Rule, root: Path, read: Reader = _read) -> list[str]:
    errors = [f"retired path exists: {relative}" for relative in rule.absent if (root / relative).exists()]

    if rule.files:
        parts = [read(root / relative) for relative in rule.files]
        missing = [relative for relative, part in zip(rule.files, parts) if part is None]
        if missing:
            return errors + [f"missing file: {relative}" for relative in missing]
        text = "\n".join(parts)
        if rule.slice is not None:
            span = _span(text, rule.slice)
            if span is None:
                return errors + [f"slice markers not found: {rule.slice!r}"]
            text = text[span[0]:span[1]]
        if rule.compact:
            text = _compact(text)
        errors += [f"required text missing: {t!r}" for t in rule.require if _token(rule, t) not in text]
        errors += [f"forbidden text present: {t!r}" for t in rule.forbid if _token(rule, t) in text]
        position = -1
        for token in rule.order:
            position = text.find(_token(rule, token), position + 1)
            if position < 0:
                errors.append(f"order broken at {token!r}")
                break
        firsts = [text.find(_token(rule, token)) for token in rule.first_order]
        errors += [f"first_order token missing: {t!r}" for t, first in zip(rule.first_order, firsts) if first < 0]
        if min(firsts, default=0) >= 0 and any(a >= b for a, b in zip(firsts, firsts[1:])):
            errors.append(f"first occurrences out of order: {rule.first_order!r}")
        for token, expected in rule.count:
            actual = text.count(_token(rule, token))
            if actual != expected:
                errors.append(f"count of {token!r} is {actual}, expected {expected}")
        if rule.no_module_directive and any(
                line.lstrip(" \t").startswith(MODULE_DIRECTIVES) for line in text.splitlines()):
            errors.append("contains a C++ module directive")

    if rule.trees:
        errors += [f"missing tree: {tree}" for tree in rule.trees if not (root / tree).is_dir()]
        files = _tree_files(rule, root)
        if not files:
            errors.append("trees contain no matching files")
        found_owners: dict[str, set[str]] = {token: set() for token, _ in rule.owners}
        for path in files:
            name = path.relative_to(root).as_posix()
            content = read(path)
            if content is None:
                errors.append(f"unreadable file: {name}")
                continue
            text = _compact(content) if rule.compact else content
            errors += [f"{name}: forbidden text present: {t!r}" for t in rule.forbid if _token(rule, t) in text]
            for token, _ in rule.owners:
                if _token(rule, token) in text:
                    found_owners[token].add(name)
        for token, expected in rule.owners:
            if found_owners[token] != set(expected):
                errors.append(f"files containing {token!r} are {sorted(found_owners[token])}, "
                              f"expected {sorted(expected)}")
    return errors


def _validate(rules: Sequence[Rule]) -> list[str]:
    problems: list[str] = []
    labels = [rule.label for rule in rules]
    for label in sorted({label for label in labels if labels.count(label) > 1}):
        problems.append(f"duplicate rule label: {label}")
    for rule in rules:
        if rule.trees and (rule.require or rule.order or rule.first_order or rule.count or rule.slice):
            problems.append(f"{rule.label}: tree rules support forbid and owners only")
        if rule.owners and not rule.trees:
            problems.append(f"{rule.label}: owners need trees")
        if not (rule.files or rule.trees or rule.absent):
            problems.append(f"{rule.label}: rule checks nothing")
        if (rule.require or rule.order or rule.first_order or rule.count) and not rule.files:
            problems.append(f"{rule.label}: require/order/count need files")
    return problems


def _mutations(rule: Rule, root: Path) -> Iterable[tuple[str, Reader]]:
    """Yield (description, reader) inputs under which `rule` must fail.

    Missing files, slice markers and retired paths fail through generic branches that
    Test.CheckSourceRules.py covers; these mutations prove each rule's own tokens bite.
    """
    rule_files = {root / relative for relative in rule.files}

    def rewrite(edit: Callable[[str], str]) -> Reader:
        """Apply `edit` to the rule's slice of each rule file (or the whole file)."""
        def apply(content: str) -> str:
            if rule.slice is None:
                return edit(content)
            span = _span(content, rule.slice)
            return content if span is None else content[:span[0]] + edit(content[span[0]:span[1]]) + content[span[1]:]

        return lambda path: (lambda c: c if c is None or path not in rule_files else apply(c))(_read(path))

    def inject(token: str) -> Reader:
        marker = rule.slice[0] if rule.slice is not None else ""
        return rewrite(lambda part: marker + token + part[len(marker):])

    def remove(token: str, limit: int = -1) -> Reader:
        # Compacting is equivalent for the check and also removes whitespace-split tokens.
        return rewrite(lambda c: (_compact(c) if rule.compact else c).replace(_token(rule, token), "", limit))

    for token in rule.require:
        yield f"remove required {token!r}", remove(token)
    if rule.files:
        for token in rule.forbid:
            yield f"inject forbidden {token!r}", inject(token)
    if len(rule.order) >= 2:
        yield "reverse order", rewrite(lambda c: _reverse_order(c, rule.order, rule.compact))
    if len(rule.first_order) >= 2:
        yield "move last first_order token first", inject(_token(rule, rule.first_order[-1]))
    for token, expected in rule.count:
        yield f"add one {token!r}", inject(token)
        if expected > 0:
            yield f"remove one {token!r}", remove(token, limit=1)
    if rule.no_module_directive:
        yield "add a module directive", rewrite(lambda c: "import Probe;\n" + c)


def _reverse_order(content: str, order: tuple[str, ...], compact: bool) -> str:
    text = _compact(content) if compact else content
    tokens = [_compact(t) if compact else t for t in order]
    placeholders = [f"\x00{i}\x00" for i in range(len(tokens))]
    for token, placeholder in zip(tokens, placeholders):
        text = text.replace(token, placeholder)
    for token, placeholder in zip(reversed(tokens), placeholders):
        text = text.replace(placeholder, token)
    return text


def self_check(rules: Sequence[Rule], root: Path) -> list[str]:
    problems: list[str] = []
    for rule in rules:
        if check_rule(rule, root):
            continue  # The live failure is reported by the normal run.
        for description, read in _mutations(rule, root):
            if not check_rule(rule, root, read=read):
                problems.append(f"{rule.label}: still passes after {description}")
        problems.extend(_tree_problems(rule, root))
    return problems


def _tree_problems(rule: Rule, root: Path) -> list[str]:
    """Tree prohibitions must notice an injected token; owner sets a new or a lost owner."""
    files = _tree_files(rule, root)
    problems: list[str] = []

    def edited(target: Path, edit: Callable[[str], str]) -> Reader:
        return lambda path: (lambda c: edit(c or "") if path == target else c)(_read(path))

    if files:
        for token in rule.forbid:
            if not check_rule(rule, root, read=edited(files[0], lambda c, t=token: t + c)):
                problems.append(f"{rule.label}: still passes after injecting {token!r} into "
                                f"{files[0].relative_to(root)}")
    for token, expected in rule.owners:
        owned = {root / name for name in expected}
        others = [path for path in files if path not in owned]
        if others and not check_rule(rule, root, read=edited(others[0], lambda c, t=token: t + c)):
            problems.append(f"{rule.label}: owners of {token!r} miss a new file")
        for owner in sorted(owned):
            if not check_rule(rule, root, read=edited(owner, lambda c, t=token: c.replace(t, ""))):
                problems.append(f"{rule.label}: owners of {token!r} miss losing {owner.relative_to(root)}")
    return problems


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--suite", action="append", default=[],
                        help="Only check rules of these GoogleTest suites (repeatable).")
    parser.add_argument("--self-check", action="store_true",
                        help="Also prove every rule fails under a matching mutation.")
    args = parser.parse_args(argv)

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from source_rules import RULES  # noqa: E402

    root = args.root.resolve()
    rules = [rule for rule in RULES if not args.suite or rule.suite in args.suite]
    problems = _validate(RULES)
    if args.suite and not rules:
        problems.append(f"no rules for suites {args.suite}")
    for rule in rules:
        problems.extend(f"{rule.label}: {error}" for error in check_rule(rule, root))
    if args.self_check:
        problems.extend(self_check(rules, root))

    for problem in problems:
        print(f"[check_source_rules] {problem}")
    print(f"[check_source_rules] rules={len(rules)} problems={len(problems)}"
          f"{' self-check' if args.self_check else ''}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
