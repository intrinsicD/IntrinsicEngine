#!/usr/bin/env python3
"""Regression tests for the IntrinsicEngine architecture diagram skill."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
RENDERER = (
    REPO_ROOT
    / "tools"
    / "agents"
    / "skills"
    / "intrinsicengine-draw-architecture"
    / "scripts"
    / "render_architecture.py"
)


def fixture_graph() -> dict[str, object]:
    nodes = [
        {
            "id": "mod__core",
            "label": "Extrinsic.Core.Foundation",
            "kind": "module",
            "layer": "core",
            "source_file": "src/core/Core.Foundation.cppm",
            "source_location": "L3",
        },
        {
            "id": "mod__geometry",
            "label": "Extrinsic.Geometry.Mesh",
            "kind": "module",
            "layer": "geometry",
            "source_file": "src/geometry/Geometry.Mesh.cppm",
            "source_location": "L5",
        },
        {
            "id": "mod__runtime_focus",
            "label": "Extrinsic.Runtime.Controller",
            "kind": "module",
            "layer": "runtime",
            "source_file": "src/runtime/Runtime.Controller.cppm",
            "source_location": "L7",
        },
        {
            "id": "mod__runtime_consumer",
            "label": "Extrinsic.Runtime.Consumer",
            "kind": "module",
            "layer": "runtime",
            "source_file": "src/runtime/Runtime.Consumer.cppm",
            "source_location": "L11",
        },
        {
            "id": "mod__app",
            "label": "Extrinsic.App.Main",
            "kind": "module",
            "layer": "app",
            "source_file": "src/app/App.Main.cppm",
            "source_location": "L13",
        },
        {
            "id": "method__fixture",
            "label": "Fixture Method",
            "kind": "method",
            "layer": "",
            "source_file": "methods/fixture/method.yaml",
            "source_location": "L1",
        },
    ]
    links = [
        {
            "source": "mod__geometry",
            "target": "mod__core",
            "relation": "imports",
            "weight": 1.0,
            "layer_relation": "allowed",
            "source_file": "src/geometry/Geometry.Mesh.cppm",
            "source_location": "L7",
        },
        {
            "source": "mod__runtime_focus",
            "target": "mod__geometry",
            "relation": "imports",
            "weight": 2.0,
            "layer_relation": "allowed",
            "source_file": "src/runtime/Runtime.Controller.cppm",
            "source_location": "L9",
        },
        {
            "source": "mod__runtime_consumer",
            "target": "mod__runtime_focus",
            "relation": "imports",
            "weight": 1.0,
            "layer_relation": "same-layer",
            "source_file": "src/runtime/Runtime.Consumer.cppm",
            "source_location": "L13",
        },
        {
            "source": "mod__app",
            "target": "mod__runtime_focus",
            "relation": "imports",
            "weight": 1.0,
            "layer_relation": "allowed",
            "source_file": "src/app/App.Main.cppm",
            "source_location": "L15",
        },
        {
            "source": "mod__core",
            "target": "mod__app",
            "relation": "imports",
            "weight": 1.0,
            "layer_relation": "violation",
            "source_file": "src/core/Core.Foundation.cppm",
            "source_location": "L17",
        },
        {
            "source": "method__fixture",
            "target": "mod__geometry",
            "relation": "implements",
            "weight": 1.0,
            "source_file": "methods/fixture/method.yaml",
            "source_location": "L4",
        },
    ]
    return {"directed": True, "nodes": nodes, "links": links}


def run_renderer(
    graph_path: Path,
    *arguments: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(RENDERER), *arguments, "--graph", str(graph_path)],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class ArchitectureDiagramSkillTests(unittest.TestCase):
    def write_fixture(
        self, root: Path, payload: dict[str, object] | None = None
    ) -> Path:
        path = root / "graph.json"
        path.write_text(
            json.dumps(payload if payload is not None else fixture_graph(), indent=2),
            encoding="utf-8",
        )
        return path

    def test_layer_view_aggregates_cross_layer_edges_and_marks_violation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(graph_path, "layers")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("flowchart TB", result.stdout)
            self.assertIn("core<br/>1 module", result.stdout)
            self.assertIn("runtime<br/>2 modules", result.stdout)
            self.assertIn('"1 dep / 2 sites"', result.stdout)
            self.assertIn('"1 dep / 1 site / 1 violation"', result.stdout)
            self.assertIn("linkStyle", result.stdout)
            self.assertNotIn("same-layer", result.stdout)
            self.assertIn("A --> B means A imports B", result.stdout)

    def test_explicit_audit_style_preserves_default_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            implicit_layers = run_renderer(graph_path, "layers")
            explicit_layers = run_renderer(
                graph_path,
                "layers",
                "--style",
                "audit",
            )
            implicit_modules = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
            )
            explicit_modules = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--style",
                "audit",
            )
            self.assertEqual(implicit_layers.returncode, 0, implicit_layers.stderr)
            self.assertEqual(implicit_modules.returncode, 0, implicit_modules.stderr)
            self.assertEqual(implicit_layers.stdout, explicit_layers.stdout)
            self.assertEqual(implicit_modules.stdout, explicit_modules.stdout)

    def test_presentation_layer_view_embeds_layout_and_moves_counts_to_comments(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "layers",
                "--style",
                "presentation",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.startswith("---\nconfig:\n"))
            self.assertIn("  layout: elk", result.stdout)
            self.assertIn("  look: neo", result.stdout)
            self.assertIn("    curve: step", result.stdout)
            self.assertIn("cross_layer_pairs=4 visible_pairs=4", result.stdout)
            self.assertNotIn('"1 dep / 2 sites"', result.stdout)
            self.assertIn('"1 violation"', result.stdout)
            self.assertIn(
                "%% edge evidence: runtime -> geometry "
                "deps=1 sites=2 violations=0 visible=yes",
                result.stdout,
            )
            self.assertIn("classDef context", result.stdout)

    def test_focused_presentation_keeps_context_and_only_incident_edges(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "layers",
                "--style",
                "presentation",
                "--focus",
                "runtime",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("focus=runtime", result.stdout)
            self.assertIn("cross_layer_pairs=4 visible_pairs=3", result.stdout)
            self.assertIn("core<br/>1 module", result.stdout)
            self.assertIn("geometry<br/>1 module", result.stdout)
            self.assertIn("runtime<br/>2 modules", result.stdout)
            self.assertIn("app<br/>1 module", result.stdout)
            self.assertIn(
                "%% edge evidence: geometry -> core "
                "deps=1 sites=1 violations=0 visible=no",
                result.stdout,
            )
            self.assertIn(
                "%% edge evidence: core -> app deps=1 sites=1 violations=1 visible=yes",
                result.stdout,
            )
            self.assertIn('"1 violation"', result.stdout)

    def test_audit_focus_highlights_without_filtering_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "layers",
                "--focus",
                "runtime",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("style=audit focus=runtime", result.stdout)
            self.assertIn("cross_layer_pairs=4 visible_pairs=4", result.stdout)
            self.assertIn('"1 dep / 2 sites"', result.stdout)
            self.assertIn("classDef focus", result.stdout)

    def test_missing_layer_focus_fails_with_available_layers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "layers",
                "--focus",
                "renderer",
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("focus layer 'renderer' was not found", result.stderr)
            self.assertIn("available layers:", result.stderr)
            self.assertIn("runtime", result.stderr)

    def test_internal_rhi_layer_key_uses_repository_vocabulary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            payload = fixture_graph()
            nodes = payload["nodes"]
            assert isinstance(nodes, list)
            nodes[1]["layer"] = "graphics_rhi"
            graph_path = self.write_fixture(Path(tmp), payload)
            result = run_renderer(graph_path, "layers")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("graphics/rhi<br/>1 module", result.stdout)
            self.assertNotIn('["graphics_rhi<br/>', result.stdout)

    def test_repository_rhi_label_resolves_as_layer_focus(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            payload = fixture_graph()
            nodes = payload["nodes"]
            assert isinstance(nodes, list)
            nodes[1]["layer"] = "graphics_rhi"
            graph_path = self.write_fixture(Path(tmp), payload)
            result = run_renderer(
                graph_path,
                "layers",
                "--style",
                "presentation",
                "--focus",
                "graphics/rhi",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("focus=graphics/rhi", result.stdout)

    def test_dependency_view_traverses_outgoing_imports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "dependencies",
                "--radius",
                "1",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Extrinsic.Runtime.Controller", result.stdout)
            self.assertIn("Extrinsic.Geometry.Mesh", result.stdout)
            self.assertNotIn("Extrinsic.Runtime.Consumer", result.stdout)
            self.assertNotIn("Extrinsic.App.Main", result.stdout)
            self.assertIn(
                "src/runtime/Runtime.Controller.cppm:L7",
                result.stdout,
            )

    def test_dependent_view_traverses_reverse_imports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "dependents",
                "--radius",
                "1",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Extrinsic.Runtime.Consumer", result.stdout)
            self.assertIn("Extrinsic.App.Main", result.stdout)
            self.assertNotIn("Extrinsic.Geometry.Mesh", result.stdout)

    def test_module_presentation_preserves_bounded_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "both",
                "--style",
                "presentation",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.startswith("---\nconfig:\n"))
            self.assertIn(
                "style=presentation focus=Extrinsic.Runtime.Controller",
                result.stdout,
            )
            self.assertIn("Extrinsic.Geometry.Mesh", result.stdout)
            self.assertIn("Extrinsic.Runtime.Consumer", result.stdout)
            self.assertIn("Extrinsic.App.Main", result.stdout)
            self.assertIn("classDef related", result.stdout)
            self.assertIn(
                "%% source: Extrinsic.Runtime.Controller -> "
                "src/runtime/Runtime.Controller.cppm:L7",
                result.stdout,
            )

    def test_source_path_resolves_focus_module(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "src/runtime/Runtime.Controller.cppm",
                "--direction",
                "dependencies",
                "--radius",
                "0",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("focus=Extrinsic.Runtime.Controller", result.stdout)
            self.assertIn("nodes=1", result.stdout)

    def test_radius_two_reaches_transitive_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "dependencies",
                "--radius",
                "2",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Extrinsic.Core.Foundation", result.stdout)
            self.assertIn("nodes=3", result.stdout)

    def test_node_budget_fails_without_silent_truncation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "both",
                "--radius",
                "1",
                "--max-nodes",
                "3",
            )
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("view selects 4 modules", result.stderr)
            self.assertIn("exceeding --max-nodes 3", result.stderr)

    def test_hard_static_budget_routes_broader_views_to_graphify(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--max-nodes",
                "51",
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("--max-nodes must be between 1 and 50", result.stderr)
            self.assertIn("Graphify", result.stderr)

    def test_ambiguous_focus_lists_matches(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime",
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("is ambiguous", result.stderr)
            self.assertIn("Extrinsic.Runtime.Consumer", result.stderr)
            self.assertIn("Extrinsic.Runtime.Controller", result.stderr)

    def test_missing_focus_offers_nearby_module(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = self.write_fixture(Path(tmp))
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controllr",
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("was not found", result.stderr)
            self.assertIn("Extrinsic.Runtime.Controller", result.stderr)

    def test_labels_are_escaped_and_provenance_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            payload = fixture_graph()
            nodes = payload["nodes"]
            assert isinstance(nodes, list)
            nodes[0]["label"] = 'Extrinsic.Core.Quoted"Thing&'
            graph_path = self.write_fixture(Path(tmp), payload)
            result = run_renderer(
                graph_path,
                "modules",
                "--focus",
                'Extrinsic.Core.Quoted"Thing&',
                "--direction",
                "dependents",
                "--radius",
                "0",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Extrinsic.Core.Quoted&quot;Thing&amp;", result.stdout)
            self.assertIn("src/core/Core.Foundation.cppm:L3", result.stdout)

    def test_output_is_deterministic_and_can_be_written_to_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            graph_path = self.write_fixture(root)
            first = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "both",
            )
            second = run_renderer(
                graph_path,
                "modules",
                "--focus",
                "Extrinsic.Runtime.Controller",
                "--direction",
                "both",
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first.stdout, second.stdout)

            output = root / "nested" / "view.mmd"
            written = run_renderer(
                graph_path,
                "layers",
                "--output",
                str(output),
            )
            self.assertEqual(written.returncode, 0, written.stderr)
            self.assertEqual(written.stdout, "")
            expected = run_renderer(graph_path, "layers").stdout
            self.assertEqual(output.read_text(encoding="utf-8"), expected)
            self.assertIn(f"wrote {output}", written.stderr)

    def test_duplicate_json_keys_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = Path(tmp) / "graph.json"
            graph_path.write_text(
                '{"nodes": [], "nodes": [], "links": []}',
                encoding="utf-8",
            )
            result = run_renderer(graph_path, "layers")
            self.assertEqual(result.returncode, 2)
            self.assertIn("duplicate key 'nodes'", result.stderr)

    def test_missing_graph_has_build_instruction(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            graph_path = Path(tmp) / "missing.json"
            result = run_renderer(graph_path, "layers")
            self.assertEqual(result.returncode, 2)
            self.assertIn("graph not found", result.stderr)
            self.assertIn(
                "python3 tools/repo/build_knowledge_graph.py",
                result.stderr,
            )


if __name__ == "__main__":
    unittest.main()
