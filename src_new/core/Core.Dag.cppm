module;

#include <cstdint>
#include <span>

export module Core.Dag;

import Core.Properties;

namespace Extrinsic::Core
{
    using NodeHandle = std::uint32_t;
    using EdgeHandle = std::uint32_t;

    class Dag
    {
    public:
        Dag() = default;
        virtual ~Dag() = default;

        NodeHandle AddNode();

        EdgeHandle AddDependency(NodeHandle source, std::span<NodeHandle> reads, std::span<NodeHandle> writes);
    
    private:
        PropertySet m_Nodes;
        PropertySet m_Edges;
    };
}
