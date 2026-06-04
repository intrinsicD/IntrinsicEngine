"""The generator: scaffold the agentic workflow into a target repository."""
from __future__ import annotations

import os
import stat
from pathlib import Path
from typing import Any

from . import config as cfgmod
from .render import render

TEMPLATES_DIR = Path(__file__).parent / "templates"

# (template path under templates/, output path under target root, mode)
# mode: "render" -> substitute {{ }} ; "copy" -> verbatim.
_EXECUTABLE = {".claude/setup.sh", ".claude/wait-for-setup.sh", "tools/agent/resync_skills.sh"}


def _toml_string(value: str) -> str:
    if "\\" in value and "'" not in value and "\n" not in value:
        return f"'{value}'"  # literal string: no escaping (handy for regexes)
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _toml_scalar(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, list):
        return "[" + ", ".join(_toml_string(str(v)) for v in value) + "]"
    return _toml_string(str(value))


def dump_toml(cfg: dict[str, Any], *, header: str = "") -> str:
    """Serialize a two-level config dict to TOML (stdlib-only writer)."""
    lines: list[str] = []
    if header:
        lines.extend(f"# {line}" if line else "#" for line in header.splitlines())
        lines.append("")
    for section, body in cfg.items():
        lines.append(f"[{section}]")
        for key, value in body.items():
            lines.append(f"{key} = {_toml_scalar(value)}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


class Generator:
    def __init__(
        self,
        target: Path,
        cfg: dict[str, Any],
        *,
        dry_run: bool = False,
        force: bool = False,
    ) -> None:
        self.target = Path(target)
        self.cfg = cfg
        self.ctx = cfgmod.build_context(cfg)
        self.dry_run = dry_run
        self.force = force
        self.results: list[tuple[str, str]] = []  # (action, rel_path)

    # --- low-level helpers --------------------------------------------------
    def _record(self, action: str, rel: str) -> None:
        self.results.append((action, rel))

    def _write(self, rel: str, content: str, *, executable: bool = False) -> None:
        dest = self.target / rel
        exists = dest.exists()
        if exists and not self.force:
            self._record("skip", rel)
            return
        action = "overwrite" if exists else "create"
        if not self.dry_run:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(content, encoding="utf-8")
            if executable:
                mode = dest.stat().st_mode
                dest.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        self._record(action, rel)

    def _template(self, tmpl_rel: str) -> str:
        return (TEMPLATES_DIR / tmpl_rel).read_text(encoding="utf-8")

    def _emit(self, tmpl_rel: str, out_rel: str, *, mode: str = "render") -> None:
        text = self._template(tmpl_rel)
        if mode == "render":
            text = render(text, self.ctx)
        self._write(out_rel, text, executable=out_rel in _EXECUTABLE)

    def _symlink(self, link_rel: str, target_rel: str) -> None:
        link = self.target / link_rel
        if link.is_symlink() or link.exists():
            if not self.force:
                self._record("skip", link_rel)
                return
            if not self.dry_run:
                if link.is_dir() and not link.is_symlink():
                    self._record("skip(dir-exists)", link_rel)
                    return
                link.unlink()
        if not self.dry_run:
            link.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.symlink(target_rel, link, target_is_directory=True)
            except OSError as exc:  # pragma: no cover - platform dependent
                self._record(f"symlink-failed ({exc.strerror})", link_rel)
                return
        self._record("symlink", link_rel)

    # --- the manifest -------------------------------------------------------
    def run(self) -> list[tuple[str, str]]:
        slug = self.ctx["PROJECT_SLUG"]
        harness = self.cfg.get("harness", {})

        # 1. agentkit.toml (serialized, not templated).
        header = (
            f"agentkit configuration for {self.ctx['PROJECT_NAME']}.\n"
            "Single source of truth for the generator and the shipped checks.\n"
            "Edit, then run: python3 tools/agentkit/agentkit.py check --strict"
        )
        self._write(cfgmod.CONFIG_FILENAME, dump_toml(self.cfg, header=header))

        # 2. Contract + thin redirects.
        self._emit("AGENTS.md.tmpl", "AGENTS.md")
        if harness.get("claude", True):
            self._emit("CLAUDE.md.tmpl", "CLAUDE.md")
        if harness.get("copilot", True):
            self._emit("copilot-instructions.md.tmpl", ".github/copilot-instructions.md")
        if harness.get("codex", True):
            self._emit("codex-config.yaml.tmpl", ".codex/config.yaml")

        # 3. Session hooks (Claude on the web / desktop).
        if harness.get("claude", True):
            self._emit("claude/settings.json.tmpl", ".claude/settings.json")
            if harness.get("setup_hook", True):
                self._emit("claude/setup.sh.tmpl", ".claude/setup.sh")
                self._emit("claude/wait-for-setup.sh.tmpl", ".claude/wait-for-setup.sh")

        # 4. Generic process docs.
        for doc in cfgmod.AGENT_DOCS:
            self._emit(f"docs/agent/{doc}.tmpl", f"docs/agent/{doc}")

        # 5. Task system.
        self._emit("tasks/README.md.tmpl", "tasks/README.md")
        self._emit("tasks/active-README.md.tmpl", "tasks/active/README.md")
        self._write("tasks/backlog/.gitkeep", "")
        self._write("tasks/done/.gitkeep", "")
        for tmpl in cfgmod.TASK_TEMPLATES:
            self._emit(f"tasks/templates/{tmpl}", f"tasks/templates/{tmpl}", mode="copy")

        # 6. tools/agent: checks + helpers.
        self._emit("tools/agent/README.md.tmpl", f"{cfgmod.TOOLS_DIR}/README.md")
        self._emit("tools/agent/resync_skills.sh.tmpl", f"{cfgmod.TOOLS_DIR}/resync_skills.sh")
        self._emit("tools/agent/docs_sync_rules.toml.tmpl", self.ctx["RULES_FILE"])
        for check in cfgmod.CHECK_FILES:
            self._emit(
                f"tools/agent/checks/{check}", f"{cfgmod.CHECKS_DIR}/{check}", mode="copy"
            )

        # 7. Skills (core router + specialists).
        self._emit("tools/agent/skills/README.md.tmpl", f"{cfgmod.SKILLS_DIR}/README.md")
        for skill_dir in cfgmod.SKILL_DIRS:
            self._emit(
                f"tools/agent/skills/{skill_dir}/SKILL.md.tmpl",
                f"{cfgmod.SKILLS_DIR}/{slug}-{skill_dir}/SKILL.md",
            )

        # 8. Skill references = verbatim copies of the authoritative docs.
        self._mirror_references(slug)

        # 9. Skill discovery symlinks for harnesses that auto-load .claude/.codex.
        if harness.get("claude", True):
            self._symlink(".claude/skills", "../tools/agent/skills")
        if harness.get("codex", True):
            self._symlink(".codex/skills", "../tools/agent/skills")

        # 10. CI + PR template.
        self._emit("github/workflows/pr-fast.yml.tmpl", ".github/workflows/pr-fast.yml")
        self._emit("github/workflows/ci-docs.yml.tmpl", ".github/workflows/ci-docs.yml")
        self._emit("github/pull_request_template.md.tmpl", ".github/pull_request_template.md")

        return self.results

    def _mirror_references(self, slug: str) -> None:
        for src_rel, skill_dir, ref_name in cfgmod.RESYNC_MAP:
            src = self.target / src_rel
            out_rel = f"{cfgmod.SKILLS_DIR}/{slug}-{skill_dir}/references/{ref_name}"
            if self.dry_run:
                self._record("mirror", out_rel)
                continue
            if not src.is_file():
                self._record(f"mirror-skip (missing {src_rel})", out_rel)
                continue
            self._write(out_rel, src.read_text(encoding="utf-8"))
