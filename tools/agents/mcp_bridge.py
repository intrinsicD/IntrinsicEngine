#!/usr/bin/env python3
"""MCP stdio bridge to a running IntrinsicEngine Sandbox (PROC-035, ARCH-019, RUNTIME-312).

The MCP client (e.g. Claude Code via .mcp.json) launches this script. It speaks MCP
(newline-delimited JSON-RPC 2.0) on stdin/stdout and forwards tool calls to the Sandbox's
owner-only Unix socket, which the Sandbox opens only when started with --agent-socket.

One single-threaded `selectors` loop serves both ends, so calls are concurrent:
  * tools/call is forwarded under a fresh `bridge-N` id (params, including
    `_meta.progressToken`, untouched) and the loop continues; the reply gets the client's id back.
  * Server messages without an id (e.g. notifications/progress) are forwarded to the client verbatim.
  * `ping` is answered by the bridge at once, even while calls are pending.
  * Each call has its own deadline (--timeout). On expiry the client gets an error result saying the
    call may still be running in the Sandbox; the connection stays open and a late reply is dropped.
  * notifications/cancelled drops the pending call and is forwarded upstream with the server id.
  * If the Sandbox disconnects, every pending call is answered with an error result.
Short internal requests (upstream initialize, tools/list, ping) block for up to 10 s; client input
is buffered meanwhile.

The bridge starts without acquiring the engine and offers connection tools plus `sandbox_tools`/`sandbox_call` for clients that cache tool lists.
After an explicit sandbox_status and notifications/initialized it probes an unavailable socket
every --probe-interval seconds and sends
notifications/tools/list_changed once the Sandbox's tools become available (or disappear).
Protocol versions 2025-06-18, 2025-03-26 and 2024-11-05 are negotiated. Standard library only.
A busy refusal or explicit disconnect pauses reconnect until sandbox_status is called again.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import selectors
import socket
import sys
import time

SUPPORTED_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
LATEST_VERSION = SUPPORTED_VERSIONS[0]
INTERNAL_TIMEOUT = 10.0  # seconds for bridge-internal requests (initialize, tools/list, ping)
STATUS_TOOL = {
    "name": "sandbox_status",
    "title": "Sandbox connection",
    "description": (
        "Connect to the running IntrinsicEngine Sandbox and report the connection. Start the "
        "Sandbox with --agent-socket (optionally --agent-readonly, --agent-root <dir>) first."),
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    "annotations": {"readOnlyHint": True, "openWorldHint": False},
}
DISCONNECT_TOOL = {
    "name": "sandbox_disconnect",
    "title": "Release Sandbox connection",
    "description": (
        "Release this client's connection so another agent can use the Sandbox. Refuses while "
        "tool calls are pending. Automatic reconnect stays paused until sandbox_status is called; "
        "timed-out or cancelled operations may still finish in the engine."),
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    "annotations": {"readOnlyHint": False, "destructiveHint": False, "openWorldHint": False},
}
CATALOG_TOOL = {
    "name": "sandbox_tools", "title": "Sandbox tool catalog",
    "description": "List the running Sandbox's operation names and descriptions. Pass name to read one operation's full input schema. Use with sandbox_call if your client does not refresh dynamically added tools; call sandbox_status first.",
    "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}, "additionalProperties": False},
    "annotations": {"readOnlyHint": True, "openWorldHint": False},
}
CALL_TOOL = {
    "name": "sandbox_call", "title": "Call a registered Sandbox operation",
    "description": "Call an engine operation from sandbox_tools by name and arguments. Uses the same validated registry and read-only policy as named tools. May change the scene or files; inspect the operation's schema and annotations first. This also works in clients that cache the startup tool list.",
    "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}, "arguments": {"type": "object"}},
                    "required": ["name"], "additionalProperties": False},
    "annotations": {"readOnlyHint": False, "destructiveHint": True, "openWorldHint": False},
}
LOCAL_TOOLS = [STATUS_TOOL, DISCONNECT_TOOL, CATALOG_TOOL, CALL_TOOL]


def default_socket_path() -> str:
    if os.environ.get("INTRINSIC_AGENT_SOCKET"):
        return os.environ["INTRINSIC_AGENT_SOCKET"]
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        return os.path.join(runtime_dir, "intrinsic-sandbox.sock")
    return f"/tmp/intrinsic-sandbox-{os.getuid()}.sock"


def pop_line(buffer: bytearray) -> bytes | None:
    """Removes and returns the first newline-terminated line of `buffer`, or None."""
    end = buffer.find(b"\n")
    if end < 0:
        return None
    line = bytes(buffer[:end])
    del buffer[:end + 1]
    return line


class Bridge:
    def __init__(self, path: str, timeout: float, probe_interval: float, out) -> None:
        self.path = path
        self.timeout = timeout
        self.probe_interval = probe_interval
        self.out = out
        self.selector = selectors.DefaultSelector()
        self.sock: socket.socket | None = None
        self.inbuf = bytearray()      # socket line buffer
        self.stdin_buf = bytearray()  # stdin line buffer
        self.next_id = 1
        self.server_info: dict = {}
        self.last_error = ""
        self.generation = 0           # counts successful connections (a restarted Sandbox is a new one)
        self.pending: dict[str, tuple[object, float]] = {}  # server id -> (client id, deadline)
        self.abandoned: set[str] = set()                    # server ids whose reply is to be dropped
        self.client_initialized = False
        self.tools_visible = False    # whether the client last saw the Sandbox's tools
        self.tools_generation = 0     # connection whose tools the client last saw
        self.next_probe = 0.0
        self.unresponsive = False     # the last ping probe timed out
        self.running = True
        self.released = False
        self.busy = False
        self.connection_requested = False

    # -- client side ------------------------------------------------------------------------------

    @property
    def connected(self) -> bool:
        return self.sock is not None

    def write(self, message: dict) -> None:
        try:
            self.out.write(json.dumps(message) + "\n")
            self.out.flush()
        except OSError:  # the client is gone (e.g. BrokenPipeError)
            self.running = False

    def notify_tools_changed_if_needed(self) -> None:
        if not self.client_initialized:
            return
        # A restarted Sandbox may be a different build, so a new connection also refreshes the list.
        if self.tools_visible != self.connected or (self.connected and self.tools_generation != self.generation):
            self.write({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
            self.tools_visible, self.tools_generation = self.connected, self.generation

    def status_text(self) -> str:
        if self.connected:
            status = {"connected": True, "socket": self.path, "server": self.server_info}
            if self.unresponsive:
                status["warning"] = "connected but not answering ping; it may be busy"
            return json.dumps(status)
        hint = ("Connection released. Call sandbox_status to reconnect." if self.released else
                "Another client owns the Sandbox. Ask it to call sandbox_disconnect, or use View > Agent Connection "
                "to disconnect it, then call sandbox_status." if self.busy else
                "Call sandbox_status to acquire the Sandbox connection." if not self.connection_requested else
                "Start the Sandbox with --agent-socket, then call sandbox_status again.")
        return json.dumps({"connected": False, "socket": self.path, "error": self.last_error,
                           "code": "released" if self.released else "agent_busy" if self.busy else "unavailable",
                           "hint": hint})

    @staticmethod
    def error_result(client_id, text: str) -> dict:
        return {"jsonrpc": "2.0", "id": client_id,
                "result": {"content": [{"type": "text", "text": text}], "isError": True}}

    # -- upstream connection ----------------------------------------------------------------------

    def connect(self) -> bool:
        if self.released or self.busy or not self.connection_requested:
            return False
        if self.sock is not None:
            return True
        self.busy = False
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.settimeout(INTERNAL_TIMEOUT)
            sock.connect(self.path)
        except OSError as error:
            sock.close()
            self.last_error = f"{self.path}: {error.strerror or error}"
            return False
        self.sock, self.inbuf = sock, bytearray()
        self.selector.register(sock, selectors.EVENT_READ, "sock")
        reply = self.request_sync("initialize", {
            "protocolVersion": LATEST_VERSION, "capabilities": {},
            "clientInfo": {"name": "intrinsic-mcp-bridge", "version": "0.2.0"}})
        if reply is None or "result" not in reply:
            if self.connected:
                self.close("the Sandbox did not answer initialize")
            return False
        self.server_info = reply["result"].get("serverInfo", {})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        if not self.connected:
            return False
        self.generation += 1
        return True

    def close(self, reason: str = "") -> None:
        """Drops the connection and answers every pending call with an error result."""
        if self.sock is not None:
            try:
                self.selector.unregister(self.sock)
            except (KeyError, ValueError):
                pass
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock, self.inbuf = None, bytearray()
        if reason:
            self.last_error = reason
        for client_id, _ in self.pending.values():
            self.write(self.error_result(
                client_id, "The Sandbox connection was lost while this call was pending; it may or may not "
                           f"have completed. {self.status_text()}"))
        self.pending.clear()
        self.abandoned.clear()

    def send(self, message: dict) -> bool:
        if self.sock is None:
            return False
        try:
            self.sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
            return True
        except OSError as error:
            self.close(f"send failed: {error}")
            return False

    def new_id(self) -> str:
        message_id = f"bridge-{self.next_id}"
        self.next_id += 1
        return message_id

    def handle_server_message(self, line: bytes) -> dict | None:
        """Routes one upstream line; returns it when it is an unrouted reply (an internal request's), else None."""
        if not line.strip():
            return None
        try:
            message = json.loads(line)
        except ValueError:
            return None
        if not isinstance(message, dict):
            return None
        error = message.get("error")
        if message.get("id") is None and isinstance(error, dict) and error.get("code") == -32001:
            self.busy = True
            self.close(error.get("message", "Another client owns the Sandbox connection."))
            return None
        if "id" not in message:
            self.write(message)  # e.g. notifications/progress, forwarded verbatim
            return None
        if "method" in message:
            return None          # the Sandbox does not send requests; ignore
        server_id = message["id"]
        if isinstance(server_id, str) and server_id in self.pending:
            client_id, _ = self.pending.pop(server_id)
            message["id"] = client_id
            self.write(message)
            return None
        if isinstance(server_id, (str, int)) and server_id in self.abandoned:
            self.abandoned.discard(server_id)
            return None
        return message

    def read_socket(self) -> bool:
        """One recv into the line buffer. False on EOF/error (the connection is then closed)."""
        try:
            data = self.sock.recv(65536)
        except OSError as error:
            self.close(f"receive failed: {error}")
            return False
        if not data:
            self.close("the Sandbox closed the connection")
            return False
        self.inbuf += data
        return True

    def request_sync(self, method: str, params: dict | None = None) -> dict | None:
        """Short internal request: blocks up to INTERNAL_TIMEOUT; client input stays buffered."""
        message_id = self.new_id()
        if not self.send({"jsonrpc": "2.0", "id": message_id, "method": method, "params": params or {}}):
            return None
        deadline = time.monotonic() + INTERNAL_TIMEOUT
        while self.sock is not None:
            line = pop_line(self.inbuf)
            if line is not None:
                reply = self.handle_server_message(line)
                if reply is not None and reply.get("id") == message_id:
                    return reply
                continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.last_error = f"the Sandbox did not answer {method} within {INTERNAL_TIMEOUT:g} s"
                return None
            ready, _, _ = select.select([self.sock], [], [], remaining)
            if ready and not self.read_socket():
                return None
        return None

    def probe(self) -> bool:
        """Reconnects when the current connection no longer answers (e.g. the Sandbox restarted)."""
        self.unresponsive = False
        if self.connected:
            # A dead connection closes itself through EOF/send errors; a timeout leaves it open.
            self.unresponsive = self.request_sync("ping") is None and self.connected
        return self.connect()

    # -- request handling -------------------------------------------------------------------------

    def forward_call(self, message: dict) -> dict | None:
        """Forwards a tools/call; returns an immediate reply only on failure."""
        self.connection_requested = True
        client_id, params = message["id"], message.get("params", {})
        for _ in range(2):
            if not self.connect():
                break
            server_id = self.new_id()
            if self.send({"jsonrpc": "2.0", "id": server_id, "method": "tools/call", "params": params}):
                self.pending[server_id] = (client_id, time.monotonic() + self.timeout)
                self.notify_tools_changed_if_needed()  # a call-driven reconnect may bring new tools
                return None
            # The send failed, so the call never reached the dead connection: retry once on a new one.
        self.notify_tools_changed_if_needed()
        return self.error_result(client_id, self.status_text())

    def handle(self, message: dict) -> dict | None:
        method = message.get("method")
        message_id = message.get("id")
        params = message.get("params")
        params = params if isinstance(params, dict) else {}
        if message_id is None and "id" in message:
            return {"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid request"}}
        if message_id is None:  # notification
            if method == "notifications/initialized":
                self.client_initialized = True
                self.next_probe = time.monotonic() + self.probe_interval
            elif method == "notifications/cancelled":
                self.cancel(params)
            return None
        result = lambda value: {"jsonrpc": "2.0", "id": message_id, "result": value}
        if method == "initialize":
            requested = params.get("protocolVersion")
            return result({
                "protocolVersion": requested if requested in SUPPORTED_VERSIONS else LATEST_VERSION,
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": "intrinsic-sandbox-bridge", "version": "0.2.0"},
                "instructions": "Tools of the running IntrinsicEngine Sandbox, reached through its --agent-socket. "
                                "Start with sandbox_status, then scene_entities and entity_properties. If named engine tools "
                                "are unavailable, use sandbox_tools to inspect their schemas and sandbox_call to call them. Inspect "
                                "config_get and config_preview before authorized config_apply; use preview_* before run_*. "
                                "After a timeout inspect jobs_list and the scene before retrying a mutation. Release "
                                "the connection with sandbox_disconnect when finished so another agent can connect."})
        if method == "ping":
            return result({})
        if method == "tools/list":
            tools = list(LOCAL_TOOLS)
            listed = False
            if self.connect():
                reply = self.request_sync("tools/list")
                if reply is not None and "result" in reply:
                    tools += reply["result"].get("tools", [])
                    listed = True
            self.tools_visible = self.connected and listed
            self.tools_generation = self.generation
            return result({"tools": tools})
        if method == "tools/call":
            if params.get("name") in (CATALOG_TOOL["name"], CALL_TOOL["name"]):
                args = params.get("arguments", {})
                if not isinstance(args, dict) or ("name" in args and not isinstance(args["name"], str)):
                    return self.error_result(message_id, "Pass an object with an optional string name.")
                if params["name"] == CALL_TOOL["name"]:
                    if not args.get("name") or not isinstance(args.get("arguments", {}), dict):
                        return self.error_result(message_id, "Pass {name: <operation>, arguments: <object>} from sandbox_tools.")
                    forwarded = {"name": args["name"], "arguments": args.get("arguments", {})}
                    if "_meta" in params:
                        forwarded["_meta"] = params["_meta"]
                    return self.forward_call({**message, "params": forwarded})
                self.connection_requested = True
                if not self.connect():
                    return self.error_result(message_id, self.status_text())
                reply = self.request_sync("tools/list")
                self.notify_tools_changed_if_needed()
                if reply is None or "result" not in reply:
                    return self.error_result(message_id, "The Sandbox did not return its tool catalog. " + self.status_text())
                entries = reply["result"].get("tools", [])
                if "name" in args:
                    entries = [tool for tool in entries if tool["name"] == args["name"]]
                    if not entries:
                        return self.error_result(message_id, "Unknown Sandbox operation: " + args["name"])
                else:
                    entries = [{key: tool[key] for key in ("name", "title", "description", "annotations") if key in tool}
                               for tool in entries]
                return result({"content": [{"type": "text", "text": json.dumps({"tools": entries})}], "isError": False})
            if params.get("name") == STATUS_TOOL["name"]:
                self.released = False
                self.busy = False
                self.connection_requested = True
                self.probe()
                self.notify_tools_changed_if_needed()
                return result({"content": [{"type": "text", "text": self.status_text()}], "isError": False})
            if params.get("name") == DISCONNECT_TOOL["name"]:
                if self.pending:
                    return self.error_result(message_id, "Tool calls are pending; wait for their replies before releasing the connection.")
                self.released = True
                self.close("Connection released by this client.")
                self.notify_tools_changed_if_needed()
                return result({"content": [{"type": "text", "text": self.status_text()}], "isError": False})
            return self.forward_call(message)
        return {"jsonrpc": "2.0", "id": message_id, "error": {"code": -32601, "message": f"Method not found: {method}"}}

    def abandon(self, server_id: str) -> None:
        if len(self.abandoned) >= 4096:  # bounded; ids are unique per bridge, so stale entries are harmless
            self.abandoned.clear()
        self.abandoned.add(server_id)

    def cancel(self, params: dict) -> None:
        request_id = params.get("requestId")
        for server_id, (client_id, _) in list(self.pending.items()):
            if client_id == request_id and type(client_id) is type(request_id):
                del self.pending[server_id]
                self.abandon(server_id)
                self.send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                           "params": {"requestId": server_id,
                                      "reason": params.get("reason", "cancelled by the client")}})
                return

    def handle_line(self, line: bytes) -> None:
        line = line.strip()
        if not line:
            return
        try:
            message = json.loads(line)
        except ValueError:
            self.write({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}})
            return
        if not isinstance(message, dict):
            self.write({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid request"}})
            return
        response = self.handle(message)
        if response is not None:
            self.write(response)

    # -- loop -------------------------------------------------------------------------------------

    def expire_calls(self, now: float) -> None:
        for server_id, (client_id, deadline) in list(self.pending.items()):
            if deadline <= now:
                del self.pending[server_id]
                self.abandon(server_id)
                self.write(self.error_result(
                    client_id, f"No reply within {self.timeout:g} s. The call is still running in the Sandbox "
                               "and may complete; poll `jobs_list` or `scene_entities` for its effect."))

    def probe_if_due(self, now: float) -> None:
        if (not self.connection_requested or self.released or self.busy or self.connected
                or not self.client_initialized or now < self.next_probe):
            return
        self.next_probe = now + self.probe_interval
        if self.connect():
            self.notify_tools_changed_if_needed()

    def next_timeout(self, now: float) -> float | None:
        deadlines = [deadline for _, deadline in self.pending.values()]
        if self.connection_requested and not self.released and not self.busy and not self.connected and self.client_initialized:
            deadlines.append(self.next_probe)
        return max(0.0, min(deadlines) - now) if deadlines else None

    def run(self) -> int:
        try:
            self.selector.register(0, selectors.EVENT_READ, "stdin")
        except OSError:  # a regular file as stdin cannot be polled: read it to the end instead
            while self.running:
                self.read_stdin()
            self.close()
            return 0
        while self.running:
            for key, _ in self.selector.select(self.next_timeout(time.monotonic())):
                if key.data == "stdin":
                    self.read_stdin()
                elif key.fileobj is self.sock:
                    self.serve_socket()
            now = time.monotonic()
            self.expire_calls(now)
            self.probe_if_due(now)
        self.close()
        return 0

    def read_stdin(self) -> None:
        data = os.read(0, 65536)
        if not data:
            self.running = False
            return
        self.stdin_buf += data
        while self.running:
            line = pop_line(self.stdin_buf)
            if line is None:
                break
            self.handle_line(line)

    def serve_socket(self) -> None:
        if not self.read_socket():
            self.notify_tools_changed_if_needed()
            return
        while self.sock is not None:
            line = pop_line(self.inbuf)
            if line is None:
                break
            self.handle_server_message(line)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--socket", default=default_socket_path(), help="Sandbox agent socket path")
    parser.add_argument("--timeout", type=float, default=120.0, help="seconds to wait for one tool call's reply")
    parser.add_argument("--probe-interval", type=float, default=2.0,
                        help="seconds between connection attempts while the Sandbox is away")
    args = parser.parse_args(argv)
    return Bridge(args.socket, args.timeout, args.probe_interval, sys.stdout).run()


if __name__ == "__main__":
    sys.exit(main())
