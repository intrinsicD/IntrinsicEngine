"""Minimal, dependency-free template rendering.

Placeholders are ``{{ UPPER_SNAKE_CASE }}``. The pattern only matches
uppercase identifiers, so it never collides with GitHub Actions
``${{ github.sha }}`` expressions or shell ``${VAR}`` substitutions that appear
verbatim in the templates. Rendering is strict: an unknown placeholder raises,
which turns template typos into immediate, loud failures instead of silent
empty substitutions.
"""
from __future__ import annotations

import re

_PLACEHOLDER = re.compile(r"\{\{\s*([A-Z][A-Z0-9_]*)\s*\}\}")


def render(text: str, context: dict[str, object]) -> str:
    """Substitute ``{{ KEY }}`` placeholders in *text* from *context*."""

    def _replace(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in context:
            raise KeyError(f"template referenced unknown placeholder: {{{{ {key} }}}}")
        return str(context[key])

    return _PLACEHOLDER.sub(_replace, text)


def find_placeholders(text: str) -> set[str]:
    """Return the set of placeholder keys referenced in *text*."""
    return {match.group(1) for match in _PLACEHOLDER.finditer(text)}
