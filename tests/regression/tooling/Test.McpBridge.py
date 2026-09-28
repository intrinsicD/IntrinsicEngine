#!/usr/bin/env python3
"""tools/agents/mcp_bridge.py against a fake Sandbox socket (PROC-035)."""

import importlib.util
import io
import json
import os
import socket
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("mcp_bridge", ROOT / "tools/agents/mcp_bridge.py")
bridge_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge_module)


class FakeSandbox:
    """Answers initialize, tools/list and tools/call like the Sandbox's agent server."""

    def __init__(self, path: str) -> None:
        self.path = path
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.calls = []
        self.connection = None
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self) -> None:
        try:
            connection, _ = self.server.accept()
        except OSError:
            return
        self.connection = connection
        with connection, connection.makefile("r") as reader:
            for line in reader:
                message = json.loads(line)
                if "id" not in message:
                    continue
                method = message["method"]
                self.calls.append(method)
                if method == "initialize":
                    result = {"protocolVersion": "2025-06-18", "capabilities": {"tools": {}},
                              "serverInfo": {"name": "intrinsic-sandbox"}}
                elif method == "tools/list":
                    result = {"tools": [{"name": "scene_entities", "inputSchema": {"type": "object"}}]}
                elif method == "tools/call":
                    result = {"content": [{"type": "text", "text": json.dumps(message["params"])}], "isError": False}
                else:
                    result = {}
                connection.sendall((json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result}) + "\n").encode())

    def close(self) -> None:
        self.server.close()

    def stop(self) -> None:
        """Like a Sandbox exit: the listener and the client's connection go away."""
        self.server.close()
        if self.connection is not None:
            self.connection.shutdown(socket.SHUT_RDWR)
        self.thread.join(timeout=5)
        os.unlink(self.path)


class McpBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.directory.name, "sandbox.sock")
        self.out = io.StringIO()
        self.bridge = bridge_module.Bridge(bridge_module.Engine(self.path, 5.0), self.out)

    def tearDown(self) -> None:
        self.bridge.engine.close()
        self.directory.cleanup()

    def call(self, method, params=None, message_id=1):
        return self.bridge.handle({"jsonrpc": "2.0", "id": message_id, "method": method, "params": params or {}})

    def written(self):
        lines = [json.loads(line) for line in self.out.getvalue().splitlines()]
        self.out.seek(0)
        self.out.truncate()
        return lines

    def test_starts_without_a_sandbox_and_connects_later(self):
        init = self.call("initialize", {"protocolVersion": "2025-06-18"})
        self.assertTrue(init["result"]["capabilities"]["tools"]["listChanged"])
        tools = self.call("tools/list")["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], ["sandbox_status"])
        failed = self.call("tools/call", {"name": "scene_entities", "arguments": {}})
        self.assertTrue(failed["result"]["isError"])
        self.assertIn("--agent-socket", failed["result"]["content"][0]["text"])

        sandbox = FakeSandbox(self.path)
        try:
            status = self.call("tools/call", {"name": "sandbox_status", "arguments": {}})
            self.assertTrue(json.loads(status["result"]["content"][0]["text"])["connected"])
            self.assertIn({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"}, self.written())
            tools = self.call("tools/list")["result"]["tools"]
            self.assertEqual([tool["name"] for tool in tools], ["sandbox_status", "scene_entities"])
            reply = self.call("tools/call", {"name": "scene_entities", "arguments": {"x": 1}}, message_id="client-7")
            self.assertEqual(reply["id"], "client-7", "the client's id is restored")
            self.assertEqual(json.loads(reply["result"]["content"][0]["text"])["arguments"], {"x": 1})
            self.assertEqual(sandbox.calls[:2], ["initialize", "tools/list"])
        finally:
            sandbox.close()

    def test_reports_a_lost_sandbox_and_other_methods(self):
        sandbox = FakeSandbox(self.path)
        self.call("initialize")
        self.assertEqual(len(self.call("tools/list")["result"]["tools"]), 2)
        self.bridge.engine.close("simulated crash")
        sandbox.close()
        os.unlink(self.path)
        lost = self.call("tools/call", {"name": "scene_entities"})
        self.assertTrue(lost["result"]["isError"])
        self.assertIn({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"}, self.written())
        self.assertEqual(self.call("ping")["result"], {})
        self.assertEqual(self.call("resources/list")["error"]["code"], -32601)
        self.assertIsNone(self.bridge.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}))

    def test_a_restarted_sandbox_is_reconnected_and_refreshes_the_tools(self):
        first = FakeSandbox(self.path)
        self.call("initialize")
        self.call("tools/list")
        first.stop()
        second = FakeSandbox(self.path)
        try:
            self.written()
            status = self.call("tools/call", {"name": "sandbox_status", "arguments": {}})
            self.assertTrue(json.loads(status["result"]["content"][0]["text"])["connected"],
                            "sandbox_status probes the stale connection and reconnects")
            self.assertIn({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"}, self.written(),
                          "the new Sandbox's tools are announced")
            reply = self.call("tools/call", {"name": "scene_entities", "arguments": {}})
            self.assertFalse(reply["result"]["isError"])
            self.assertIn("tools/call", second.calls)
        finally:
            second.close()

    def test_a_call_that_never_left_is_retried_on_a_new_connection(self):
        first = FakeSandbox(self.path)
        self.call("initialize")
        first.stop()
        second = FakeSandbox(self.path)
        try:
            # The bridge still holds the dead connection; the send fails, so the call is retried.
            reply = None
            for _ in range(3):  # the first send into a reset socket may still be accepted by the kernel
                reply = self.call("tools/call", {"name": "scene_entities", "arguments": {"n": 2}})
                if not reply["result"]["isError"]:
                    break
            self.assertFalse(reply["result"]["isError"], reply)
            self.assertIn("tools/call", second.calls)
        finally:
            second.close()

    def test_endpoints_are_unix_socket_paths_only(self):
        # A host:port string is treated as a (missing) socket path, never as a network address.
        engine = bridge_module.Engine("127.0.0.1:8080", 1.0)
        self.assertFalse(engine.connect())
        self.assertIn("127.0.0.1:8080", engine.last_error)


if __name__ == "__main__":
    unittest.main(verbosity=2)
