// Geometry format capabilities shared by geometry and assets without cross-layer types.
// Define INTRINSIC_GEOMETRY_FORMAT(Name, Canonical, ImportDomains, ExportDomains,
// BinaryImport, BinaryExport, ...aliases) before inclusion and undefine it afterwards.
// Intentionally repeatable; domain tokens bind to each consumer's local arrays.

INTRINSIC_GEOMETRY_FORMAT(OBJ, "obj", MeshOnly, MeshOnly, false, false, "obj")
INTRINSIC_GEOMETRY_FORMAT(OFF, "off", MeshOnly, MeshOnly, false, false, "off")
INTRINSIC_GEOMETRY_FORMAT(STL, "stl", MeshOnly, MeshOnly, true, true, "stl")
INTRINSIC_GEOMETRY_FORMAT(PLY, "ply", MeshAndPointCloud, MeshAndPointCloud, true, true, "ply")
INTRINSIC_GEOMETRY_FORMAT(XYZ, "xyz", PointCloudOnly, PointCloudOnly, false, false, "xyz")
INTRINSIC_GEOMETRY_FORMAT(PTS, "pts", PointCloudOnly, NoDomains, false, false, "pts")
INTRINSIC_GEOMETRY_FORMAT(PWN, "pwn", PointCloudOnly, NoDomains, false, false, "pwn")
INTRINSIC_GEOMETRY_FORMAT(CSV, "csv", PointCloudOnly, NoDomains, false, false, "csv")
INTRINSIC_GEOMETRY_FORMAT(ThreeD, "3d", PointCloudOnly, NoDomains, false, false, "3d")
INTRINSIC_GEOMETRY_FORMAT(TXT, "txt", PointCloudOnly, NoDomains, false, false, "txt")
INTRINSIC_GEOMETRY_FORMAT(XYZRGB, "xyzrgb", PointCloudOnly, NoDomains, false, false, "xyzrgb")
INTRINSIC_GEOMETRY_FORMAT(PCD, "pcd", PointCloudOnly, PointCloudOnly, true, true, "pcd")
INTRINSIC_GEOMETRY_FORMAT(TGF, "tgf", GraphOnly, GraphOnly, false, false, "tgf")
INTRINSIC_GEOMETRY_FORMAT(EdgeList, "edges", GraphOnly, GraphOnly, false, false, "edges", "edgelist")
