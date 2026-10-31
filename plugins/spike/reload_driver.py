#!/usr/bin/env python3
"""ARCH-022 slice 2, S2a: unloads and reloads the spike plugin in the running Sandbox and asserts the S2a checks.

Starts the Sandbox like spike_driver.py (isolated socket, built-in config, the driver's LSan settings). Builds of
the plugin come from rewriting <build>/plugins/spike/generated/SpikeVersion.inc (its generation, or a Register that
stages and then fails) and building IntrinsicSpikePlugin; repository sources are never touched. Before the session
it prebuilds generation 2 (A), 3 (B), 4 (Register fails), 6 (unresolved symbol) and 7 (clamping validator); during
it, it builds generation 5 and a broken header.
It checks, in order:
  1. a seeded scene (an imported mesh, renamed, moved and scaled through a loaded scene file) saves to identical
     files twice (the comparison's own control);
  2. load A (every callable kind counted); set marker_size 12; unload: registries equal the pre-load baseline, the
     plugin's tools are unknown, the close found every kind at zero and removed the copy; a pending load dropped
     as a disconnect would is closed the same way;
  3. load A again: the saved payload 12 is reapplied, the new instance got one change callback, the marker is 96 px;
  4. reload after a real build of generation 5 while the Sandbox runs: same PID and host binary, the new version
     answers, only it is invoked, registries equal the loaded state, 12 and the 96 px marker survive, the scene file
     is unchanged, the identity probes still share one address;
  5. a failed build leaves the plugin file and the running version unchanged;
  6. a failed load (generation 6 needs a symbol the host does not export), a failed Register (generation 4) and a
     failed commit (fail_at windows) each leave
     no plugin, with the reason, registries equal to the baseline and no live callable at the close;
  7. 20 reloads alternating A and B: version, registries and close records each cycle; then a build whose validator
     clamps 12 to 10 with a fallback: the saved 12 is reported rejected and not applied (the default 8 stays);
  8. undo and redo across a reload (S2b): add 5 and 3, undo, reload, then undo, redo, redo give 0, 5, 8, served
     only by the new generation's registered history command;
  9. the close guard: an unload with one plugin callable kept alive (test hook) unregisters everything but leaves the
     image mapped, with callables_alive;
  10. a clean exit: status 0, no sanitizer report, every image's static construction matched by its destruction.
Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
With --closure-control it runs the S2b control instead: today's closure entry, recorded by the plugin, is undone
after the plugin's image left the process. The control counts only if the entry was listed, the image was unmapped,
the host logged `closure_undo` as its last event, and the Sandbox then died with a sanitizer SEGV whose address lies
in the closed image's former range, with EditorCommandHistory::Undo on the stack. A still-mapped image makes it
inconclusive.
Usage: python3 plugins/spike/reload_driver.py --build build/spike --out <new dir> [--icd <VK_DRIVER_FILES json>]
       [--closure-control]
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spike_driver as s1  # noqa: E402  (shared session helpers)

CYCLES = 20
SCENE_MESH = s1.REPO / "tests/data/framework24-open-patch.obj"
# Every kind of callable the plugin hands over; each must be counted while loaded and appear at every close.
KINDS = {"validator", "change_callback", "operation", "window_draw", "window_state", "history_handler"}
SEEDED = {"name": "SpikeSeeded", "position": [1.25, -0.5, 2.0], "scale": [1.5, 1.5, 1.5]}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Builds:
    """Plugin builds made by rewriting the generated version header."""

    def __init__(self, build: Path, out: Path) -> None:
        self.build, self.dir = build, out / "builds"
        self.dir.mkdir()
        self.header = build / "plugins/spike/generated/SpikeVersion.inc"
        self.plugin = build / "plugins/libIntrinsicSpikePlugin.so"

    def write_header(self, generation: int, fail_register: bool = False, missing: bool = False,
                     clamp: bool = False) -> None:
        self.header.write_text(f"#define SPIKE_GENERATION {generation}\n#define SPIKE_FAIL_REGISTER {int(fail_register)}\n"
                               f"#define SPIKE_MISSING_SYMBOL {int(missing)}\n#define SPIKE_CLAMP_MARKER {int(clamp)}\n")

    def make(self, generation: int, fail_register: bool = False, broken: bool = False, missing: bool = False,
             clamp: bool = False) -> bool:
        if broken:
            self.header.write_text("#error a broken plugin build\n")
        else:
            self.write_header(generation, fail_register, missing, clamp)
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
    """The last close: this generation, closed with every kind counted and none alive, unmapped, its copy gone."""
    close = state["closes"][-1] if state["closes"] else {}
    return close.get("generation") == generation and s1.clean_close(close) and set(close.get("live", {})) == KINDS \
        and not Path(close.get("copy", "/")).exists()


def counted_while_loaded(state: dict) -> bool:
    """Every kind of the loaded generation has live callables, so a missing token cannot pass as zero."""
    live = state["live"].get(str(state["loaded"]), {})
    return all(live.get(kind, 0) > 0 for kind in KINDS)


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

    # 1. A seeded scene (an imported mesh, renamed, moved and scaled through a scene file) and the comparison's own
    # control: two saves without a change are identical.
    shutil.copyfile(SCENE_MESH, out / SCENE_MESH.name)
    ok, imported, _, _ = agent.call("import_file", {"path": str(out / SCENE_MESH.name), "wait": True})
    checks.check("scene: the mesh imported", ok, imported)
    save_scene(agent, out, "scene-imported.scene")
    document = json.loads((out / "scene-imported.scene").read_text())
    mesh = next(e for e in document["entities"] if e.get("name") != "ReferenceTriangle")
    mesh["name"] = SEEDED["name"]
    mesh["transform"]["position"], mesh["transform"]["scale"] = SEEDED["position"], SEEDED["scale"]
    (out / "scene-seeded.scene").write_text(json.dumps(document, indent=2))
    ok, loaded_scene, _, _ = agent.call("load_scene", {"path": str(out / "scene-seeded.scene")})
    scene = save_scene(agent, out, "scene-0a.scene")
    seeded = next((e for e in json.loads((out / "scene-0a.scene").read_text())["entities"]
                   if e.get("name") == SEEDED["name"]), {})
    checks.check("scene: the seeded entity is renamed, moved and scaled",
                 ok and seeded.get("transform", {}).get("position") == SEEDED["position"]
                 and seeded["transform"].get("scale") == SEEDED["scale"], [loaded_scene, seeded.get("transform")])
    checks.check("scene: two saves without a change are identical (control)",
                 scene == save_scene(agent, out, "scene-0b.scene"), scene)

    # 2. Load, set the marker, unload.
    baseline = s1.registries(agent)
    ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"])})
    checks.check("load A: committed", ok and generation_of(reply) == 2, [reply, error])
    checks.check("load A: every callable kind is counted while loaded", counted_while_loaded(host(agent)),
                 host(agent)["live"])
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

    # 2b. A pending load dropped as a disconnect would: its image is closed, its copy removed, a retry works.
    closes_before = len(host(agent)["closes"])
    ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"]), "simulate_drop": True})
    state = host(agent)
    checks.check("dropped load: the pending image is closed with nothing alive, unmapped, its copy removed",
                 ok and isinstance(reply, dict) and reply.get("dropped") is True
                 and len(state["closes"]) == closes_before + 1 and last_close_clean(state, 2), [reply, error])
    checks.check("dropped load: no plugin is loaded", host(agent)["loaded"] is None)

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
    builds.write_header(5)

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

    # 7b. A saved payload the new validator would change (12 clamped to 10 with a fallback) is not applied.
    ok, reply, _, error = agent.call("plugin_reload", {"path": str(kept["clamp"])})
    _, seen, _, _ = agent.call("spike_identity")
    checks.check("clamp: the reload reports the saved payload rejected and keeps the default 8, no change callback",
                 ok and generation_of(reply) == 7 and reply.get("config", {}).get("rejected") == ["spike"]
                 and seen.get("marker_size") == 8 and seen.get("config_changes") == 0, [reply, error, seen])

    # 8. Undo and redo through the plugin across a reload (S2b): both stacks hold an entry when it reloads.
    start = host(agent)["undo_value"]
    for delta in (5, 3):
        ok, reply, _, error = agent.call("spike_counter_add", {"delta": delta})
        checks.check(f"history: add {delta} recorded", ok, [reply, error])
    undone = agent.call("undo")[0]
    _, history, _, _ = agent.call("history")
    checks.check("history: before the reload, the undo entry holds 5 and the redo entry 3",
                 undone and str(history.get("undo_label", "")).endswith("Spike add 5")
                 and str(history.get("redo_label", "")).endswith("Spike add 3") and host(agent)["undo_value"] == start + 5,
                 history)
    invoked = host(agent)["invoked"]
    ok, reply, _, error = agent.call("plugin_reload", {"path": str(kept["B"])})
    checks.check("history: reloaded to generation 3", ok and generation_of(reply) == 3, [reply, error])
    values: list[object] = []
    for step in ("undo", "redo", "redo"):
        ok, reply, _, error = agent.call(step)
        values.append(host(agent)["undo_value"] - start if ok else f"{step} failed: {error or reply}")
    checks.check("history: after the reload, undo, redo, redo give 0, 5, 8", values == [0, 5, 8], values)
    now = host(agent)["invoked"]
    handlers = {gen: kinds.get("history_handler", 0) - invoked.get(gen, {}).get("history_handler", 0)
                for gen, kinds in now.items()}
    checks.check("history: only generation 3's handler ran after the reload, three times",
                 {gen: n for gen, n in handlers.items() if n} == {"3": 3}, handlers)

    # 9. The close guard: with a plugin callable kept alive, the unload unregisters everything but must not close.
    ok, reply, _, error = agent.call("plugin_unload", {"retain_callable": True})
    state = host(agent)
    close = state["closes"][-1] if state["closes"] else {}
    checks.check("guard: the unload fails with callables_alive", not ok and error == "callables_alive", [error, reply])
    checks.check("guard: the image was not closed, with one live operation of generation 3",
                 close.get("generation") == 3 and close.get("closed") is False and close.get("live", {}).get("operation") == 1,
                 close)
    checks.check("guard: no plugin is registered", state["loaded"] is None and s1.registries(agent) == baseline, "changed")
    checks.check("exit: app_quit accepted", agent.quit())


def control(agent: s1.Agent, checks: s1.Checks, kept: dict[str, Path], state: dict) -> None:
    ok, reply, _, error = agent.call("plugin_load", {"path": str(kept["A"])})
    checks.check("control: loaded", ok and generation_of(reply) == 2, [reply, error])
    ok, reply, _, error = agent.call("spike_counter_add", {"delta": 5, "form": "closure"})
    _, history, _, _ = agent.call("history")
    checks.check("control: the closure entry is recorded and listed",
                 ok and host(agent)["undo_value"] == 5 and str(history.get("undo_label", "")).endswith("Spike add 5 (closure)"),
                 [reply, error, history])
    ok, reply, _, error = agent.call("plugin_unload")
    close = host(agent)["closes"][-1]
    state["unmapped"] = ok and close.get("unmapped") is True
    state["range"] = close.get("range", [])
    checks.check("control: the plugin unloaded and its image left the process", state["unmapped"], [reply, error, close])
    try:
        state["undo"] = agent.call("undo")
    except (ConnectionError, OSError) as error:
        state["undo"] = f"{type(error).__name__}: {error}"


def run_control(args, checks: s1.Checks, kept: dict[str, Path]) -> int:
    import re
    state: dict = {}
    result = s1.launch(args, checks, lambda agent: control(agent, checks, kept, state), expect_crash=True)
    if result is None:
        return s1.finish(checks, args.out)
    code, log, lines = result
    fault = re.search(r"ERROR: AddressSanitizer: SEGV on unknown address (0x[0-9a-f]+)", log)
    report = log[fault.start():] if fault else ""
    in_undo = re.search(r"EditorCommandHistory(@[\w.]+)?::Undo\(", report.split("\n\n")[0]) is not None
    low, high = (int(x, 16) for x in state["range"]) if len(state.get("range", [])) == 2 else (0, 0)
    in_image = fault is not None and low <= int(fault[1], 16) < high
    if not state.get("unmapped"):
        checks.check("control: inconclusive, the image stayed mapped", False, state)
    else:
        checks.check("control: undo of today's closure entry touched the unloaded image (after closure_undo, a SEGV "
                     "inside the closed image's range, EditorCommandHistory::Undo on the stack, the process died)",
                     code != 0 and lines[-1:] == ["closure_undo"] and in_image and in_undo,
                     [code, lines[-3:], fault.group(0) if fault else None, state.get("range"), in_undo, state.get("undo")])
    return s1.finish(checks, args.out)


def main() -> int:
    closure_control = "--closure-control" in sys.argv
    if closure_control:
        sys.argv.remove("--closure-control")
    args = s1.arguments(__doc__)
    checks = s1.Checks()
    builds = Builds(args.build, args.out)
    kept: dict[str, Path] = {}
    # A normal generation last, so an interrupted session leaves the build directory loadable.
    for name, generation, options in (("F", 4, {"fail_register": True}), ("missing", 6, {"missing": True}),
                                      ("clamp", 7, {"clamp": True}), ("B", 3, {}), ("A", 2, {})):
        if not checks.check(f"prebuild: generation {generation} ({name}) built", builds.make(generation, **options)):
            return s1.finish(checks, args.out)
        kept[name] = builds.keep(name)
    if closure_control:
        return run_control(args, checks, kept)
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
