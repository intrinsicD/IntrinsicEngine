#!/usr/bin/env python3
"""ARCH-022 slice 2: drives the bilateral filter plugin through the running Sandbox over MCP and asserts its checks.

Starts <build>/bin/ExtrinsicSandbox like plugins/fixture/s1_driver.py (isolated socket, built-in config, the LSan
settings for a Vulkan exit) but with its built-in plugins, so the bilateral filter is the startup plugin. It imports
a point cloud with normals (a .pwn written here) and, over the agent socket:
  1. finds the plugin loaded at startup with its tools, its section sandbox.bilateral_filter and its window;
  2. runs zero passes (the output equals the positions) and one and three passes on CPU octree, compared with an
     independent float64 reference of the documented kernel (exhaustive kNN);
  3. runs CPU LBVH and Vulkan LBVH with the same settings: both agree with the octree run, and the Vulkan run pages
     its queries 64 at a time (gpu_query_batch_size 64);
  4. lists a long run in jobs_list and cancels it through MCP notifications/cancelled: the run ends, nothing is
     written (its row belongs to the editor's Jobs window, so jobs_cancel, the agent session's, is refused there);
  5. undoes and redoes a run while the plugin is unloaded and after it is loaded again, and runs it again;
  6. loads the fixture plugin beside it (a build of its own generation, made before the start): both are listed, an
     unnamed unload is refused, each reloads by name with the other loaded and keeps working, the fixture unloads;
  7. takes a screenshot of the window for the record, quits through app_quit and checks a clean, sanitizer-clean exit.
Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
Usage: python3 plugins/bilateral_filter/live_driver.py --build build/dev-shared --out <new dir> [--icd <json>]
"""

from __future__ import annotations

import base64
import json
import math
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "fixture"))
import s1_driver as s1  # noqa: E402  (shared session helpers)
import reload_driver as s2  # noqa: E402  (fixture builds of a chosen generation)

COEXIST_GENERATION = 9  # not the bilateral filter's 1

SECTION = "sandbox.bilateral_filter"
DOMAIN = "PointCloudPoint"
K, SPATIAL, NORMAL = 8, 0.15, 0.25
TOLERANCE = 1e-4  # float32 engine against the float64 reference, after three passes


def cloud(n: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    """A wavy height field with its analytic normals (normals vary, so the normal term matters)."""
    rng = np.random.default_rng(seed)
    xy = rng.random((n, 2))
    z = 0.05 * np.sin(6 * xy[:, 0]) * np.cos(5 * xy[:, 1]) + 0.01 * rng.standard_normal(n)
    dzdx = 0.3 * np.cos(6 * xy[:, 0]) * np.cos(5 * xy[:, 1])
    dzdy = -0.25 * np.sin(6 * xy[:, 0]) * np.sin(5 * xy[:, 1])
    normals = np.stack([-dzdx, -dzdy, np.ones(n)], axis=1)
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    return np.column_stack([xy, z]).astype(np.float32), normals.astype(np.float32)


def write_pwn(path: Path, points: np.ndarray, normals: np.ndarray) -> None:
    """Points with normals: the count, the positions, then the normals (float32 values written exactly)."""
    rows = [" ".join(f"{v:.9g}" for v in row) for row in (*points, *normals)]
    path.write_text("\n".join([str(len(points))] + rows) + "\n")


def reference(points: np.ndarray, normals: np.ndarray, passes: int) -> np.ndarray:
    """The documented kernel: k nearest others by (distance, id), weight exp(-d^2/2s^2) exp(-(1-|n.nj|)^2/2t^2),
    displacement along the fixed normal, simultaneous updates, neighbourhoods rebuilt every pass."""
    p = points.astype(np.float64)
    n = normals.astype(np.float64)
    unit = n / np.linalg.norm(n, axis=1, keepdims=True)
    for _ in range(passes):
        d2 = ((p[:, None, :] - p[None, :, :]) ** 2).sum(-1)
        order = np.lexsort((np.broadcast_to(np.arange(len(p)), d2.shape), d2), axis=1)
        moved = p.copy()
        for i in range(len(p)):
            row = [j for j in order[i, :K + 1] if j != i][:K]
            diff = p[row] - p[i]
            dist2 = (diff ** 2).sum(1)
            dot = np.abs(unit[row] @ unit[i])
            w = np.exp(-dist2 / (2 * SPATIAL ** 2)) * np.exp(-((1 - dot) ** 2) / (2 * NORMAL ** 2))
            moved[i] = p[i] + unit[i] * ((w * (diff @ unit[i])).sum() / w.sum())
        p = moved
    return p


class Agent(s1.Agent):
    """Keeps replies to other requests (a call left waiting while the driver polls) instead of dropping them."""

    def __init__(self, path: Path, deadline_s: float) -> None:
        self.stash: dict[int, dict] = {}
        super().__init__(path, deadline_s)

    def request(self, method: str, params: dict) -> dict:
        return self.reply(self.send(method, params))

    def reply(self, wanted: int, timeout_s: float = 120.0) -> dict:
        while wanted not in self.stash:
            message = self.read(timeout_s)
            if "id" in message:
                self.stash[message["id"]] = message
        return self.stash.pop(wanted)


class Session:
    def __init__(self, agent: s1.Agent, checks: s1.Checks) -> None:
        self.agent, self.checks = agent, checks
        self.last_jobs: object = None

    def call(self, name: str, arguments: dict | None = None) -> tuple[bool, object, str]:
        ok, value, _, code = self.agent.call(name, arguments)
        return ok, value, code

    def settings(self, entity: int, positions: str, normals: str, output: str, **values) -> bool:
        payload = {"entity": entity, "backend": "cpu_octree", "k_neighbors": K, "spatial_sigma": SPATIAL,
                   "normal_sigma": NORMAL, "iterations": 1, "gpu_query_batch_size": 4096,
                   "positions": {"domain": DOMAIN, "name": positions, "kind": "vec3"},
                   "normals": {"domain": DOMAIN, "name": normals, "kind": "vec3"},
                   "output": {"domain": DOMAIN, "name": output, "kind": "vec3"}} | values
        ok, reply, _ = self.call("config_apply", {"section": SECTION, "payload": payload})
        return self.checks.check(f"config_apply {values}", ok, reply)

    def run(self) -> dict:
        ok, reply, code = self.call("bilateral_filter")
        return reply if isinstance(reply, dict) else {"status": code or "unparsed", "message": reply}

    def values(self, entity: int, name: str, count: int) -> np.ndarray | None:
        rows: list = []
        while len(rows) < count:
            ok, reply, _ = self.call("property_values", {"entity": entity, "domain": DOMAIN, "name": name,
                                                         "offset": len(rows), "limit": 4096})
            if not ok or not isinstance(reply, dict) or not reply.get("rows"):
                return None
            rows += [row["values"] for row in reply["rows"]]
        return np.asarray(rows, dtype=np.float64)

    def properties(self, entity: int) -> set[str]:
        ok, reply, _ = self.call("property_list", {"entity": entity})
        return {entry.get("name") for entry in (s1.find(reply, "properties") or [])} if ok else set()

    def running(self, label: str, deadline_s: float = 30.0,
                states: tuple[str, ...] = ("queued", "running")) -> dict | None:
        end = time.monotonic() + deadline_s
        while time.monotonic() < end:
            ok, reply, _ = self.call("jobs_list")
            self.last_jobs = reply
            for job in (s1.find(reply, "jobs") or []) if ok else []:
                if job.get("name") == label and job.get("state") in states:
                    return job
            time.sleep(0.05)
        return None


def scenario(agent: Agent, checks: s1.Checks, out: Path, build: Path, meta: dict) -> None:
    try:
        steps(agent, checks, out, build, meta)
    finally:
        checks.check("exit: app_quit accepted", agent.quit())


def steps(agent: Agent, checks: s1.Checks, out: Path, build: Path, meta: dict) -> None:
    session = Session(agent, checks)
    ok, listed, _ = session.call("plugin_list")
    loaded = [p.get("name") for p in (listed.get("loaded") or [])] if isinstance(listed, dict) else []
    checks.check("startup: the bilateral filter plugin is loaded", "bilateral_filter" in loaded, listed)
    names = {tool["name"] for tool in agent.request("tools/list", {}).get("result", {}).get("tools", [])}
    checks.check("startup: its tools are registered", {"bilateral_filter", "bilateral_filter_preview"} <= names)
    ok, sections, _ = session.call("config_sections")
    checks.check("startup: its section is registered",
                 ok and any(s.get("section") == SECTION for s in (s1.find(sections, "sections") or [])))
    ok, identity, _ = session.call("plugin_host_identity")
    checks.check("startup: its window is registered",
                 ok and any(w.get("id") == "view.bilateral_filter" for w in (s1.find(identity, "windows") or [])),
                 identity)

    points, normals = cloud(300, 241)
    ply = out / "bilateral_cloud.pwn"
    write_pwn(ply, points, normals)
    ok, imported, _ = session.call("import_file", {"path": str(ply), "wait": True})
    checks.check("scene: the point cloud imported", ok, imported)
    ok, scene, _ = session.call("scene_entities")
    entities = s1.find(scene, "entities") or []
    entity = next((e["entity"] for e in entities if "bilateral_cloud" in str(e.get("name"))), None)
    checks.check("scene: the point cloud entity found", entity is not None, entities)
    if entity is None:
        return
    props = session.properties(entity)
    positions = next((p for p in props if "position" in p), "v:position")
    normal_name = next((p for p in props if "normal" in p), "v:normal")
    meta.update(entity=entity, positions=positions, normals=normal_name, properties=sorted(props))
    stored = session.values(entity, positions, len(points))
    checks.check("scene: positions imported as written", stored is not None and np.allclose(stored, points, atol=1e-6))

    # Zero passes copy; one and three passes match the reference.
    session.settings(entity, positions, normal_name, "filtered", iterations=0)
    result = session.run()
    copied = session.values(entity, "filtered", len(points))
    checks.check("zero passes: applied, output equals positions",
                 result.get("status") == "applied" and copied is not None and np.array_equal(copied, stored), result)
    outputs: dict[str, np.ndarray] = {}
    for passes in (1, 3):
        session.settings(entity, positions, normal_name, "filtered", iterations=passes)
        result = session.run()
        filtered = session.values(entity, "filtered", len(points))
        expected = reference(points, normals, passes)
        error = float(np.abs(filtered - expected).max()) if filtered is not None else math.inf
        moved = float(np.abs(expected - points).max())
        meta[f"octree_{passes}_max_error"], meta[f"reference_{passes}_max_move"] = error, moved
        checks.check(f"{passes} pass(es) cpu_octree: applied, matches the reference within {TOLERANCE}",
                     result.get("status") == "applied" and result.get("completed_iterations") == passes
                     and error <= TOLERANCE, {"result": result, "max_error": error})
        checks.check(f"{passes} pass(es): the reference moves points by more than the tolerance (not a no-op)",
                     moved > 10 * TOLERANCE, moved)
        outputs["cpu_octree"] = filtered

    # Backends agree; Vulkan pages 64 queries per batch.
    for backend, extra in (("cpu_lbvh", {}), ("vulkan_lbvh", {"gpu_query_batch_size": 64})):
        session.settings(entity, positions, normal_name, "filtered", iterations=3, backend=backend, **extra)
        result = session.run()
        filtered = session.values(entity, "filtered", len(points))
        error = float(np.abs(filtered - outputs["cpu_octree"]).max()) if filtered is not None else math.inf
        meta[f"{backend}_vs_octree"] = error
        checks.check(f"{backend}: applied with that backend, agrees with cpu_octree within {TOLERANCE}",
                     result.get("status") == "applied" and result.get("actual_backend") == backend
                     and error <= TOLERANCE, {"result": result, "max_error": error})
        if backend == "vulkan_lbvh":
            pages = 3 * math.ceil(len(points) / 64)
            checks.check(f"vulkan_lbvh: {pages} query batches of at most 64 over three passes",
                         result.get("gpu_query_batches") == pages, result.get("gpu_query_batches"))

    # The Jobs window lists runs and cancels them; MCP cancellation cancels too. A long run: a big cloud, many passes.
    big_points, big_normals = cloud(50_000, 7)
    big = out / "bilateral_big.pwn"
    write_pwn(big, big_points, big_normals)
    ok, _, _ = session.call("import_file", {"path": str(big), "wait": True})
    ok, scene, _ = session.call("scene_entities")
    big_entity = next((e["entity"] for e in s1.find(scene, "entities") or [] if "bilateral_big" in str(e.get("name"))), None)
    checks.check("cancel: the big cloud imported", big_entity is not None)
    if big_entity is not None:
        # One pass's time bounds the cancel latency: the CPU run checks for a cancel between passes.
        session.settings(big_entity, positions, normal_name, "one_pass", iterations=1, k_neighbors=30)
        started = time.monotonic()
        one = session.run()
        pass_seconds = time.monotonic() - started
        meta.update(big_one_pass_seconds=pass_seconds, big_one_pass_cpu_ms=one.get("cpu_compute_ms"))
        checks.check("cancel: one pass on the big cloud applied", one.get("status") == "applied", one)
        session.settings(big_entity, positions, normal_name, "cancelled_output", iterations=30, k_neighbors=30)
        pending = agent.send("tools/call", {"name": "bilateral_filter", "arguments": {}})
        job = session.running("Bilateral point filter")
        checks.check("cancel: the run is a running jobs_list row", job is not None, session.last_jobs)
        if job is not None:
            ok, refused, code = session.call("jobs_cancel", {"token": job["token"]})
            checks.check("cancel: jobs_cancel refuses the Jobs window's row", not ok, [code, refused])
        agent.sock.sendall(json.dumps({"jsonrpc": "2.0", "method": "notifications/cancelled",
                                       "params": {"requestId": pending, "reason": "driver"}}).encode() + b"\n")
        started = time.monotonic()
        while time.monotonic() - started < 60 and session.running("Bilateral point filter", deadline_s=0.2):
            time.sleep(0.2)
        meta["mcp_cancel_seconds"] = time.monotonic() - started
        checks.check("MCP cancel: the run ended", session.running("Bilateral point filter", 0.2) is None,
                     session.last_jobs)
        checks.check("MCP cancel: it ended within two passes' time (a cancel stops between passes)",
                     meta["mcp_cancel_seconds"] <= 2 * pass_seconds + 1.0,
                     {"cancel_s": meta["mcp_cancel_seconds"], "pass_s": pass_seconds})
        checks.check("MCP cancel: nothing written", "cancelled_output" not in session.properties(big_entity))

    # Undo and redo outlive the plugin.
    session.settings(entity, positions, normal_name, "history_output", iterations=1)
    result = session.run()
    written = session.values(entity, "history_output", len(points))
    checks.check("history: a run applied", result.get("status") == "applied" and written is not None, result)
    ok, unloaded, code = session.call("plugin_unload", {"plugin": "bilateral_filter"})
    checks.check("history: plugin unloaded", ok, [code, unloaded])
    ok, _, _ = session.call("undo")
    checks.check("history: undo while unloaded removes the output",
                 ok and "history_output" not in session.properties(entity))
    ok, _, _ = session.call("redo")
    again = session.values(entity, "history_output", len(points))
    checks.check("history: redo while unloaded restores it", ok and again is not None and np.array_equal(again, written))
    ok, loaded, code = session.call("plugin_load", {"path": str(build / "plugins/libIntrinsicBilateralFilterPlugin.so")})
    checks.check("history: plugin loaded again", ok, [code, loaded])
    ok, _, _ = session.call("undo")
    checks.check("history: undo after the reload", ok and "history_output" not in session.properties(entity))
    ok, _, _ = session.call("redo")
    checks.check("history: redo after the reload",
                 ok and np.array_equal(session.values(entity, "history_output", len(points)), written))
    ok, current, _ = session.call("config_get", {"section": SECTION})
    checks.check("reload: the section kept its applied payload",
                 ok and s1.find(current, "output") is not None and "history_output" in json.dumps(current), current)
    result = session.run()
    checks.check("reload: the tool runs again", result.get("status") == "applied", result)

    # A second plugin beside it (ARCH-022 step 2d-0): each is addressed by name and leaves the other working.
    fixture = meta.get("coexist_fixture", "no coexistence build")
    ok, loaded, code = session.call("plugin_load", {"path": fixture})
    checks.check("coexist: the fixture plugin loads beside it", ok, [code, loaded])
    ok, listed, _ = session.call("plugin_list")
    names = [p.get("name") for p in (listed.get("loaded") or [])] if isinstance(listed, dict) else []
    checks.check("coexist: both are listed", {"bilateral_filter", "fixture"} <= set(names), listed)
    ok, echo, _ = session.call("fixture_echo", {"text": "beside"})
    checks.check("coexist: the fixture tool answers", ok and echo == {"echo": {"text": "beside"}}, echo)
    # A second load of a loaded plugin is refused after its Register ran; the exit's leak check covers what it made.
    ok, twice, code = session.call("plugin_load", {"path": str(build / "plugins/libIntrinsicBilateralFilterPlugin.so")})
    checks.check("coexist: a second load of a loaded plugin is refused", not ok and code == "already_loaded",
                 [code, twice])
    ok, unnamed, code = session.call("plugin_unload")
    checks.check("coexist: an unload without a name is refused", not ok and code == "invalid_params", [code, unnamed])
    ok, reloaded, code = session.call("plugin_reload", {"plugin": "fixture", "path": fixture})
    checks.check("coexist: the fixture plugin reloads by name", ok, [code, reloaded])
    ok, reloaded, code = session.call("plugin_reload", {"plugin": "bilateral_filter",
                                                         "path": str(build / "plugins/libIntrinsicBilateralFilterPlugin.so")})
    checks.check("coexist: the bilateral filter reloads by name beside the fixture", ok, [code, reloaded])
    result = session.run()
    checks.check("coexist: the bilateral tool runs with both loaded", result.get("status") == "applied", result)
    ok, echo, _ = session.call("fixture_echo", {"text": "after"})
    checks.check("coexist: the fixture tool still answers", ok and echo == {"echo": {"text": "after"}}, echo)
    ok, unloaded, code = session.call("plugin_unload", {"plugin": "fixture"})
    checks.check("coexist: the fixture plugin unloads by name", ok, [code, unloaded])
    ok, listed, _ = session.call("plugin_list")
    names = [p.get("name") for p in (listed.get("loaded") or [])] if isinstance(listed, dict) else []
    checks.check("coexist: the bilateral filter stays loaded", "bilateral_filter" in names and "fixture" not in names,
                 listed)
    result = session.run()
    checks.check("coexist: the bilateral tool runs after the fixture unload", result.get("status") == "applied", result)

    ok, _, content, _ = agent.call("view_capture", {"region": "window", "path": "bilateral.png", "overwrite": True})
    image = next((item for item in content if item.get("type") == "image"), None)
    if image is not None:
        (out / "screenshot.png").write_bytes(base64.b64decode(image["data"]))
    checks.check("record: a screenshot was taken", image is not None)


def main() -> int:
    args = s1.arguments(__doc__)
    checks = s1.Checks()
    meta: dict = {}
    # The fixture build for the coexistence steps gets a generation of its own: the host counts tracked callables
    # by generation, and a fresh fixture build's (1) is the bilateral filter's, so the fixture's would refuse its close.
    builds = s2.Builds(args.build, args.out)
    if checks.check(f"coexist: a fixture build of generation {COEXIST_GENERATION} is made", builds.make(COEXIST_GENERATION)):
        meta["coexist_fixture"] = str(builds.keep("coexist"))
    result = s1.launch(args, checks, lambda agent: scenario(agent, checks, args.out, args.build, meta),
                       flags=("--plugin-test-hooks",), client=Agent)
    (args.out / "meta.json").write_text(json.dumps(meta, indent=2, default=str))
    if result is not None:
        log = result[1]
        own = [r for r in s1.leak_records(log) if "BilateralFilter" in r or "PointCloudEditing" in r]
        checks.check("exit: no leak record has a frame in the plugin or the editing service", not own, own[:2])
    return s1.finish(checks, args.out)


if __name__ == "__main__":
    sys.exit(main())
