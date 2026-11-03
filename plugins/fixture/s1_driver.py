#!/usr/bin/env python3
"""ARCH-022 slice 2, S1: drives the fixture plugin through the running Sandbox and asserts the S1 pass checks.

Runs <build>/bin/ExtrinsicSandbox with an isolated agent socket, the built-in config (no --engine-config, no
INTRINSIC_ENGINE_CONFIG, no config/engine.json) and the repository's LSan settings for a Vulkan exit (BUG-083: one
ICD through VK_DRIVER_FILES, fast_unwind_on_malloc=0, lsan.supp). The ICD's library is preloaded so it stays mapped
until LSan runs: its leaks are then named and suppressed by plugins/fixture/lsan-driver.supp, appended to lsan.supp.
Over the agent socket (newline-delimited JSON-RPC,
as tools/agents/mcp_bridge.py speaks it) it:
  1. checks the Vulkan device through diagnostics_read and captures the window without a marker;
  2. injects a load failure at every host stage: the error names that stage, and tools, config sections (with
     their schema, defaults and payload) and windows are unchanged; each opened image is closed again;
  3. loads the plugin (a second load in the same dispatch must be refused as load_pending) and asserts its tool,
     config section and window are registered;
  4. asserts one identity of shared state: the plugin mutates the host's counter, inline static and thread-local
     through imported code, and both sides see the same addresses, ImGui context and allocator, ImPlot context;
  5. calls fixture_echo; checks the config section's schema, default 8, active state, invalid payloads and a change to 12;
  6. finds a solid magenta square of the expected side in window captures before and after the change;
  7. quits through app_quit; checks a clean exit, sanitizer errors, the attribution of any leak record by its frames
     and the lifecycle event order.
Every check is recorded in <out>/summary.json; the exit status is 1 if any failed.
Usage: python3 plugins/fixture/s1_driver.py --build build/dev --out <new dir> [--icd <VK_DRIVER_FILES json>]
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
# Expected error code per injected stage; commit stages must also name the stage in their message.
# A failing Register is a plugin build of its own (S2, reload_driver.py), not a host-side injection.
FAIL_STAGES = {"dlopen": "dlopen_failed", "dlsym": "dlsym_failed",
               "config": "commit_failed", "operations": "commit_failed", "windows": "commit_failed"}
MAGENTA = (255, 0, 255)
# A sanitizer frame in any of these is the fixture's own code.
FIXTURE_FRAMES = ("libIntrinsicFixturePlugin", "FixturePlugin.cpp", "Runtime.PluginHost", "Runtime.PluginContract",
               "PluginHostModule", "PluginProbes")
# The only sanitizer lines a driver leak report may contain; anything else (a crash, a fatal LSan error, UBSan) fails.
LEAK_REPORT = re.compile(r"ERROR: LeakSanitizer: detected memory leaks|SUMMARY: AddressSanitizer: \d+ byte\(s\) leaked")


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
                                    "clientInfo": {"name": "arch022-fixture-driver", "version": "1"}})
        self.sock.sendall(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}).encode() + b"\n")

    def send(self, method: str, params: dict) -> int:
        self.next_id += 1
        self.sock.sendall(json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params})
                          .encode() + b"\n")
        return self.next_id

    def read(self, timeout_s: float = 60.0) -> dict:
        self.sock.settimeout(timeout_s)
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise ConnectionError("the Sandbox closed the agent socket")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def request(self, method: str, params: dict) -> dict:
        wanted = self.send(method, params)
        while (message := self.read()).get("id") != wanted:
            pass
        return message

    @staticmethod
    def outcome(reply: dict) -> tuple[bool, dict | str, list[dict], str]:
        """(ok, parsed text or raw text, content items, error code) of one tool reply."""
        result = reply.get("result", {})
        content = result.get("content", [])
        text = next((item["text"] for item in content if item.get("type") == "text"), "")
        try:
            parsed: dict | str = json.loads(text)
        except (json.JSONDecodeError, TypeError):
            parsed = text
        code = ((result.get("structuredContent") or {}).get("error") or {}).get("code", "")
        return not result.get("isError", "error" in reply), parsed, content, code

    def call(self, name: str, arguments: dict | None = None) -> tuple[bool, dict | str, list[dict], str]:
        return self.outcome(self.request("tools/call", {"name": name, "arguments": arguments or {}}))

    def call_both(self, first: tuple[str, dict], second: tuple[str, dict]) -> list[tuple]:
        """Sends both calls before reading a reply; the replies come back in request order."""
        ids = [self.send("tools/call", {"name": name, "arguments": args}) for name, args in (first, second)]
        replies: dict[int, dict] = {}
        while len(replies) < 2:
            if (message := self.read()).get("id") in ids:
                replies[message["id"]] = message
        return [self.outcome(replies[i]) for i in ids]

    def quit(self) -> bool:
        """app_quit; the shutdown may close the socket before the reply is sent, which also counts as accepted."""
        try:
            return self.call("app_quit")[0]
        except ConnectionError:
            return True


def magenta_square(png: bytes) -> dict:
    """The magenta pixels' count and bounding box; a solid square has count == width * height."""
    image = Image.open(io.BytesIO(png)).convert("RGB")
    width = image.width
    points = [(i % width, i // width) for i, pixel in enumerate(image.getdata()) if pixel == MAGENTA]
    if not points:
        return {"count": 0, "width": 0, "height": 0}
    xs, ys = [x for x, _ in points], [y for _, y in points]
    return {"count": len(points), "width": max(xs) - min(xs) + 1, "height": max(ys) - min(ys) + 1}


def expect_square(agent: Agent, checks: Checks, name: str, side: int) -> None:
    ok, _, content, _ = agent.call("view_capture", {"region": "window", "path": f"{name}.png", "overwrite": True})
    image = next((item for item in content if item.get("type") == "image"), None)
    if checks.check(f"{name}: window capture returned an image", ok and image is not None, content[:1]):
        found = magenta_square(base64.b64decode(image["data"]))
        checks.check(f"{name}: one solid {side}x{side} magenta square",
                     found["width"] == found["height"] == side and found["count"] == side * side, found)


def registries(agent: Agent) -> dict:
    """What a load may change, by content: tool specs, each config section (its listing entry and its config_get),
    windows and their state. A failed read raises, so a comparison never rests on error replies."""
    def read(name: str, arguments: dict | None = None) -> dict:
        ok, value, _, _ = agent.call(name, arguments)
        if not ok or not isinstance(value, dict):
            raise RuntimeError(f"registry read {name} {arguments or ''} failed: {value}")
        return value

    sections = {entry["section"]: {"listing": entry, "get": read("config_get", {"section": entry["section"]})}
                for entry in read("config_sections").get("sections", [])}
    tools = sorted(agent.request("tools/list", {}).get("result", {}).get("tools", []), key=lambda t: t["name"])
    return {"tools": tools, "sections": sections, "windows": read("plugin_host_identity").get("windows", [])}


def find(value: object, key: str) -> object:
    """The first value under `key` anywhere in a JSON document."""
    if isinstance(value, dict):
        if key in value:
            return value[key]
        value = list(value.values())
    if isinstance(value, list):
        for item in value:
            if (found := find(item, key)) is not None:
                return found
    return None


def sanitizer_lines(log: str) -> list[str]:
    return [line for line in log.splitlines() if "Sanitizer" in line or "runtime error:" in line or "DEADLYSIGNAL" in line]


def leak_records(log: str) -> list[str]:
    """Each leak record (headline and frames) without addresses, process ids or build ids, sorted."""
    records, current = [], None
    for line in log.splitlines():
        if re.match(r"(Direct|Indirect) leak of", line):
            current = [line.strip()]
            records.append(current)
        elif current is not None and re.match(r"\s+#\d+ ", line):
            frame = re.sub(r"0x[0-9a-f]+|\(BuildId: [0-9a-f]+\)|\+0x[0-9a-f]+", "", line)
            current.append(re.sub(r"\s+", " ", re.sub(r"#\d+", "", frame)).strip())
        elif not line.strip():
            current = None
    return sorted("\n".join(record) for record in records)


def clean_close(close: dict) -> bool:
    """A close attempt that closed the image with no live callable and saw it leave the process."""
    return close.get("closed") is True and close.get("unmapped") is True and \
        all(count == 0 for count in close.get("live", {}).values())


def arguments(description: str) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--icd", type=Path, default=Path(os.environ.get(
        "VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/nvidia_icd.json")), help="the one Vulkan ICD (VK_DRIVER_FILES)")
    parser.add_argument("--deadline", type=float, default=120.0, help="seconds for startup and for exit")
    args = parser.parse_args()
    args.out, args.build = args.out.resolve(), args.build.resolve()
    args.out.mkdir(parents=True)  # never reuse an earlier attempt's directory
    return args


def launch(args: argparse.Namespace, checks: Checks, scenario, expect_crash: bool = False,
           flags: tuple[str, ...] = ("--plugin-test-hooks", "--no-startup-plugins"), client: type | None = None
           ) -> tuple[object, str, list[str]] | None:
    """Runs scenario(agent) against a Sandbox started with the driver's environment and `flags`, and waits for its
    exit. Returns (exit code, stderr, lifecycle events), or None when the environment is not ready. With
    expect_crash the caller judges the exit; the clean-exit checks are skipped. The default flags load no
    built-in plugin, so the fixture plugin is the only one. `client` is the Agent class the scenario gets."""
    out, build = args.out, args.build
    ready = [checks.check("environment: DISPLAY is set", bool(os.environ.get("DISPLAY")), os.environ.get("DISPLAY")),
             checks.check("environment: the Vulkan ICD exists", args.icd.is_file(), str(args.icd)),
             checks.check("environment: built-in config (no config/engine.json)",
                          not (REPO / "config/engine.json").exists()),
             # A longer socket path makes the Sandbox refuse to listen (InvalidPath), which looks like a hang.
             checks.check("environment: the agent socket path fits AF_UNIX (at most 107 bytes)",
                          len(str(out / "agent.sock").encode()) <= 107, str(out / "agent.sock"))]
    if not all(ready):
        return None
    events, suppressions = out / "events.log", out / "lsan.supp"
    suppressions.write_text((REPO / "lsan.supp").read_text() + (Path(__file__).parent / "lsan-driver.supp").read_text())
    env = {k: v for k, v in os.environ.items() if k != "INTRINSIC_ENGINE_CONFIG"}
    env.update(INTRINSIC_PLUGIN_EVENT_LOG=str(events), VK_DRIVER_FILES=str(args.icd),
               LD_PRELOAD=json.loads(args.icd.read_text())["ICD"]["library_path"],
               ASAN_OPTIONS="detect_leaks=1:fast_unwind_on_malloc=0",
               LSAN_OPTIONS=f"detect_leaks=1:fast_unwind_on_malloc=0:suppressions={suppressions}",
               UBSAN_OPTIONS="print_stacktrace=1")
    with (out / "sandbox.stdout").open("wb") as stdout, (out / "sandbox.stderr").open("wb") as stderr:
        sandbox = subprocess.Popen([str(build / "bin/ExtrinsicSandbox"), "--agent-socket", str(out / "agent.sock"),
                                    "--agent-root", str(out), *flags],
                                   cwd=REPO, env=env, stdout=stdout, stderr=stderr)
        try:
            scenario((client or Agent)(out / "agent.sock", args.deadline))
        except Exception as error:  # a crash or a protocol failure is a failed check, not a driver traceback
            checks.check("driver: the session completed", False, f"{type(error).__name__}: {error}")
        try:
            code: object = sandbox.wait(timeout=args.deadline)
        except subprocess.TimeoutExpired:
            sandbox.kill()
            code = "killed after the deadline"
    log = (out / "sandbox.stderr").read_bytes().decode(errors="replace")
    lines = events.read_text().splitlines() if events.exists() else []
    if expect_crash:
        return code, log, lines
    sanitizer = sanitizer_lines(log)
    checks.check("exit: the Sandbox exited with status 0", code == 0, code)
    checks.check("exit: ASan, LSan and UBSan clean", not sanitizer, sanitizer[:3])
    checks.check("exit: no sanitizer error other than a leak report", all(LEAK_REPORT.search(l) for l in sanitizer),
                 [l for l in sanitizer if not LEAK_REPORT.search(l)][:3])
    return code, log, lines


def main() -> int:
    args = arguments(__doc__)
    checks = Checks()
    closes: list[dict] = []
    result = launch(args, checks, lambda agent: closes.extend(run(agent, checks, args.build / "plugins/libIntrinsicFixturePlugin.so")))
    if result is None:
        return finish(checks, args.out)
    _, log, lines = result
    fixture = [r for r in leak_records(log) if any(m in r for m in FIXTURE_FRAMES)]
    checks.check("exit: no leak record has a frame in the plugin or the plugin host code", not fixture, fixture[:2])
    # Each failed attempt that opened an image is closed with no live callable and leaves the process: static
    # construction, Register and its shutdown hook when it reached the commit (every successful Register gets one),
    # static destruction at the close.
    opened = [stage for stage in FAIL_STAGES if stage != "dlopen"]
    clean = [c for c in closes if clean_close(c)]
    checks.check("lifecycle: every failed image closed with no live callable and unmapped",
                 len(closes) == len(clean) == len(opened), closes)
    expected = []
    for stage in opened:
        expected += ["static_construct"] + (["register", "shutdown"] if FAIL_STAGES[stage] == "commit_failed" else []) \
            + ["static_destruct"]
    # app_quit requests the exit; the plugin host runs the plugin's shutdown hook during the engine's shutdown and
    # leaves the image mapped, whose static destruction runs as the process ends.
    expected += ["static_construct", "register", "commit", "first_draw", "config_changed", "shutdown", "host_shutdown",
                 "static_destruct"]
    checks.check("lifecycle: events in order (Register once per staged attempt)", lines == expected, lines)
    return finish(checks, args.out)


def run(agent: Agent, checks: Checks, plugin: Path) -> list[dict]:
    """The S1 session; returns the host's close records of the failed attempts."""
    ok, diagnostics, _, _ = agent.call("diagnostics_read")
    status = find(diagnostics, "device_status")
    checks.check("environment: Vulkan device operational",
                 ok and isinstance(status, dict) and status.get("actual_backend") == "vulkan"
                 and status.get("is_operational") is True, status)
    expect_square(agent, checks, "before_load", 0)
    baseline = registries(agent)
    names = {tool["name"] for tool in baseline["tools"]}
    checks.check("host: plugin host tools present",
                 {"plugin_load", "plugin_unload", "plugin_reload", "plugin_list", "plugin_host_identity", "app_quit"} <= names)
    ok, listed, _, _ = agent.call("plugin_list")
    checks.check("host: plugin_list reports no plugin", ok and listed == {"loaded": []}, listed)
    for stage, code in FAIL_STAGES.items():
        ok, reply, _, error = agent.call("plugin_load", {"path": str(plugin), "fail_at": stage})
        named = code != "commit_failed" or str(reply).startswith(f"{stage}:")
        checks.check(f"failure at {stage}: fails at that stage ({code})", not ok and error == code and named,
                     [error, reply])
        now = registries(agent)
        checks.check(f"failure at {stage}: tools, config sections and windows unchanged", now == baseline,
                     {key: "changed" for key in now if now[key] != baseline[key]})
    # A second load in the same dispatch (host hook) is refused as pending; one sent right behind it over the socket
    # may land in the same frame (pending) or after the commit (already loaded).
    (ok, reply, _, _), (ok2, _, _, error2) = agent.call_both(
        ("plugin_load", {"path": str(plugin), "probe_pending": True}), ("plugin_load", {"path": str(plugin)}))
    checks.check("load: committed", ok and isinstance(reply, dict) and reply.get("loaded") is True, reply)
    generation = reply.get("generation") if isinstance(reply, dict) else None
    checks.check("load: a second load in the same dispatch is refused as load_pending",
                 isinstance(reply, dict) and reply.get("second_attempt") == "load_pending", reply)
    checks.check("load: a second load right behind it is refused", not ok2 and error2 in ("load_pending", "already_loaded"),
                 error2)
    after = registries(agent)
    checks.check("load: tools fixture_echo and fixture_identity registered",
                 {"fixture_echo", "fixture_identity"} <= {tool["name"] for tool in after["tools"]})
    checks.check("load: config section fixture registered", "fixture" in after["sections"], sorted(after["sections"]))
    checks.check("load: window view.fixture registered and open", {"id": "view.fixture", "open": True} in after["windows"],
                 after["windows"])

    _, before, _, _ = agent.call("plugin_host_identity")
    _, seen, _, _ = agent.call("fixture_identity")
    _, host, _, _ = agent.call("plugin_host_identity")
    for probe in ("counter", "inline_static", "thread_local"):
        checks.check(f"identity: {probe} has one address", seen[probe]["address"] == host[probe]["address"],
                     [seen[probe]["address"], host[probe]["address"]])
        checks.check(f"identity: {probe} mutated by the plugin is seen by the host",
                     host[probe]["value"] == before[probe]["value"] + 1 == seen[probe]["value"],
                     [before[probe]["value"], seen[probe]["value"], host[probe]["value"]])
    for key in ("imgui_context", "imgui_alloc", "imgui_free", "imgui_alloc_user_data", "implot_context"):
        set_by_host = key == "imgui_alloc_user_data" or host[key] != 0  # ImGui's default allocator has no user data
        checks.check(f"identity: {key} is the host's", seen[key] == host[key] and set_by_host, [seen[key], host[key]])

    ok, echo, _, _ = agent.call("fixture_echo", {"text": "hello"})
    checks.check("mcp: fixture_echo answers", ok and echo == {"echo": {"text": "hello"}}, echo)
    ok, listed, _, _ = agent.call("plugin_list")
    loaded = next((p for p in (listed.get("loaded") or []) if p.get("name") == "fixture"), None) \
        if isinstance(listed, dict) else None
    checks.check("host: plugin_list reports the loaded generation and its registrations",
                 ok and isinstance(loaded, dict) and loaded.get("generation") == generation
                 and "fixture_echo" in loaded["operations"]
                 and loaded.get("config_sections") == ["fixture"] and loaded.get("history_commands") == ["fixture.add"], listed)

    section = after["sections"].get("fixture", {}).get("get", {})
    checks.check("config: schema declares marker_size as an integer in [1, 64]",
                 section.get("schema", {}).get("properties", {}).get("marker_size")
                 == {"type": "integer", "minimum": 1, "maximum": 64}, section)
    checks.check("config: default is marker_size 8", section.get("defaults") == {"marker_size": 8}, section)
    checks.check("config: the section is active after the load", section.get("active") is True, section)
    expect_square(agent, checks, "marker_default", 8 * 8)
    for bad in ({"marker_size": -12}, {"marker_size": "12"}, {"marker_size": 1.5}, {"marker_size": 65},
                {"marker_size": 12, "extra": 1}):
        ok, reply, _, _ = agent.call("config_apply", {"section": "fixture", "payload": bad})
        checks.check(f"config: invalid payload {json.dumps(bad)} rejected", not ok, reply)
    ok, applied, _, _ = agent.call("config_apply", {"section": "fixture", "payload": {"marker_size": 12}})
    checks.check("config: change to 12 applied", ok, applied)
    ok, section, _, _ = agent.call("config_get", {"section": "fixture"})
    checks.check("config: active payload 12 after the change",
                 ok and section.get("active") is True and section.get("payload") == {"marker_size": 12}, section)
    _, seen, _, _ = agent.call("fixture_identity")
    checks.check("config: the plugin got exactly one change callback with 12",
                 seen.get("marker_size") == 12 and seen.get("config_changes") == 1, seen)
    expect_square(agent, checks, "marker_changed", 12 * 8)
    _, host, _, _ = agent.call("plugin_host_identity")
    checks.check("exit: app_quit accepted", agent.quit())
    return host.get("closes", []) if isinstance(host, dict) else []


def finish(checks: Checks, out: Path) -> int:
    failed = [r["check"] for r in checks.results if not r["pass"]]
    (out / "summary.json").write_text(json.dumps({"checks": checks.results, "failed": failed}, indent=2) + "\n")
    print(f"{len(checks.results) - len(failed)} of {len(checks.results)} checks passed", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
