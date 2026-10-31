#!/usr/bin/env python3
"""ARCH-022 slice 2, S2a: unloads and reloads the spike plugin in the running Sandbox and asserts the S2a checks.

Starts the Sandbox like spike_driver.py (isolated socket, built-in config, the driver's LSan settings). Builds of
the plugin come from rewriting <build>/plugins/spike/generated/SpikeVersion.inc (its generation, or a Register that
stages and then fails) and building IntrinsicSpikePlugin; repository sources are never touched. Before the session
it prebuilds generation 2 (A), 3 (B), 4 (Register fails) and 6 (unresolved symbol); during it, it builds generation 5
and a broken header.
It checks, in order:
  1. a seeded scene (an imported mesh) saves to identical files twice (the comparison's own control);
  2. load A; set marker_size 12; unload: registries equal the pre-load baseline, the plugin's tools are unknown,
     the close found no live callable of A;
  3. load A again: the saved payload 12 is reapplied, the new instance got one change callback, the marker is 96 px;
  4. reload after a real build of generation 5 while the Sandbox runs: same PID and host binary, the new version
     answers, only it is invoked, registries equal the loaded state, 12 and the 96 px marker survive, the scene file
     is unchanged, the identity probes still share one address;
  5. a failed build leaves the plugin file and the running version unchanged;
  6. a failed load (generation 6 needs a symbol the host does not export), a failed Register (generation 4) and a
     failed commit (fail_at windows) each leave
     no plugin, with the reason, registries equal to the baseline and no live callable at the close;
  7. 20 reloads alternating A and B: version, registries and close records each cycle;
  8. the close guard: an unload with one plugin callable kept alive (test hook) unregisters everything but leaves the
     image mapped, with callables_alive; at exit the kept callable still runs on mapped code;
  8. a clean exit: status 0, no sanitizer report, every image's static construction matched by its destruction.
Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
Usage: python3 plugins/spike/reload_driver.py --build build/spike --out <new dir> [--icd <VK_DRIVER_FILES json>]
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spike_driver as s1  # noqa: E402  (shared session helpers)

CYCLES = 20
SCENE_MESH = s1.REPO / "tests/data/framework24-open-patch.obj"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Builds:
    """Plugin builds made by rewriting the generated version header."""

    def __init__(self, build: Path, out: Path) -> None:
        self.build, self.dir = build, out / "builds"
        self.dir.mkdir()
        self.header = build / "plugins/spike/generated/SpikeVersion.inc"
        self.plugin = build / "plugins/libIntrinsicSpikePlugin.so"

    def make(self, generation: int, fail_register: bool = False, broken: bool = False, missing: bool = False) -> bool:
        text = (f"#define SPIKE_GENERATION {generation}\n#define SPIKE_FAIL_REGISTER {int(fail_register)}\n"
                f"#define SPIKE_MISSING_SYMBOL {int(missing)}\n")
        self.header.write_text("#error a broken plugin build\n" if broken else text)
        result = subprocess.run(["cmake", "--build", str(self.build), "--target", "IntrinsicSpikePlugin"],
                                capture_output=True, text=True, timeout=1800)
        (self.dir / f"build-{generation}{'-broken' if broken else ''}.log").write_text(result.stdout + result.stderr)
        return result.returncode == 0

    def keep(self, name: str) -> Path:
        kept = self.dir / f"{name}.so"
        shutil.copyfile(self.plugin, kept)
        return kept


def host(agent: s1.Agent) -> dict:
    ok, value, _, _ = agent.call("plugin_host_identity")
    if not ok or not isinstance(value, dict):
        raise RuntimeError(f"plugin_host_identity failed: {value}")
    return value


def generation_of(reply: object) -> object:
    return reply.get("generation") if isinstance(reply, dict) else None


def last_close_clean(state: dict, generation: int) -> bool:
    close = state["closes"][-1] if state["closes"] else {}
    return close.get("generation") == generation and close.get("closed") is True and \
        all(count == 0 for count in close.get("live", {}).values())


def save_scene(agent: s1.Agent, out: Path, name: str) -> str:
    ok, value, _, _ = agent.call("save_scene", {"path": str(out / name)})
    if not ok:
        raise RuntimeError(f"save_scene failed: {value}")
    return sha256(out / name)


def run(agent: s1.Agent, checks: s1.Checks, builds: Builds, kept: dict[str, Path], out: Path) -> None:
    ok, diagnostics, _, _ = agent.call("diagnostics_read")
    status = s1.find(diagnostics, "device_status")
    checks.check("environment: Vulkan device operational",
                 ok and isinstance(status, dict) and status.get("is_operational") is True, status)

    # 1. A seeded scene and the comparison's own control: two saves without a change are identical.
    shutil.copyfile(SCENE_MESH, out / SCENE_MESH.name)
    ok, imported, _, _ = agent.call("import_file", {"path": str(out / SCENE_MESH.name), "wait": True})
    checks.check("scene: the mesh imported", ok, imported)
    scene = save_scene(agent, out, "scene-0a.scene")
    checks.check("scene: two saves without a change are identical (control)",
                 scene == save_scene(agent, out, "scene-0b.scene"), scene)

    # 2. Load, set the marker, unload.
    baseline = s1.registries(agent)
    ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"])})
    checks.check("load A: committed", ok and generation_of(reply) == 2, [reply, error])
    ok, applied, _, _ = agent.call("config_apply", {"section": "spike", "payload": {"marker_size": 12}})
    checks.check("load A: marker_size 12 applied", ok, applied)
    loaded = s1.registries(agent)  # with 12 active, as every later state should be
    ok, reply, _, error = agent.call("plugin_unload")
    state = host(agent)
    checks.check("unload: accepted", ok, [reply, error])
    checks.check("unload: tools, config sections and windows equal the pre-load baseline",
                 s1.registries(agent) == baseline, "changed")
    reply = agent.request("tools/call", {"name": "spike_echo", "arguments": {"text": "gone"}})
    checks.check("unload: the plugin's tools answer as unknown",
                 str(reply.get("error", {}).get("message", "")).startswith("Unknown tool"), reply)
    checks.check("unload: no live callable of generation 2 at the close", last_close_clean(state, 2), state["closes"][-1:])
    checks.check("unload: the image left the process (reported)", state["closes"][-1].get("unmapped") is True,
                 state["closes"][-1:])

    # 3. Load again: the saved payload comes back through the new registration.
    ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"])})
    checks.check("load again: the saved payload 12 is reapplied",
                 ok and isinstance(reply, dict) and reply.get("config", {}).get("restored") == ["spike"], [reply, error])
    _, seen, _, _ = agent.call("spike_identity")
    checks.check("load again: the new instance got one change callback with 12",
                 seen.get("marker_size") == 12 and seen.get("config_changes") == 1, seen)
    s1.expect_square(agent, checks, "load_again", 12 * 8)

    # 4. Reload after a real build while the Sandbox runs.
    before = host(agent)
    sandbox_binary = builds.build / "bin/ExtrinsicSandbox"
    binary_sha = sha256(sandbox_binary)
    built = builds.make(5)
    checks.check("reload: generation 5 built while the Sandbox runs", built)
    after_build = host(agent)
    checks.check("reload: the Sandbox kept its PID and binary across the plugin build",
                 after_build["pid"] == before["pid"] and sha256(sandbox_binary) == binary_sha,
                 [before["pid"], after_build["pid"]])
    ok, reply, _, error = agent.call("plugin_reload", {"path": str(builds.plugin)})
    checks.check("reload: generation 5 committed", ok and generation_of(reply) == 5, [reply, error])
    state = host(agent)
    checks.check("reload: no live callable of generation 2 at the close", last_close_clean(state, 2), state["closes"][-1:])
    ok, version, _, _ = agent.call("spike_version")
    checks.check("reload: the new version answers", ok and version == {"generation": 5}, version)
    invoked = host(agent)["invoked"]
    agent.call("spike_echo", {"text": "new"})
    now = host(agent)["invoked"]
    changed = {gen: kinds for gen, kinds in now.items() if kinds != invoked.get(gen)}
    checks.check("reload: only generation 5 is invoked", set(changed) == {"5"}, changed)
    checks.check("reload: tools, config sections and windows equal the loaded state", s1.registries(agent) == loaded,
                 "changed")
    _, seen, _, _ = agent.call("spike_identity")
    checks.check("reload: 12 survived, and the new instance got one change callback with it",
                 seen.get("marker_size") == 12 and seen.get("config_changes") == 1, seen)
    s1.expect_square(agent, checks, "reloaded", 12 * 8)
    checks.check("reload: the scene file is unchanged", save_scene(agent, out, "scene-1.scene") == scene, "differs")
    _, probes, _, _ = agent.call("plugin_host_identity")
    checks.check("reload: the identity probes still share one address",
                 all(seen[p]["address"] == probes[p]["address"] for p in ("counter", "inline_static", "thread_local")),
                 [seen.get("counter"), probes.get("counter")])

    # 5. A failed build.
    plugin_sha = sha256(builds.plugin)
    checks.check("failed build: the build fails", not builds.make(5, broken=True))
    checks.check("failed build: the plugin file is unchanged", sha256(builds.plugin) == plugin_sha)
    ok, version, _, _ = agent.call("spike_version")
    checks.check("failed build: the running version still answers", ok and version == {"generation": 5}, version)
    builds.header.write_text("#define SPIKE_GENERATION 5\n#define SPIKE_FAIL_REGISTER 0\n#define SPIKE_MISSING_SYMBOL 0\n")

    # 6. Failed load, failed Register, failed commit: no plugin, a reason, nothing left registered.
    for name, arguments, code, generation in (
            ("failed load", {"path": str(kept["missing"])}, "dlopen_failed", None),
            ("failed Register", {"path": str(kept["F"])}, "register_failed", 4),
            ("failed commit", {"path": str(kept["A"]), "fail_at": "windows"}, "commit_failed", 2)):
        ok, reply, _, error = agent.call("plugin_reload", arguments)
        state = host(agent)
        checks.check(f"{name}: fails with {code} and a reason", not ok and error == code and bool(reply), [error, reply])
        checks.check(f"{name}: no plugin is loaded", state["loaded"] is None, state["loaded"])
        checks.check(f"{name}: registries equal the baseline", s1.registries(agent) == baseline, "changed")
        if generation is not None:
            checks.check(f"{name}: no live callable of generation {generation} at its close",
                         last_close_clean(state, generation), state["closes"][-1:])
        ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"])})
        checks.check(f"{name}: a later load works", ok and generation_of(reply) == 2, [reply, error])

    # 7. Reload cycles.
    cycle_failures = []
    for cycle in range(CYCLES):
        name, generation, previous = ("B", 3, 2) if cycle % 2 == 0 else ("A", 2, 3)
        ok, reply, _, error = agent.call("plugin_reload", {"path": str(kept[name])})
        _, version, _, _ = agent.call("spike_version")
        state = host(agent)
        problems = [what for what, good in (
            ("reload", ok and generation_of(reply) == generation),
            ("version", version == {"generation": generation}),
            ("close", last_close_clean(state, previous)),
            ("registries", s1.registries(agent) == loaded)) if not good]
        if problems:
            cycle_failures.append({"cycle": cycle, "problems": problems, "reply": reply, "error": error})
    checks.check(f"cycles: {CYCLES} reloads, each with the right version, a clean close and the loaded registries",
                 not cycle_failures, cycle_failures[:3])
    _, seen, _, _ = agent.call("spike_identity")
    checks.check("cycles: 12 is still active", seen.get("marker_size") == 12, seen)

    # 8. The close guard: with a plugin callable kept alive, the unload unregisters everything but must not close.
    ok, reply, _, error = agent.call("plugin_unload", {"retain_callable": True})
    state = host(agent)
    close = state["closes"][-1] if state["closes"] else {}
    checks.check("guard: the unload fails with callables_alive", not ok and error == "callables_alive", [error, reply])
    checks.check("guard: the image was not closed, with one live operation of generation 2",
                 close.get("generation") == 2 and close.get("closed") is False and close.get("live", {}).get("operation") == 1,
                 close)
    checks.check("guard: no plugin is registered", state["loaded"] is None and s1.registries(agent) == baseline, "changed")
    checks.check("exit: app_quit accepted", agent.quit())


def main() -> int:
    args = s1.arguments(__doc__)
    checks = s1.Checks()
    builds = Builds(args.build, args.out)
    kept: dict[str, Path] = {}
    for name, generation, fail, missing in (("A", 2, False, False), ("B", 3, False, False), ("F", 4, True, False),
                                            ("missing", 6, False, True)):
        if not checks.check(f"prebuild: generation {generation} ({name}) built", builds.make(generation, fail, missing=missing)):
            return s1.finish(checks, args.out)
        kept[name] = builds.keep(name)
    result = s1.launch(args, checks, lambda agent: run(agent, checks, builds, kept, args.out))
    if result is not None:
        _, _, lines = result
        # The image the guard kept open is destroyed at exit like every other.
        constructed, destructed = lines.count("static_construct"), lines.count("static_destruct")
        checks.check("lifecycle: every image's static construction matched by its destruction",
                     constructed == destructed and constructed > CYCLES, [constructed, destructed])
    return s1.finish(checks, args.out)


if __name__ == "__main__":
    sys.exit(main())
