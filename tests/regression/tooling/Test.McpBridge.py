#!/usr/bin/env python3
"""tools/agents/mcp_bridge.py (a subprocess) against a fake Sandbox socket (PROC-035, RUNTIME-312)."""

import json
import os
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BRIDGE = ROOT / "tools/agents/mcp_bridge.py"
WAIT = 8.0
LIST_CHANGED = {"jsonrpc": "2.0", "method": "notifications/tools/list_changed"}


class FakeSandbox:
    """Answers initialize, tools/list and tools/call like the Sandbox's agent server.

    `slow_tool` replies only after `release` is set (on its own thread, so other requests are served
    meanwhile); `accepts` counts connections; `received` records every message the bridge sent.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(4)
        self.accepts = 0
        self.received = []
        self.calls = []
        self.connections = []
        self.release = threading.Event()
        self.slow_replied = threading.Event()
        self.lock = threading.Lock()
        self.threads = [threading.Thread(target=self.accept_loop, daemon=True)]
        self.threads[0].start()

    def accept_loop(self) -> None:
        while True:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            with self.lock:
                self.accepts += 1
                self.connections.append(connection)
            thread = threading.Thread(target=self.serve, args=(connection,), daemon=True)
            thread.start()
            self.threads.append(thread)

    def reply(self, connection, message) -> None:
        with self.lock:
            try:
                connection.sendall((json.dumps(message) + "\n").encode())
            except OSError:
                pass

    def serve(self, connection) -> None:
        with connection.makefile("r") as reader:
            for line in reader:
                message = json.loads(line)
                self.received.append(message)
                if "id" not in message:
                    continue
                method = message["method"]
                self.calls.append(method)
                if method == "initialize":
                    result = {"protocolVersion": "2025-06-18", "capabilities": {"tools": {}},
                              "serverInfo": {"name": "intrinsic-sandbox"}}
                elif method == "tools/list":
                    result = {"tools": [{"name": "scene_entities", "inputSchema": {"type": "object"}},
                                        {"name": "slow_tool", "inputSchema": {"type": "object"}}]}
                elif method == "tools/call" and message["params"].get("name") == "slow_tool":
                    threading.Thread(target=self.answer_slow, args=(connection, message), daemon=True).start()
                    continue
                elif method == "tools/call":
                    result = {"content": [{"type": "text", "text": json.dumps(message["params"])}], "isError": False}
                else:
                    result = {}
                self.reply(connection, {"jsonrpc": "2.0", "id": message["id"], "result": result})

    def answer_slow(self, connection, message) -> None:
        self.release.wait(WAIT)
        self.reply(connection, {"jsonrpc": "2.0", "id": message["id"], "result": {
            "content": [{"type": "text", "text": "slow done"}], "isError": False}})
        self.slow_replied.set()

    def emit(self, message) -> None:
        for connection in list(self.connections):
            self.reply(connection, message)

    def wait_received(self, predicate, timeout=WAIT):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for message in list(self.received):
                if predicate(message):
                    return message
            time.sleep(0.01)
        return None

    def close(self) -> None:
        self.server.close()

    def stop(self) -> None:
        """Like a Sandbox exit: the listener and the bridge's connections go away."""
        self.server.close()
        for connection in self.connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()
        if os.path.exists(self.path):
            os.unlink(self.path)


class BridgeProcess:
    """The bridge as the MCP client sees it: JSON lines in, JSON lines out."""

    def __init__(self, path: str, timeout: float = 5.0, probe_interval: float = 0.1) -> None:
        self.process = subprocess.Popen(
            [sys.executable, str(BRIDGE), "--socket", path, "--timeout", str(timeout),
             "--probe-interval", str(probe_interval)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.messages = queue.Queue()
        self.log = []
        self.next_id = 1
        threading.Thread(target=self.read, daemon=True).start()

    def read(self) -> None:
        for line in self.process.stdout:
            self.messages.put(json.loads(line))
        self.messages.put(None)

    def send(self, message) -> None:
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def post(self, method, params=None, message_id=None) -> object:
        message_id = message_id if message_id is not None else self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": message_id, "method": method, "params": params or {}})
        return message_id

    def wait(self, predicate, timeout=WAIT):
        for index, message in enumerate(self.log):
            if predicate(message):
                del self.log[index]
                return message
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.last_log = list(self.log)
                raise AssertionError(f"timed out waiting; unmatched messages: {self.log}")
            try:
                message = self.messages.get(timeout=remaining)
            except queue.Empty:
                continue
            if message is None:
                raise AssertionError(f"bridge exited; unmatched messages: {self.log}")
            if predicate(message):
                return message
            self.log.append(message)

    def reply_to(self, message_id, timeout=WAIT):
        return self.wait(lambda message: message.get("id") == message_id and "method" not in message, timeout)

    def request(self, method, params=None, message_id=None, timeout=WAIT):
        return self.reply_to(self.post(method, params, message_id), timeout)

    def call(self, name, arguments=None, message_id=None, timeout=WAIT):
        return self.request("tools/call", {"name": name, "arguments": arguments or {}}, message_id, timeout)

    def wait_list_changed(self, timeout=WAIT):
        return self.wait(lambda message: message == LIST_CHANGED, timeout)

    def start(self, version="2025-06-18"):
        reply = self.request("initialize", {"protocolVersion": version})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return reply

    def stop(self) -> None:
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()


def tool_names(reply):
    return [tool["name"] for tool in reply["result"]["tools"]]


def result_text(reply):
    return reply["result"]["content"][0]["text"]


class McpBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.directory.name, "sandbox.sock")
        self.sandboxes = []
        self.bridges = []

    def tearDown(self) -> None:
        for bridge in self.bridges:
            bridge.stop()
        for sandbox in self.sandboxes:
            sandbox.release.set()
            sandbox.close()
        self.directory.cleanup()

    def sandbox(self) -> FakeSandbox:
        sandbox = FakeSandbox(self.path)
        self.sandboxes.append(sandbox)
        return sandbox

    def bridge(self, **kwargs) -> BridgeProcess:
        bridge = BridgeProcess(self.path, **kwargs)
        self.bridges.append(bridge)
        return bridge

    def test_starts_without_a_sandbox_and_connects_later(self):
        bridge = self.bridge(probe_interval=60)  # no background probe: sandbox_status connects
        init = bridge.start()
        self.assertTrue(init["result"]["capabilities"]["tools"]["listChanged"])
        self.assertEqual(tool_names(bridge.request("tools/list")), ["sandbox_status"])
        failed = bridge.call("scene_entities")
        self.assertTrue(failed["result"]["isError"])
        self.assertIn("--agent-socket", result_text(failed))

        sandbox = self.sandbox()
        status = bridge.call("sandbox_status")
        self.assertTrue(json.loads(result_text(status))["connected"])
        bridge.wait_list_changed()
        self.assertEqual(tool_names(bridge.request("tools/list")), ["sandbox_status", "scene_entities", "slow_tool"])
        reply = bridge.call("scene_entities", {"x": 1}, message_id="client-7")
        self.assertEqual(reply["id"], "client-7", "the client's id is restored")
        self.assertEqual(json.loads(result_text(reply))["arguments"], {"x": 1})
        self.assertEqual(sandbox.calls[:2], ["initialize", "tools/list"])

    def test_reports_a_lost_sandbox_and_other_methods(self):
        sandbox = self.sandbox()
        bridge = self.bridge(probe_interval=60)
        bridge.start()
        self.assertEqual(len(bridge.request("tools/list")["result"]["tools"]), 3)
        sandbox.stop()
        bridge.wait_list_changed()  # the loop notices the closed connection by itself
        lost = bridge.call("scene_entities")
        self.assertTrue(lost["result"]["isError"])
        self.assertEqual(bridge.request("ping")["result"], {})
        self.assertEqual(bridge.request("resources/list")["error"]["code"], -32601)
        bridge.send({"jsonrpc": "2.0", "method": "notifications/unknown"})
        self.assertEqual(bridge.request("ping")["result"], {}, "notifications get no reply")

    def test_malformed_input_is_answered_with_errors(self):
        bridge = self.bridge()
        bridge.process.stdin.write("{not json\n[1]\n")
        bridge.process.stdin.flush()
        self.assertEqual(bridge.wait(lambda m: m["id"] is None)["error"]["code"], -32700)
        self.assertEqual(bridge.wait(lambda m: m["id"] is None)["error"]["code"], -32600)

    def test_a_restarted_sandbox_is_reconnected_and_refreshes_the_tools(self):
        first = self.sandbox()
        bridge = self.bridge(probe_interval=60)
        bridge.start()
        bridge.request("tools/list")
        first.stop()
        bridge.wait_list_changed()
        second = self.sandbox()
        status = bridge.call("sandbox_status")
        self.assertTrue(json.loads(result_text(status))["connected"],
                        "sandbox_status probes the stale connection and reconnects")
        bridge.wait_list_changed()  # the new Sandbox's tools are announced
        reply = bridge.call("scene_entities")
        self.assertFalse(reply["result"]["isError"])
        self.assertIn("tools/call", second.calls)

    def test_a_call_that_never_left_is_retried_on_a_new_connection(self):
        first = self.sandbox()
        bridge = self.bridge(probe_interval=60)
        bridge.start()
        first.stop()
        second = self.sandbox()
        # The bridge may still hold the dead connection; a failed send is retried on a new one.
        deadline = time.monotonic() + WAIT
        reply = bridge.call("scene_entities", {"n": 2})
        while reply["result"]["isError"] and time.monotonic() < deadline:
            reply = bridge.call("scene_entities", {"n": 2})
        self.assertFalse(reply["result"]["isError"], reply)
        self.assertIn("tools/call", second.calls)
        bridge.wait_list_changed()  # the call-driven reconnect announces the new Sandbox's tools

    def test_odd_ids_do_not_kill_the_loop(self):
        bridge = self.bridge()
        bridge.process.stdin.write('{"jsonrpc":"2.0","id":null,"method":"ping"}\n')
        bridge.process.stdin.flush()
        self.assertEqual(bridge.wait(lambda m: m.get("id") is None)["error"]["code"], -32600)
        sandbox = self.sandbox()
        bridge.start()
        bridge.request("tools/list")
        sandbox.emit({"jsonrpc": "2.0", "id": [1], "result": {}})
        sandbox.emit({"jsonrpc": "2.0", "id": {}, "result": {}})
        self.assertEqual(bridge.request("ping")["result"], {})

    def test_endpoints_are_unix_socket_paths_only(self):
        # A host:port string is treated as a (missing) socket path, never as a network address.
        bridge = BridgeProcess("127.0.0.1:8080", probe_interval=60)
        self.bridges.append(bridge)
        bridge.start()
        status = json.loads(result_text(bridge.call("sandbox_status")))
        self.assertFalse(status["connected"])
        self.assertIn("127.0.0.1:8080", status["error"])

    def connected_bridge(self, **kwargs):
        sandbox = self.sandbox()
        bridge = self.bridge(**kwargs)
        bridge.start()
        bridge.request("tools/list")
        return sandbox, bridge

    def test_ping_answered_during_pending_call(self):
        sandbox, bridge = self.connected_bridge()
        call_id = bridge.post("tools/call", {"name": "slow_tool", "arguments": {}})
        self.assertIsNotNone(sandbox.wait_received(lambda m: m.get("params", {}).get("name") == "slow_tool"))
        self.assertEqual(bridge.request("ping")["result"], {})
        sandbox.release.set()
        self.assertEqual(result_text(bridge.reply_to(call_id)), "slow done")

    def test_server_notifications_forwarded(self):
        sandbox, bridge = self.connected_bridge()
        progress = {"jsonrpc": "2.0", "method": "notifications/progress",
                    "params": {"progressToken": "tok", "progress": 1, "total": 2}}
        sandbox.emit(progress)
        self.assertEqual(bridge.wait(lambda m: m.get("method") == "notifications/progress"), progress)
        call_id = bridge.post("tools/call", {"name": "scene_entities", "_meta": {"progressToken": "tok"}})
        echoed = json.loads(result_text(bridge.reply_to(call_id)))
        self.assertEqual(echoed["_meta"], {"progressToken": "tok"}, "params pass through untouched")

    def test_call_timeout_returns_error_and_keeps_connection(self):
        sandbox, bridge = self.connected_bridge(timeout=0.3)
        reply = bridge.call("slow_tool")
        self.assertTrue(reply["result"]["isError"])
        self.assertIn("still running", result_text(reply))
        second = bridge.call("scene_entities", {"after": "timeout"})
        self.assertFalse(second["result"]["isError"])
        self.assertEqual(sandbox.accepts, 1, "the timeout did not reconnect")

    def test_late_reply_after_timeout_is_discarded(self):
        sandbox, bridge = self.connected_bridge(timeout=0.3)
        slow_id = bridge.post("tools/call", {"name": "slow_tool"})
        self.assertTrue(bridge.reply_to(slow_id)["result"]["isError"])
        sandbox.release.set()
        self.assertTrue(sandbox.slow_replied.wait(WAIT))
        fast_id = bridge.post("tools/call", {"name": "scene_entities"})
        self.assertFalse(bridge.reply_to(fast_id)["result"]["isError"])
        stray = [m for m in bridge.log if m.get("id") == slow_id]
        self.assertEqual(stray, [], "the late reply never reaches the client")

    def test_cancelled_notification_drops_pending_and_informs_server(self):
        sandbox, bridge = self.connected_bridge()
        call_id = bridge.post("tools/call", {"name": "slow_tool"}, message_id="client-9")
        forwarded = sandbox.wait_received(lambda m: m.get("params", {}).get("name") == "slow_tool")
        self.assertIsNotNone(forwarded)
        bridge.send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                     "params": {"requestId": call_id, "reason": "user pressed stop"}})
        cancelled = sandbox.wait_received(lambda m: m.get("method") == "notifications/cancelled")
        self.assertIsNotNone(cancelled)
        self.assertEqual(cancelled["params"]["requestId"], forwarded["id"])
        self.assertTrue(cancelled["params"]["reason"])
        sandbox.release.set()
        self.assertTrue(sandbox.slow_replied.wait(WAIT))
        self.assertFalse(bridge.call("scene_entities")["result"]["isError"])
        self.assertEqual([m for m in bridge.log if m.get("id") == call_id], [], "no reply for a cancelled call")

    def test_pending_calls_fail_when_sandbox_exits(self):
        sandbox, bridge = self.connected_bridge()
        call_id = bridge.post("tools/call", {"name": "slow_tool"})
        self.assertIsNotNone(sandbox.wait_received(lambda m: m.get("params", {}).get("name") == "slow_tool"))
        sandbox.stop()
        reply = bridge.reply_to(call_id)
        self.assertTrue(reply["result"]["isError"])
        self.assertIn("lost", result_text(reply))
        bridge.wait_list_changed()

    def test_later_started_sandbox_announced_via_list_changed(self):
        bridge = self.bridge(probe_interval=0.05)
        bridge.start()
        self.assertEqual(tool_names(bridge.request("tools/list")), ["sandbox_status"])
        self.sandbox()
        bridge.wait_list_changed()  # no sandbox_status call: the probe found the Sandbox
        self.assertEqual(tool_names(bridge.request("tools/list")), ["sandbox_status", "scene_entities", "slow_tool"])

    def test_protocol_version_negotiation(self):
        sandbox = self.sandbox()
        for requested, expected in (("2025-06-18", "2025-06-18"), ("2025-03-26", "2025-03-26"),
                                    ("2024-11-05", "2024-11-05"), ("2099-01-01", "2025-06-18")):
            bridge = self.bridge()
            self.assertEqual(bridge.request("initialize", {"protocolVersion": requested})["result"]["protocolVersion"],
                             expected)
        upstream = [m for m in sandbox.received if m.get("method") == "initialize"]
        self.assertTrue(upstream)
        self.assertTrue(all(m["params"]["protocolVersion"] == "2025-06-18" for m in upstream))


if __name__ == "__main__":
    unittest.main(verbosity=2)
