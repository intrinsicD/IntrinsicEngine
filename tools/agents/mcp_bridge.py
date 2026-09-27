#!/usr/bin/env python3
"""MCP stdio bridge to a running IntrinsicEngine Sandbox (PROC-035, ARCH-019).

The MCP client (e.g. Claude Code via .mcp.json) launches this script. It speaks MCP
(newline-delimited JSON-RPC 2.0) on stdin/stdout and forwards tool calls to the Sandbox's
owner-only Unix socket, which the Sandbox opens only when started with --agent-socket.

The bridge starts even when no Sandbox runs: it then offers only `sandbox_status`, connects
lazily on the next call, and sends notifications/tools/list_changed once the Sandbox's
tools become available (or disappear). Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys

PROTOCOL_VERSION = "2025-06-18"
STATUS_TOOL = {
    "name": "sandbox_status",
    "title": "Sandbox connection",
    "description": (
        "Connect to the running IntrinsicEngine Sandbox and report the connection. Start the "
        "Sandbox with --agent-socket (optionally --agent-readonly, --agent-root <dir>) first."),
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    "annotations": {"readOnlyHint": True, "openWorldHint": False},
}


def default_socket_path() -> str:
    if os.environ.get("INTRINSIC_AGENT_SOCKET"):
        return os.environ["INTRINSIC_AGENT_SOCKET"]
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        return os.path.join(runtime_dir, "intrinsic-sandbox.sock")
    return f"/tmp/intrinsic-sandbox-{os.getuid()}.sock"


class Engine:
    """One connection to the Sandbox; requests are answered in order."""

    def __init__(self, path: str, timeout: float) -> None:
        self.path = path
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.reader = None
        self.next_id = 1
        self.server_info: dict = {}
        self.last_error = ""

    @property
    def connected(self) -> bool:
        return self.sock is not None

    def connect(self) -> bool:
        if self.sock is not None:
            return True
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect(self.path)
        except OSError as error:
            self.last_error = f"{self.path}: {error.strerror or error}"
            return False
        self.sock, self.reader = sock, sock.makefile("r", encoding="utf-8", newline="\n")
        reply = self.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION, "capabilities": {},
            "clientInfo": {"name": "intrinsic-mcp-bridge", "version": "0.1.0"}})
        if reply is None or "result" not in reply:
            self.close("the Sandbox did not answer initialize")
            return False
        self.server_info = reply["result"].get("serverInfo", {})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return True

    def close(self, reason: str = "") -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock, self.reader = None, None
        if reason:
            self.last_error = reason

    def send(self, message: dict) -> bool:
        if self.sock is None:
            return False
        try:
            self.sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
            return True
        except OSError as error:
            self.close(f"send failed: {error}")
            return False

    def request(self, method: str, params: dict | None = None) -> dict | None:
        message_id = f"bridge-{self.next_id}"
        self.next_id += 1
        if not self.send({"jsonrpc": "2.0", "id": message_id, "method": method, "params": params or {}}):
            return None
        try:
            while True:
                line = self.reader.readline()
                if not line:
                    self.close("the Sandbox closed the connection")
                    return None
                reply = json.loads(line)
                if reply.get("id") == message_id:
                    return reply
        except (OSError, ValueError) as error:
            self.close(f"receive failed: {error}")
            return None


class Bridge:
    def __init__(self, engine: Engine, out) -> None:
        self.engine = engine
        self.out = out
        self.tools_visible = False  # whether the client last saw the Sandbox's tools

    def write(self, message: dict) -> None:
        self.out.write(json.dumps(message) + "\n")
        self.out.flush()

    def notify_tools_changed_if_needed(self) -> None:
        if self.tools_visible != self.engine.connected:
            self.write({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})

    def status_text(self) -> str:
        if self.engine.connected:
            return json.dumps({"connected": True, "socket": self.engine.path, "server": self.engine.server_info})
        return json.dumps({"connected": False, "socket": self.engine.path, "error": self.engine.last_error,
                           "hint": "Start the Sandbox with --agent-socket, then call sandbox_status again."})

    def handle(self, message: dict) -> dict | None:
        method = message.get("method")
        message_id = message.get("id")
        if message_id is None:  # notification
            return None
        result = lambda value: {"jsonrpc": "2.0", "id": message_id, "result": value}
        if method == "initialize":
            self.engine.connect()
            return result({
                "protocolVersion": message.get("params", {}).get("protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": "intrinsic-sandbox-bridge", "version": "0.1.0"},
                "instructions": "Tools of the running IntrinsicEngine Sandbox, reached through its --agent-socket. "
                                "Call sandbox_status if they are missing."})
        if method == "ping":
            return result({})
        if method == "tools/list":
            tools = [STATUS_TOOL]
            if self.engine.connect():
                reply = self.engine.request("tools/list")
                if reply is not None and "result" in reply:
                    tools += reply["result"].get("tools", [])
            self.tools_visible = self.engine.connected
            return result({"tools": tools})
        if method == "tools/call":
            params = message.get("params", {})
            if params.get("name") == STATUS_TOOL["name"]:
                self.engine.connect()
                self.notify_tools_changed_if_needed()
                return result({"content": [{"type": "text", "text": self.status_text()}], "isError": False})
            if not self.engine.connect():
                self.notify_tools_changed_if_needed()
                return result({"content": [{"type": "text", "text": self.status_text()}], "isError": True})
            reply = self.engine.request("tools/call", params)
            if reply is None:
                self.notify_tools_changed_if_needed()
                return result({"content": [{"type": "text", "text": self.status_text()}], "isError": True})
            reply["id"] = message_id
            return reply
        return {"jsonrpc": "2.0", "id": message_id, "error": {"code": -32601, "message": f"Method not found: {method}"}}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--socket", default=default_socket_path(), help="Sandbox agent socket path")
    parser.add_argument("--timeout", type=float, default=120.0, help="seconds to wait for one Sandbox reply")
    args = parser.parse_args(argv)
    bridge = Bridge(Engine(args.socket, args.timeout), sys.stdout)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except ValueError:
            bridge.write({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}})
            continue
        if not isinstance(message, dict):
            bridge.write({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid request"}})
            continue
        response = bridge.handle(message)
        if response is not None:
            bridge.write(response)
    bridge.engine.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
