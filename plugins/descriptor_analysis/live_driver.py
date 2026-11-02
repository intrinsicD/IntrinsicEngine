#!/usr/bin/env python3
"""ARCH-022 step 2d: drives the FPFH descriptor analysis plugin through the running Sandbox over MCP.

Starts <build>/bin/ExtrinsicSandbox like plugins/spike/spike_driver.py (isolated socket, built-in config, the LSan
settings for a Vulkan exit) with its built-in plugins, so the descriptor analysis and the bilateral filter are the
startup plugins. It imports the golden cloud (tests/data/descriptor_golden_cloud.json: the 300 points with normals
the built-in feature was recorded on before it moved into the plugin) as a .pwn and, over the agent socket:
  1. finds the plugin loaded at startup with its tools, its section sandbox.descriptor_analysis and its window;
  2. runs cpu_kdtree, cpu_lbvh and vulkan_lbvh with the automatic radius and reads the 33 columns of each: every
     backend reproduces the golden columns exactly (sha256 of the float32 rows);
  3. cancels a long Vulkan run over MCP (notifications/cancelled) while its GPU stage pages: the run ends cancelled
     and writes nothing;
  4. undoes and redoes a run while the plugin is unloaded and after it is loaded again, and runs it again;
  5. quits through app_quit and checks a clean, sanitizer-clean exit.
Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
Usage: python3 plugins/descriptor_analysis/live_driver.py --build build/dev-shared --out <new dir> [--icd <json>]
"""

from __future__ import annotations

import hashlib
import json
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "plugins/spike"))
sys.path.insert(0, str(REPO / "plugins/bilateral_filter"))
import spike_driver as s1  # noqa: E402  (shared session helpers)
# shortcut: the bilateral driver's Agent, Session, cloud and write_pwn are reused as they are; move them into the
# shared driver module when a third plugin driver needs them.
import live_driver as bilateral  # noqa: E402

SECTION = "sandbox.descriptor_analysis"
DOMAIN = "PointCloudPoint"
BINS = 33
GPU_STAGE = "Descriptor radius support (Vulkan)"
ACTIVE = ("queued", "running", "awaiting-apply")  # a gated GPU stage waits for its frames as awaiting-apply
GOLDEN = json.loads((REPO / "tests/data/descriptor_golden_cloud.json").read_text())


class Session(bilateral.Session):
    def configure(self, entity: int, positions: str, normals: str, prefix: str, backend: str, **values) -> bool:
        payload = {"entity": entity, "backend": backend, "feature_radius": 0, "max_neighbors": 0,
                   "gpu_query_batch_size": 4096, "gpu_radius_capacity": 256,
                   "positions": {"domain": DOMAIN, "name": positions, "kind": "vec3"},
                   "normals": {"domain": DOMAIN, "name": normals, "kind": "vec3"},
                   "outputs": outputs(prefix)} | values
        ok, reply, _ = self.call("config_apply", {"section": SECTION, "payload": payload})
        return self.checks.check(f"config_apply {prefix} {backend}", ok, reply)

    def run(self) -> dict:
        ok, reply, code = self.call("descriptor_analysis")
        return reply if isinstance(reply, dict) else {"status": code or "unparsed", "message": reply}

    def columns(self, entity: int, prefix: str, count: int) -> np.ndarray | None:
        columns = [self.values(entity, f"{prefix}.bin{b}", count) for b in range(BINS)]
        if any(c is None for c in columns):
            return None
        return np.stack([c[:, 0] for c in columns], axis=1).astype(np.float32)


def outputs(prefix: str) -> list[dict]:
    return [{"domain": DOMAIN, "name": f"{prefix}.bin{b}", "kind": "float"} for b in range(BINS)]


def scenario(agent: bilateral.Agent, checks: s1.Checks, out: Path, build: Path, meta: dict) -> None:
    try:
        steps(agent, checks, out, build, meta)
    finally:
        checks.check("exit: app_quit accepted", agent.quit())


def steps(agent: bilateral.Agent, checks: s1.Checks, out: Path, build: Path, meta: dict) -> None:
    session = Session(agent, checks)
    ok, listed, _ = session.call("plugin_list")
    loaded = [p.get("name") for p in (listed.get("loaded") or [])] if isinstance(listed, dict) else []
    checks.check("startup: the descriptor analysis plugin is loaded beside the bilateral filter",
                 {"descriptor_analysis", "bilateral_filter"} <= set(loaded), listed)
    names = {tool["name"] for tool in agent.request("tools/list", {}).get("result", {}).get("tools", [])}
    checks.check("startup: its tools are registered", {"descriptor_analysis", "descriptor_analysis_preview"} <= names)
    ok, sections, _ = session.call("config_sections")
    checks.check("startup: its section is registered",
                 ok and any(s.get("section") == SECTION for s in (s1.find(sections, "sections") or [])))
    ok, identity, _ = session.call("plugin_host_identity")
    checks.check("startup: its window is registered",
                 ok and any(w.get("id") == "view.descriptor_analysis" for w in (s1.find(identity, "windows") or [])),
                 identity)

    points = np.array(GOLDEN["points"], dtype=np.float32)
    normals = np.array(GOLDEN["normals"], dtype=np.float32)
    ply = out / "descriptor_golden.pwn"
    bilateral.write_pwn(ply, points, normals)
    ok, imported, _ = session.call("import_file", {"path": str(ply), "wait": True})
    checks.check("scene: the golden cloud imported", ok, imported)
    ok, scene, _ = session.call("scene_entities")
    entity = next((e["entity"] for e in s1.find(scene, "entities") or [] if "descriptor_golden" in str(e.get("name"))),
                  None)
    checks.check("scene: the golden cloud entity found", entity is not None)
    if entity is None:
        return
    props = session.properties(entity)
    positions = next((p for p in props if "position" in p), "v:position")
    normal_name = next((p for p in props if "normal" in p), "v:normal")

    # The golden: every backend reproduces the built-in feature's columns exactly.
    for backend in ("cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"):
        prefix = f"golden_{backend}"
        session.configure(entity, positions, normal_name, prefix, backend)
        result = session.run()
        columns = session.columns(entity, prefix, len(points))
        digest = hashlib.sha256(columns.tobytes()).hexdigest() if columns is not None else None
        meta[f"{backend}_sha256"] = digest
        checks.check(f"golden {backend}: applied with that backend",
                     result.get("status") == "applied" and result.get("actual_backend") == backend, result)
        checks.check(f"golden {backend}: the 33 columns equal the built-in feature's exactly",
                     digest == GOLDEN["columns_sha256"], {"sha256": digest, "expected": GOLDEN["columns_sha256"]})

    # MCP cancel during the GPU stage of a long Vulkan run (pages of 16 over a big cloud).
    big_points, big_normals = bilateral.cloud(60_000, 7)
    big = out / "descriptor_big.pwn"
    bilateral.write_pwn(big, big_points, big_normals)
    ok, _, _ = session.call("import_file", {"path": str(big), "wait": True})
    ok, scene, _ = session.call("scene_entities")
    big_entity = next((e["entity"] for e in s1.find(scene, "entities") or [] if "descriptor_big" in str(e.get("name"))),
                      None)
    checks.check("cancel: the big cloud imported", big_entity is not None)
    if big_entity is not None:
        session.configure(big_entity, positions, normal_name, "cancelled", "vulkan_lbvh", gpu_query_batch_size=16)
        pending = agent.send("tools/call", {"name": "descriptor_analysis", "arguments": {}})
        job = session.running(GPU_STAGE, states=ACTIVE)
        checks.check("cancel: the GPU stage is a running jobs_list row", job is not None, session.last_jobs)
        agent.sock.sendall(json.dumps({"jsonrpc": "2.0", "method": "notifications/cancelled",
                                       "params": {"requestId": pending, "reason": "driver"}}).encode() + b"\n")
        started = time.monotonic()
        while time.monotonic() - started < 60 and session.running(GPU_STAGE, deadline_s=0.2, states=ACTIVE):
            time.sleep(0.2)
        meta["mcp_cancel_seconds"] = time.monotonic() - started
        checks.check("MCP cancel: the run ended", session.running(GPU_STAGE, 0.2, states=ACTIVE) is None,
                     session.last_jobs)
        checks.check("MCP cancel: nothing written", "cancelled.bin0" not in session.properties(big_entity))

    # Undo and redo outlive the plugin.
    session.configure(entity, positions, normal_name, "history", "cpu_kdtree")
    result = session.run()
    written = session.columns(entity, "history", len(points))
    checks.check("history: a run applied", result.get("status") == "applied" and written is not None, result)
    ok, unloaded, code = session.call("plugin_unload", {"plugin": "descriptor_analysis"})
    checks.check("history: plugin unloaded by name, the bilateral filter stays", ok, [code, unloaded])
    ok, _, _ = session.call("undo")
    checks.check("history: undo while unloaded removes the 33 outputs",
                 ok and not any(f"history.bin{b}" in session.properties(entity) for b in range(BINS)))
    ok, _, _ = session.call("redo")
    again = session.columns(entity, "history", len(points))
    checks.check("history: redo while unloaded restores them", ok and again is not None and np.array_equal(again, written))
    ok, loaded, code = session.call("plugin_load",
                                    {"path": str(build / "plugins/libIntrinsicDescriptorAnalysisPlugin.so")})
    checks.check("history: plugin loaded again", ok, [code, loaded])
    ok, _, _ = session.call("undo")
    checks.check("history: undo after the reload", ok and "history.bin0" not in session.properties(entity))
    ok, _, _ = session.call("redo")
    checks.check("history: redo after the reload",
                 ok and np.array_equal(session.columns(entity, "history", len(points)), written))
    ok, current, _ = session.call("config_get", {"section": SECTION})
    checks.check("reload: the section kept its applied payload", ok and "history.bin0" in json.dumps(current), current)
    result = session.run()
    checks.check("reload: the tool runs again", result.get("status") == "applied", result)


def main() -> int:
    args = s1.arguments(__doc__)
    checks = s1.Checks()
    meta: dict = {}
    result = s1.launch(args, checks, lambda agent: scenario(agent, checks, args.out, args.build, meta),
                       flags=("--plugin-test-hooks",), client=bilateral.Agent)
    (args.out / "meta.json").write_text(json.dumps(meta, indent=2, default=str))
    if result is not None:
        log = result[1]
        own = [r for r in s1.leak_records(log) if "DescriptorAnalysis" in r or "PointCloudEditing" in r]
        checks.check("exit: no leak record has a frame in the plugin or the editing service", not own, own[:2])
    return s1.finish(checks, args.out)


if __name__ == "__main__":
    sys.exit(main())
