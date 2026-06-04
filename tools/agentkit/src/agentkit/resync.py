"""Re-mirror authoritative docs into skill ``references/`` directories.

Skill references are verbatim copies of ``docs/agent/*`` (and the contract).
This keeps the auto-discovered skill surface in lockstep with the source docs,
matching the model used by ``tools/agent/resync_skills.sh`` in generated repos.
"""
from __future__ import annotations

from pathlib import Path

from . import config as cfgmod


def resync(target: Path, slug: str, contract_file: str = "AGENTS.md") -> list[tuple[str, str]]:
    target = Path(target)
    results: list[tuple[str, str]] = []
    for src_rel, skill_dir, ref_name in cfgmod.build_resync_map(contract_file):
        src = target / src_rel
        dst_rel = f"{cfgmod.SKILLS_DIR}/{slug}-{skill_dir}/references/{ref_name}"
        dst = target / dst_rel
        if not src.is_file():
            results.append((f"missing-source ({src_rel})", dst_rel))
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
        results.append(("synced", dst_rel))
    return results
