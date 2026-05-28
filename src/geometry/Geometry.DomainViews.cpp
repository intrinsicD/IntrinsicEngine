module;

module Geometry.DomainViews;

namespace Geometry::DomainViews
{
    Graph::Graph BorrowMeshAsGraph(HalfedgeMesh::Mesh& mesh)
    {
        return Graph::Graph(mesh.VertexProperties(),
                            mesh.HalfedgeProperties(),
                            mesh.EdgeProperties(),
                            mesh.DeletedVertexCount(),
                            mesh.DeletedEdgeCount());
    }
}
