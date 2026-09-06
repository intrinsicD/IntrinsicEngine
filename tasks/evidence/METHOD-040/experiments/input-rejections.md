# Read-only input diagnosis

Independent source inspection on 2026-09-06; source hashes are in the cohort JSON.
No source geometry was changed and no invalid faces were discarded.

| Input | Condition | Disposition |
| --- | --- | --- |
| bunny10k | 6,164 vertices; 9,999 triangles; 1,113 unused vertices. Referenced surface: 5,051 vertices, one connected component, 109 boundary edges, no zero-area triangles or nonmanifold links. | The current strict feature preflight rejects isolated vertices. An explicit mapped surface extraction could be considered separately. |
| sphere | 640 vertices; 1,216 triangles; 48 zero-area triangles with coincident positions in exact decimal source coordinates. First: face 577, line 1220, indices 577/609/640. | Valid degenerate-face rejection; not a float-conversion artifact. |
| office_chair | 3,476 vertices; 3,391 faces: 3,355 quads, eleven 15-gons, one 18-gon, 24 triangles. Sixteen quads repeat vertex indices; first is line 6911, face 3256, 3115 3412 3113 3113. | OBJ importer returns InvalidFormat. Repair and triangulation would be explicit derived-input work. |

The revised runner reports the parser's actual error code. The loaded geometry
sidecar preserves importer vertex order for overlays; a source-OBJ-index fallback
cannot assume that packing/corner processing leaves every vertex slot unchanged.
