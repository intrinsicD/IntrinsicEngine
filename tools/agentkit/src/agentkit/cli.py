"""agentkit command-line interface.

Commands:
  init         Scaffold the agentic workflow into a repository.
  check        Run the shipped validators (local preview of the CI gate).
  doctor       Report which workflow files are present / missing / drifted.
  resync       Re-mirror docs/agent/* into skill references/.
  new-task     Create a task file in tasks/backlog/ from a template.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import __version__
from . import config as cfgmod
from . import bootstrap, check_runner, doctor, newtask, resync


def _add_init(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("init", help="scaffold the agentic workflow into a repository")
    p.add_argument("--path", default=".", help="target repository root (default: .)")
    p.add_argument("--name", help="human-readable project name (required unless --config)")
    p.add_argument("--slug", help="short slug for skill names (default: derived from name)")
    p.add_argument("--description", default="", help="one-line project description")
    p.add_argument("--language", default="your project's language", help="primary language/stack")
    p.add_argument("--contract-file", default="AGENTS.md", help="contract filename (default: AGENTS.md)")
    p.add_argument("--config", help="use an existing agentkit.toml instead of generating defaults")
    p.add_argument("--no-claude", action="store_true", help="skip Claude surfaces (.claude, CLAUDE.md)")
    p.add_argument("--no-codex", action="store_true", help="skip Codex surfaces (.codex)")
    p.add_argument("--no-copilot", action="store_true", help="skip Copilot instructions")
    p.add_argument("--no-setup-hook", action="store_true", help="skip the session setup hook scripts")
    p.add_argument("--dry-run", action="store_true", help="show what would change without writing")
    p.add_argument("--force", action="store_true", help="overwrite existing files")


def _load_or_build_config(args: argparse.Namespace) -> dict:
    if args.config:
        with open(args.config, "rb") as handle:
            import tomllib

            return tomllib.load(handle)
    if not args.name:
        raise SystemExit("init: --name is required (or pass --config)")
    harness = {
        "claude": not args.no_claude,
        "codex": not args.no_codex,
        "copilot": not args.no_copilot,
        "setup_hook": not args.no_setup_hook,
    }
    return cfgmod.default_config(
        name=args.name,
        slug=args.slug,
        description=args.description,
        language=args.language,
        contract_file=args.contract_file,
        harness=harness,
    )


def _cmd_init(args: argparse.Namespace) -> int:
    target = Path(args.path).resolve()
    target.mkdir(parents=True, exist_ok=True)
    cfg = _load_or_build_config(args)
    gen = bootstrap.Generator(target, cfg, dry_run=args.dry_run, force=args.force)
    results = gen.run()

    created = sum(1 for a, _ in results if a == "create")
    overwritten = sum(1 for a, _ in results if a == "overwrite")
    skipped = sum(1 for a, _ in results if a.startswith("skip"))
    for action, rel in results:
        print(f"  {action:<10} {rel}")
    verb = "Would scaffold" if args.dry_run else "Scaffolded"
    print(
        f"\n{verb} agentkit workflow for '{cfg['project']['name']}' into {target}\n"
        f"  {created} created, {overwritten} overwritten, {skipped} skipped."
    )
    if not args.dry_run:
        print(
            "\nNext steps:\n"
            f"  1. Edit {cfgmod.CONFIG_FILENAME}: fill in [commands] (build/test) and task prefixes.\n"
            f"  2. Fill the TODO placeholders in {cfg['project']['contract_file']} and .claude/setup.sh.\n"
            f"  3. Run: python3 {cfgmod.TOOLS_DIR}/check.py --strict\n"
        )
    return 0


def _cmd_check(args: argparse.Namespace) -> int:
    return check_runner.run_checks(Path(args.path), strict=args.strict)


def _cmd_doctor(args: argparse.Namespace) -> int:
    target = Path(args.path)
    try:
        cfg = cfgmod.load(target)
    except FileNotFoundError as exc:
        print(f"[agentkit] {exc}")
        return 2
    present, missing, drift = doctor.diagnose(target, cfg)
    print(f"[agentkit] doctor for '{cfgmod.get(cfg, 'project.name')}' at {target.resolve()}")
    print(f"  present: {len(present)}   missing: {len(missing)}   drifted references: {len(drift)}")
    for rel in missing:
        print(f"  MISSING  {rel}")
    for rel in drift:
        print(f"  DRIFT    {rel}  (run `agentkit resync`)")
    if not missing and not drift:
        print("  OK — workflow is complete and references are in sync.")
    return 1 if (missing or drift) else 0


def _cmd_resync(args: argparse.Namespace) -> int:
    target = Path(args.path)
    try:
        cfg = cfgmod.load(target)
    except FileNotFoundError as exc:
        print(f"[agentkit] {exc}")
        return 2
    slug = cfgmod.get(cfg, "project.slug", "project")
    contract_file = cfgmod.get(cfg, "project.contract_file", "AGENTS.md")
    for action, rel in resync.resync(target, slug, contract_file):
        print(f"  {action:<22} {rel}")
    return 0


def _cmd_new_task(args: argparse.Namespace) -> int:
    try:
        dest, warnings = newtask.create_task(Path(args.path), args.id, args.title, args.template)
    except (ValueError, FileNotFoundError, FileExistsError) as exc:
        print(f"[agentkit] {exc}")
        return 1
    for warning in warnings:
        print(f"[agentkit] WARNING: {warning}")
    print(f"[agentkit] created {dest}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="agentkit",
        description="Bootstrap a portable, contract-driven agentic workflow into any repository.",
    )
    parser.add_argument("--version", action="version", version=f"agentkit {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    _add_init(sub)

    p_check = sub.add_parser("check", help="run shipped validators (local CI preview)")
    p_check.add_argument("--path", default=".", help="repository root (default: .)")
    p_check.add_argument("--strict", action="store_true", help="fail on findings")

    p_doctor = sub.add_parser("doctor", help="report present / missing / drifted workflow files")
    p_doctor.add_argument("--path", default=".", help="repository root (default: .)")

    p_resync = sub.add_parser("resync", help="re-mirror docs/agent into skill references")
    p_resync.add_argument("--path", default=".", help="repository root (default: .)")

    p_task = sub.add_parser("new-task", help="create a task file in tasks/backlog")
    p_task.add_argument("id", help="task id, e.g. FEAT-001")
    p_task.add_argument("title", help="short task title")
    p_task.add_argument("--template", default="task", choices=["task", "bug", "review"])
    p_task.add_argument("--path", default=".", help="repository root (default: .)")

    return parser


_HANDLERS = {
    "init": _cmd_init,
    "check": _cmd_check,
    "doctor": _cmd_doctor,
    "resync": _cmd_resync,
    "new-task": _cmd_new_task,
}


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return _HANDLERS[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
