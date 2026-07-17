#!/usr/bin/env python3
"""Strict parser for declared CTest cohort transitions."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


MANIFEST_SCHEMA = "intrinsic.test-cohort-transition/v1"


class CohortManifestError(RuntimeError):
    pass


@dataclass(frozen=True)
class TestCohortTransition:
    moved_to_slow: tuple[str, ...]
    added_fast_sentinels: tuple[str, ...]


def _read_json_object(path: Path) -> Mapping[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise CohortManifestError(
            f"cannot read transition manifest {path}: {error}"
        ) from error
    except json.JSONDecodeError as error:
        raise CohortManifestError(
            f"transition manifest {path} is invalid JSON: {error}"
        ) from error
    if not isinstance(document, dict):
        raise CohortManifestError(
            f"transition manifest {path} must be a JSON object"
        )
    return document


def _string_list(
    value: object,
    *,
    context: str,
    require_nonempty: bool,
) -> tuple[str, ...]:
    if not isinstance(value, list) or any(
        not isinstance(item, str) or not item for item in value
    ):
        raise CohortManifestError(f"{context} must be a list of nonempty strings")
    result = tuple(value)
    if require_nonempty and not result:
        raise CohortManifestError(f"{context} must not be empty")
    if len(result) != len(set(result)):
        raise CohortManifestError(f"{context} contains duplicate values")
    if result != tuple(sorted(result)):
        raise CohortManifestError(f"{context} must be sorted")
    return result


def read_test_cohort_manifest(path: Path) -> TestCohortTransition:
    document = _read_json_object(path)
    expected_keys = {"added_fast_sentinels", "moved_to_slow", "schema"}
    if set(document) != expected_keys:
        raise CohortManifestError(
            "transition manifest keys must be exactly "
            f"{sorted(expected_keys)!r}, got {sorted(document)!r}"
        )
    if document["schema"] != MANIFEST_SCHEMA:
        raise CohortManifestError(
            "transition manifest schema must be "
            f"{MANIFEST_SCHEMA!r}, got {document['schema']!r}"
        )
    moved = _string_list(
        document["moved_to_slow"],
        context="transition manifest moved_to_slow",
        require_nonempty=True,
    )
    sentinels = _string_list(
        document["added_fast_sentinels"],
        context="transition manifest added_fast_sentinels",
        require_nonempty=False,
    )
    overlap = sorted(set(moved).intersection(sentinels))
    if overlap:
        raise CohortManifestError(
            f"transition manifest repeats moved cases as fast sentinels: {overlap!r}"
        )
    return TestCohortTransition(
        moved_to_slow=moved,
        added_fast_sentinels=sentinels,
    )
