#!/usr/bin/env python3
"""ARCH-022 slice 2, S1: drives the spike plugin through the running Sandbox and asserts the S1 pass checks.

Starts <build>/bin/ExtrinsicSandbox with an isolated agent socket and event log, then over the agent socket
(newline-delimited JSON-RPC, as tools/agents/mcp_bridge.py speaks it):
  1. injects a load failure at every stage and asserts the registries (tools, config sections, windows) unchanged;
  2. loads the plugin and asserts its tool, config section and window are registered;
  3. asserts one identity of shared state: the plugin mutates the host's counter, inline static and thread-local
     through imported code, and both sides see the same addresses, ImGui context and allocator, ImPlot context;
  4. calls spike_echo; checks the config section's default (8) and a change to 12 (one callback, marker redrawn);
  5. finds the magenta marker in window captures before and after the change;
  6. quits through app_quit and asserts a clean exit, clean sanitizers and the lifecycle event order.
A first session without the plugin records what the Sandbox's own exit reports; the plugin session is also
compared with it. Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
Usage: python3 plugins/spike/spike_driver.py --build build/spike --out <new dir>
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FAIL_STAGES = ["dlopen", "dlsym", "register", "config", "operations", "windows", "replace"]
MAGENTA = (255, 0, 255)


class Checks:
    def __init__(self) -> None:
        self.results: list[dict] = []

    def check(self, name: str, ok: bool, detail: object = "") -> bool:
        self.results.append({"check": name, "pass": bool(ok), "detail": detail})
        print(f"{'PASS' if ok else 'FAIL'} {name}{'' if ok else f': {detail}'}", flush=True)
        return ok


class Agent:
    """Minimal JSON-RPC client for the Sandbox's agent socket."""

    def __init__(self, path: Path, deadline_s: float) -> None:
        end = time.monotonic() + deadline_s
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(str(path))
                break
            except OSError:
                self.sock.close()
                if time.monotonic() > end:
                    raise TimeoutError(f"agent socket not ready within {deadline_s} s")
                time.sleep(0.5)
        self.buffer = b""
        self.next_id = 0
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "arch022-spike-driver", "version": "1"}})
        self.sock.sendall(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}).encode() + b"\n")

    def request(self, method: str, params: dict, timeout_s: float = 60.0) -> dict:
        self.next_id += 1
        wanted = self.next_id
        self.sock.sendall(json.dumps({"jsonrpc": "2.0", "id": wanted, "method": method, "params": params}).encode() + b"\n")
        self.sock.settimeout(timeout_s)
        while True:
            while b"\n" not in self.buffer:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    raise ConnectionError("the Sandbox closed the agent socket")
                self.buffer += chunk
            line, self.buffer = self.buffer.split(b"\n", 1)
            message = json.loads(line)
            if message.get("id") == wanted:
                return message

    def call(self, name: str, arguments: dict | None = None) -> tuple[bool, dict | str, list[dict]]:
        """(ok, parsed text or raw text, content items) of one tool call."""
        reply = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        result = reply.get("result", {})
        content = result.get("content", [])
        text = next((item["text"] for item in content if item.get("type") == "text"), "")
        try:
            parsed: dict | str = json.loads(text)
        except (json.JSONDecodeError, TypeError):
            parsed = text
        return not result.get("isError", "error" in reply), parsed, content

    def tools(self) -> list[str]:
        return [tool["name"] for tool in self.request("tools/list", {}).get("result", {}).get("tools", [])]


def magenta_pixels(png: bytes) -> int:
    """Counts pure magenta pixels of an 8-bit RGB or RGBA PNG (standard library only)."""
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    position, data, width = 8, b"", 0
    while position < len(png):
        length, kind = struct.unpack(">I4s", png[position:position + 8])
        body = png[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and colour in (2, 6) and interlace == 0, (depth, colour, interlace)
            channels = 4 if colour == 6 else 3
        elif kind == b"IDAT":
            data += body
        position += 12 + length
    raw, stride, count = zlib.decompress(data), width * channels, 0
    previous = bytearray(stride)
    for row in range(height):
        kind, line = raw[row * (stride + 1)], bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up, corner = previous[i], previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        count += sum(1 for x in range(0, stride, channels) if tuple(line[x:x + 3]) == MAGENTA)
        previous = line
    return count


def capture_marker(agent: Agent, checks: Checks, out: Path, name: str, side: int) -> None:
    ok, _, content = agent.call("view_capture", {"region": "window", "path": f"{name}.png", "overwrite": True})
    image = next((item for item in content if item.get("type") == "image"), None)
    if not checks.check(f"{name}: window capture returned an image", ok and image is not None, content[:1]):
        return
    png = base64.b64decode(image["data"])
    (out / f"{name}.png").write_bytes(png)
    count = magenta_pixels(png)
    checks.check(f"{name}: marker of {side}x{side} pixels drawn", (side - 1) ** 2 <= count <= (side + 1) ** 2, count)


def registries(agent: Agent) -> tuple[list[str], list[str], list[str]]:
    _, sections, _ = agent.call("config_sections")
    _, identity, _ = agent.call("plugin_host_identity")
    names = [entry["section"] for entry in sections.get("sections", [])] if isinstance(sections, dict) else []
    return sorted(agent.tools()), sorted(names), sorted(identity.get("windows", []))


def sanitizer_report(log: str) -> list[str]:
    """The leak and error headlines of a sanitizer report, without addresses or process ids."""
    keep = ("Direct leak", "Indirect leak", "SUMMARY:", "ERROR:", "runtime error:")
    return sorted(line.split("==")[-1].strip() for line in log.splitlines() if any(k in line for k in keep))


def session(build: Path, out: Path, env: dict, deadline: float, body) -> tuple[object, str]:
    """Runs the Sandbox with an agent socket in <out>, calls body(agent), returns (exit code, stderr text)."""
    out.mkdir(parents=True)
    with (out / "sandbox.stdout").open("wb") as stdout, (out / "sandbox.stderr").open("wb") as stderr:
        sandbox = subprocess.Popen([str(build / "bin/ExtrinsicSandbox"), "--agent-socket", str(out / "agent.sock"),
                                    "--agent-root", str(out)], cwd=REPO, env=env, stdout=stdout, stderr=stderr)
        try:
            body(Agent(out / "agent.sock", deadline))
        finally:
            try:
                code: object = sandbox.wait(timeout=deadline)
            except subprocess.TimeoutExpired:
                sandbox.kill()
                code = "killed after the deadline"
    return code, (out / "sandbox.stderr").read_bytes().decode(errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--deadline", type=float, default=120.0, help="seconds for startup and for exit")
    args = parser.parse_args()
    out, build = args.out.resolve(), args.build.resolve()
    out.mkdir(parents=True)  # never reuse an earlier attempt's directory
    checks = Checks()
    if not checks.check("environment: DISPLAY is set", bool(os.environ.get("DISPLAY")), os.environ.get("DISPLAY")):
        return finish(checks, out)
    events = out / "plugin" / "events.log"
    env = dict(os.environ, INTRINSIC_SPIKE_EVENT_LOG=str(events), ASAN_OPTIONS="detect_leaks=1",
               LSAN_OPTIONS=f"suppressions={REPO / 'lsan.supp'}", UBSAN_OPTIONS="print_stacktrace=1")
    # The same Sandbox without the plugin: what its exit reports on its own.
    base_code, base_log = session(build, out / "no-plugin", env, args.deadline, lambda agent: agent.call("app_quit"))

    def body(agent: Agent) -> None:
        try:
            run(agent, checks, out / "plugin", build / "plugins/libIntrinsicSpikePlugin.so")
        except Exception as error:  # a crash or a protocol failure is a failed check, not a driver traceback
            checks.check("driver: the session completed", False, f"{type(error).__name__}: {error}")

    code, log = session(build, out / "plugin", env, args.deadline, body)
    checks.check("exit: the Sandbox exited with status 0", code == 0, code)
    checks.check("exit: ASan and LSan clean", "Sanitizer" not in log, [l for l in log.splitlines() if "Sanitizer" in l][:3])
    checks.check("exit: UBSan clean", "runtime error:" not in log, [l for l in log.splitlines() if "runtime error:" in l][:3])
    checks.check("exit: same status as without the plugin", code == base_code, [code, base_code])
    checks.check("exit: no sanitizer report beyond the no-plugin run", sanitizer_report(log) == sanitizer_report(base_log),
                 sorted(set(sanitizer_report(log)) ^ set(sanitizer_report(base_log))))
    lines = events.read_text().splitlines() if events.exists() else []
    expected = ["static_construct"] + ["register"] * 5 + ["commit", "first_draw", "config_changed", "shutdown",
                                                            "static_destruct"]
    checks.check("lifecycle: events once each in order (Register once per staged attempt)", lines == expected, lines)
    return finish(checks, out)


def run(agent: Agent, checks: Checks, out: Path, plugin: Path) -> None:
    baseline = registries(agent)
    checks.check("host: plugin tools present", {"plugin_load", "plugin_host_identity", "app_quit"} <= set(baseline[0]))
    for stage in FAIL_STAGES:
        ok, reply, _ = agent.call("plugin_load", {"path": str(plugin), "fail_at": stage})
        checks.check(f"failure at {stage}: the load fails with a reason", not ok and bool(reply), reply)
        checks.check(f"failure at {stage}: registries unchanged", registries(agent) == baseline,
                     {"tools": sorted(set(registries(agent)[0]) ^ set(baseline[0]))})
    ok, reply, _ = agent.call("plugin_load", {"path": str(plugin)})
    checks.check("load: committed", ok and isinstance(reply, dict) and reply.get("loaded") is True, reply)
    tools, sections, windows = registries(agent)
    checks.check("load: tools spike_echo and spike_identity registered", {"spike_echo", "spike_identity"} <= set(tools))
    checks.check("load: config section spike registered", "spike" in sections, sections)
    checks.check("load: window view.spike registered", "view.spike" in windows, windows)

    _, before, _ = agent.call("plugin_host_identity")
    _, plugin_view, _ = agent.call("spike_identity")
    _, after, _ = agent.call("plugin_host_identity")
    for probe in ("counter", "inline_static", "thread_local"):
        checks.check(f"identity: {probe} has one address", plugin_view[probe]["address"] == after[probe]["address"],
                     [plugin_view[probe]["address"], after[probe]["address"]])
        checks.check(f"identity: {probe} mutated by the plugin is seen by the host",
                     after[probe]["value"] == before[probe]["value"] + 1 == plugin_view[probe]["value"],
                     [before[probe]["value"], plugin_view[probe]["value"], after[probe]["value"]])
    for key in ("imgui_context", "imgui_alloc", "imgui_free", "imgui_alloc_user_data", "implot_context"):
        checks.check(f"identity: {key} is the host's", plugin_view[key] == after[key], [plugin_view[key], after[key]])

    ok, echo, _ = agent.call("spike_echo", {"text": "hello"})
    checks.check("mcp: spike_echo answers", ok and echo == {"echo": {"text": "hello"}}, echo)

    ok, section, _ = agent.call("config_get", {"section": "spike"})
    checks.check("config: section, schema and default 8 visible after the load",
                 ok and '"marker_size":8' in json.dumps(section, separators=(",", ":")), section)
    capture_marker(agent, checks, out, "marker_default", 8 * 8)
    ok, applied, _ = agent.call("config_apply", {"section": "spike", "payload": {"marker_size": 12}})
    checks.check("config: change to 12 applied", ok, applied)
    _, plugin_view, _ = agent.call("spike_identity")
    checks.check("config: active value 12 and exactly one change callback",
                 plugin_view.get("marker_size") == 12 and plugin_view.get("config_changes") == 1, plugin_view)
    capture_marker(agent, checks, out, "marker_changed", 12 * 8)
    ok, _, _ = agent.call("app_quit")
    checks.check("exit: app_quit accepted", ok)


def finish(checks: Checks, out: Path) -> int:
    failed = [r["check"] for r in checks.results if not r["pass"]]
    (out / "summary.json").write_text(json.dumps({"checks": checks.results, "failed": failed}, indent=2) + "\n")
    print(f"{len(checks.results) - len(failed)} of {len(checks.results)} checks passed", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
