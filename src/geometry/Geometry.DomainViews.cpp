module;

module Geometry.DomainViews;

namespace Geometry::DomainViews
{
    Graph::Graph BorrowMeshAsGraphReadOnly(const HalfedgeMesh::Mesh& mesh)
    {
        // `Graph::Graph(PropertySet&, ...)` is the only borrow constructor and
        // it must accept mutable references because the returned graph supports
        // in-place writes (vertex positions, etc.). The factory takes a const
        // reference to encode the no-topology-mutation contract at the public
        // surface; see the .cppm doc for the full safety statement.
        auto& mut = const_cast<HalfedgeMesh::Mesh&>(mesh);
        return Graph::Graph(mut.VertexProperties(),
                            mut.HalfedgeProperties(),
                            mut.EdgeProperties(),
                            mut.DeletedVertexCount(),
                            mut.DeletedEdgeCount());
    }
}
