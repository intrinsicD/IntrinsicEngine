#!/usr/bin/env python3
"""Validate the Agent-Native Research Artifact under ``ara/``.

``ara/logic/claims.md`` is where a research or capability statement becomes a tracked claim with
a falsification criterion and a proof binding. Every other agent-process artifact in this
repository has a validator; this one did not, so a claim could cite a proof path that had been
moved, depend on a constraint ID that was never crystallized, or carry a status word nobody else
uses — and ``ara/PAPER.md`` could keep advertising a layer directory that no longer exists.

Checks are structural, never stylistic:

  1. Required ``ara/`` layer files exist.
  2. Every layer path advertised in ``ara/PAPER.md`` resolves on disk.
  3. Claim headings are well formed (``## C<NN>: <title>``) and claim IDs are unique.
  4. Every claim carries the nine required fields.
  5. Every ``Status`` starts with a known disposition word.
  6. Every ``Dependencies`` entry resolves in the namespace its prefix implies
     (``C`` claims, ``K`` constraints, ``A`` architecture, ``H`` heuristics).
  7. Every ``Proof`` entry that looks like a repository path exists, and every disposed
     (supported/refuted) claim cites at least one such path.
  8. Every ``From staging`` observation ID is defined in ``ara/staging/observations.yaml``.
  9. ``ara/`` is described in ``AGENTS.md`` so agents discover the ledger at all.

Usage:
    python3 tools/agents/check_ara_claims.py --root .
    python3 tools/agents/check_ara_claims.py --root . --strict

Warning mode (the default) reports and exits 0; ``--strict`` exits nonzero on any finding, and is
what ``ci-docs.yml`` runs.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REQUIRED_FILES = (
    "ara/PAPER.md",
    "ara/logic/problem.md",
    "ara/logic/claims.md",
    "ara/logic/solution/constraints.md",
    "ara/logic/solution/architecture.md",
    "ara/logic/solution/heuristics.md",
    "ara/staging/observations.yaml",
    "ara/trace/exploration_tree.yaml",
    "ara/trace/sessions/session_index.yaml",
    "ara/evidence/README.md",
)

REQUIRED_CLAIM_FIELDS = (
    "Statement",
    "Status",
    "Provenance",
    "Crystallized via",
    "Falsification criteria",
    "Proof",
    "Dependencies",
    "Tags",
    "From staging",
)

# First word of a Status line; a scope qualifier may follow.
STATUS_WORDS = frozenset(
    {
        "supported",
        "refuted",
        "hypothesis",
        "untested",
        "unavailable",
        "superseded",
        "withdrawn",
    }
)

# Dependency ID prefix -> the layer file that crystallizes that namespace.
DEPENDENCY_NAMESPACES = {
    "C": "ara/logic/claims.md",
    "K": "ara/logic/solution/constraints.md",
    "A": "ara/logic/solution/architecture.md",
    "H": "ara/logic/solution/heuristics.md",
}

# A Proof entry is treated as a repository path when it starts with one of these roots.
PATH_ROOTS = (
    "ara/",
    "benchmarks/",
    "docs/",
    "methods/",
    "src/",
    "tasks/",
    "tests/",
    "tools/",
)

CLAIM_HEADING = re.compile(r"^## (C\d+):\s*(\S.*)$")
FIELD = re.compile(r"^- \*\*([A-Za-z ]+)\*\*:\s*(.*)$")
SECTION_ID = re.compile(r"^## ([A-Z]\d+):", re.MULTILINE)


class Checker:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.findings: list[str] = []

    def err(self, message: str) -> None:
        self.findings.append(message)

    def read(self, relative: str) -> str:
        path = self.root / relative
        try:
            return path.read_text(encoding="utf-8")
        except OSError:
            self.err(f"missing or unreadable file: {relative}")
            return ""

    # -- checks ---------------------------------------------------------------

    def check_required_files(self) -> None:
        for relative in REQUIRED_FILES:
            if not (self.root / relative).is_file():
                self.err(f"required ara file missing: {relative}")

    def check_paper_layer_index(self) -> None:
        """Every backticked path in ara/PAPER.md's ``## Layers`` section must resolve under ara/.

        Scoped to that one section on purpose. Paths there are ara-relative layer entries, so they
        are resolved only under ``ara/`` — resolving them at the repository root instead would let
        a stale entry like ``src/`` pass because an unrelated top-level ``src/`` exists. Prose
        elsewhere in the file may mention engine-tree directories without being a layer claim.
        """
        section = self._paper_layers_section()
        for token in re.findall(r"`([\w./-]+/?)`", section):
            if not token.endswith("/") and "." not in token:
                continue  # prose token, not a path
            if (self.root / "ara" / token).exists():
                continue
            self.err(
                f"ara/PAPER.md advertises layer '{token}' which does not exist under ara/ "
                "(remove the entry or create the layer)"
            )

    def _paper_layers_section(self) -> str:
        """The body of ara/PAPER.md's '## Layers' heading, up to the next same-level heading."""
        lines = self.read("ara/PAPER.md").splitlines()
        body: list[str] = []
        inside = False
        for line in lines:
            if line.startswith("## "):
                if inside:
                    break
                inside = line.strip().lower() == "## layers"
                continue
            if inside:
                body.append(line)
        if not body:
            self.err("ara/PAPER.md has no '## Layers' section (the layer index is required)")
        return "\n".join(body)

    def parse_claims(self) -> dict[str, dict[str, str]]:
        """Return {claim_id: {field: value}}; wrapped field values are joined with a space."""
        claims: dict[str, dict[str, str]] = {}
        current: str | None = None
        field_name: str | None = None
        for lineno, line in enumerate(self.read("ara/logic/claims.md").splitlines(), start=1):
            heading = CLAIM_HEADING.match(line)
            if heading:
                claim_id = heading.group(1)
                if claim_id in claims:
                    self.err(f"ara/logic/claims.md:{lineno}: duplicate claim id '{claim_id}'")
                claims.setdefault(claim_id, {})
                current, field_name = claim_id, None
                continue
            if line.startswith("## "):
                self.err(
                    f"ara/logic/claims.md:{lineno}: heading is not a well-formed claim "
                    f"('## C<NN>: <title>'): {line.strip()!r}"
                )
                current, field_name = None, None
                continue
            if current is None:
                continue
            field = FIELD.match(line)
            if field:
                field_name = field.group(1)
                claims[current][field_name] = field.group(2).strip()
                continue
            if field_name and line.startswith("  ") and line.strip():
                joined = f"{claims[current][field_name]} {line.strip()}".strip()
                claims[current][field_name] = joined
                continue
            if not line.strip():
                field_name = None
        return claims

    def check_claim_fields(self, claims: dict[str, dict[str, str]]) -> None:
        for claim_id, fields in claims.items():
            for required in REQUIRED_CLAIM_FIELDS:
                if required not in fields:
                    self.err(f"claim {claim_id} is missing required field '{required}'")

    def check_claim_statuses(self, claims: dict[str, dict[str, str]]) -> None:
        for claim_id, fields in claims.items():
            status = fields.get("Status", "")
            if not status:
                continue
            first = status.split()[0].strip(".,;:").lower()
            if first not in STATUS_WORDS:
                self.err(
                    f"claim {claim_id} has status '{status}' whose disposition '{first}' is not "
                    f"one of {sorted(STATUS_WORDS)}"
                )

    def check_claim_dependencies(self, claims: dict[str, dict[str, str]]) -> None:
        known: dict[str, set[str]] = {}
        for prefix, relative in DEPENDENCY_NAMESPACES.items():
            known[prefix] = set(SECTION_ID.findall(self.read(relative)))
        for claim_id, fields in claims.items():
            for dep in re.findall(r"\b([CKAH]\d+)\b", fields.get("Dependencies", "")):
                prefix = dep[0]
                if dep == claim_id:
                    self.err(f"claim {claim_id} depends on itself")
                    continue
                if dep not in known.get(prefix, set()):
                    self.err(
                        f"claim {claim_id} depends on '{dep}' which is not crystallized in "
                        f"{DEPENDENCY_NAMESPACES[prefix]}"
                    )

    def check_claim_proofs(self, claims: dict[str, dict[str, str]]) -> None:
        for claim_id, fields in claims.items():
            proof = fields.get("Proof", "")
            if not proof:
                continue
            entries = re.findall(r"`([^`]+)`", proof) or [
                part.strip() for part in proof.strip("[]").split(",")
            ]
            cited_path = False
            for entry in entries:
                token = entry.strip().strip("`\"'[]")
                if not token.startswith(PATH_ROOTS):
                    continue
                cited_path = True
                # A claim may cite "path::case" or "path:line"; resolve the file part.
                path_part = token.split("::", 1)[0]
                if not (self.root / path_part).exists():
                    self.err(
                        f"claim {claim_id} cites proof path '{path_part}' which does not exist"
                    )
            disposition = (fields.get("Status", "").split() or [""])[0].lower()
            if disposition in {"supported", "refuted"} and not cited_path:
                self.err(
                    f"claim {claim_id} is '{disposition}' but its Proof cites no repository "
                    "artifact path (a disposed claim must be bound to evidence on disk)"
                )

    def check_staging_ids(self, claims: dict[str, dict[str, str]]) -> None:
        observations = self.read("ara/staging/observations.yaml")
        known = set(re.findall(r"^\s*-\s*id:\s*\"?(O\d+)\"?\s*$", observations, flags=re.MULTILINE))
        for claim_id, fields in claims.items():
            for obs in re.findall(r"\bO\d+\b", fields.get("From staging", "")):
                if obs not in known:
                    self.err(
                        f"claim {claim_id} came 'From staging: {obs}' but that observation is not "
                        "defined in ara/staging/observations.yaml"
                    )

    def check_ara_is_documented(self) -> None:
        if "ara/logic/claims.md" not in self.read("AGENTS.md"):
            self.err(
                "AGENTS.md does not mention ara/logic/claims.md; the contract must point at the "
                "claim ledger or agents will never find it"
            )

    def run(self) -> list[str]:
        self.check_required_files()
        self.check_paper_layer_index()
        claims = self.parse_claims()
        if not claims:
            self.err("ara/logic/claims.md defines no claims (expected at least one '## C<NN>:')")
        self.check_claim_fields(claims)
        self.check_claim_statuses(claims)
        self.check_claim_dependencies(claims)
        self.check_claim_proofs(claims)
        self.check_staging_ids(claims)
        self.check_ara_is_documented()
        self.claim_count = len(claims)
        return self.findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate the ara/ research artifact.")
    parser.add_argument("--root", default=".", help="Repository root path.")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit nonzero on any finding (warning mode reports and exits 0).",
    )
    args = parser.parse_args(argv)

    checker = Checker(Path(args.root).resolve())
    findings = checker.run()

    if findings:
        label = "error" if args.strict else "warning"
        print(f"check_ara_claims: {len(findings)} {label}(s):", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
        return 1 if args.strict else 0

    print(f"check_ara_claims: OK ({checker.claim_count} claims)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
