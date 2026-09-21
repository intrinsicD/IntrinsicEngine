// Shared geometry fixtures compiled once for geometry test executables.
// Include after importing Geometry.HalfedgeMesh (or the Geometry umbrella).
#pragma once

#include <cmath>
#include <vector>
#include <glm/glm.hpp>

// Equilateral XY triangle with unit edges and counterclockwise winding.
Geometry::HalfedgeMesh::Mesh MakeSingleTriangle();

// Unit XY square split along v0-v2 into two counterclockwise triangles.
Geometry::HalfedgeMesh::Mesh MakeTwoTriangleSquare();

// Closed regular tetrahedron at the four alternating corners of [-1,1]^3.
Geometry::HalfedgeMesh::Mesh MakeTetrahedron();

// Side-two equilateral XY triangle subdivided into four; all six vertices are on the boundary.
Geometry::HalfedgeMesh::Mesh MakeSubdividedTriangle();

// Closed regular icosahedron with vertices on the unit sphere.
Geometry::HalfedgeMesh::Mesh MakeIcosahedron();

// Two coplanar unit quads sharing one edge; six vertices.
Geometry::HalfedgeMesh::Mesh MakeQuadPair();

// Closed triangulated cube with half-extent h and outward winding.
Geometry::HalfedgeMesh::Mesh MakeCube(float h = 1.0f, glm::vec3 center = glm::vec3(0.0f));

// Genus-one 4x4 torus with one deleted triangle: one boundary loop, Euler characteristic -1.
Geometry::HalfedgeMesh::Mesh MakePuncturedTorus();

// Unit-leg right triangle in the XY plane with counterclockwise winding.
Geometry::HalfedgeMesh::Mesh MakeRightTriangle();

// Unit XY square split along (1,0)-(0,1); vertex order differs from MakeTwoTriangleSquare.
Geometry::HalfedgeMesh::Mesh MakeTwoTriangleDiamond();

// Unit XY quad with counterclockwise winding.
Geometry::HalfedgeMesh::Mesh MakeSingleQuad();

// Two rows of columns+1 vertices; two counterclockwise triangles per unit column.
Geometry::HalfedgeMesh::Mesh MakeTriangleStrip(int columns = 3);

// Two rows of columns+1 vertices; one counterclockwise quad per unit column.
Geometry::HalfedgeMesh::Mesh MakeQuadStrip(int columns = 3);

// Same fixture as MakeSingleTriangle.
Geometry::HalfedgeMesh::Mesh MakeEquilateralTriangle();

// Same fixture as MakeTwoTriangleSquare.
Geometry::HalfedgeMesh::Mesh MakeTwoTriangles();

// A closed tetrahedron and a separate triangle: one boundary, two components.
Geometry::HalfedgeMesh::Mesh MakeDiskAndClosedComponent();

// Two triangles sharing only their central vertex.
Geometry::HalfedgeMesh::Mesh MakeBowtieTriangles();
