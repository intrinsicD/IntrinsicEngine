// Defines property-backed directed topology and a deterministic compiled DAG
// so task and render graphs can compose their domain-specific behavior.
module;

#include <concepts>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Core.Dag;

export import Core.Properties;

export namespace Extrinsic::Core
{
    struct DagNodeTag;
    struct DagEdgeTag;

    // Clear starts a new epoch; handles from prior epochs fail Dag::IsValid.
    using DagEpoch = std::uint64_t;

    template <typename Tag>
    struct DagHandle
    {
        static constexpr PropertyIndex InvalidIndex =
            std::numeric_limits<PropertyIndex>::max();

        PropertyIndex Index{InvalidIndex};
        DagEpoch Epoch{};

        auto operator<=>(const DagHandle&) const = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Index != InvalidIndex && Epoch != 0u;
        }
    };

    using NodeHandle = DagHandle<DagNodeTag>;
    using EdgeHandle = DagHandle<DagEdgeTag>;
    using DagRevision = std::uint64_t;

    struct EdgeEndpoints
    {
        NodeHandle Before{};
        NodeHandle After{};
    };

    enum class DagErrorCode : std::uint8_t
    {
        InvalidNode,
        SelfEdge,
        CapacityExceeded,
        CycleDetected,
    };

    struct DagDiagnostic
    {
        DagErrorCode Code{DagErrorCode::InvalidNode};
        NodeHandle Node{};

        // For CycleDetected, the first node is repeated at the end and
        // CycleEdges[i] connects Cycle[i] to Cycle[i + 1].
        std::vector<NodeHandle> Cycle{};
        std::vector<EdgeHandle> CycleEdges{};
    };

    template <typename T>
    using DagResult = std::expected<T, DagDiagnostic>;

    struct CompiledDag
    {
        DagRevision SourceRevision{};
        std::uint32_t LayerCount{};
        std::vector<NodeHandle> TopologicalOrder{};

        // Indexed by NodeHandle::Index; roots are layer zero and every other
        // node is one layer after its deepest predecessor.
        std::vector<std::uint32_t> LayerByNode{};
    };

    // Mutation, compilation, and borrowed views require external synchronization.
    class Dag final
    {
    public:
        Dag() = default;

        [[nodiscard]] DagResult<NodeHandle> AddNode();

        // Invalid nodes and self edges fail immediately. Other cycles are
        // diagnosed by Compile(); parallel edges remain distinct.
        [[nodiscard]] DagResult<EdgeHandle>
        AddEdge(NodeHandle before, NodeHandle after);

        void ReserveNodes(std::size_t count);
        void ReserveEdges(std::size_t count);
        void Clear();

        [[nodiscard]] std::size_t NodeCount() const noexcept;
        [[nodiscard]] std::size_t EdgeCount() const noexcept;

        // Advances only when logical topology changes, not for property edits.
        [[nodiscard]] DagRevision TopologyRevision() const noexcept;

        [[nodiscard]] bool IsValid(NodeHandle node) const noexcept;
        [[nodiscard]] bool IsValid(EdgeHandle edge) const noexcept;
        [[nodiscard]] std::optional<EdgeEndpoints> Endpoints(EdgeHandle edge) const noexcept;

        // Borrowed adjacency spans are invalidated by topology mutation.
        [[nodiscard]] std::optional<std::span<const EdgeHandle>>
        IncomingEdges(NodeHandle node) const noexcept;
        [[nodiscard]] std::optional<std::span<const EdgeHandle>>
        OutgoingEdges(NodeHandle node) const noexcept;

        // Equally ready nodes are ordered by NodeHandle::Index.
        [[nodiscard]] DagResult<CompiledDag> Compile() const;

        template <PropertyValue T>
        [[nodiscard]] Property<T>
        AddNodeProperty(std::string name, T defaultValue)
        {
            return m_Nodes.Add<T>(std::move(name), std::move(defaultValue));
        }

        template <PropertyValue T>
            requires std::default_initializable<T>
        [[nodiscard]] Property<T> AddNodeProperty(std::string name)
        {
            return m_Nodes.Add<T>(std::move(name));
        }

        template <PropertyValue T>
        [[nodiscard]] Property<T> GetNodeProperty(std::string_view name)
        {
            return m_Nodes.Get<T>(name);
        }

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> GetNodeProperty(std::string_view name) const
        {
            return m_Nodes.Get<T>(name);
        }

        template <PropertyValue T>
        [[nodiscard]] Property<T> AddEdgeProperty(std::string name, T defaultValue)
        {
            return m_Edges.Add<T>(std::move(name), std::move(defaultValue));
        }

        template <PropertyValue T>
            requires std::default_initializable<T>
        [[nodiscard]] Property<T> AddEdgeProperty(std::string name)
        {
            return m_Edges.Add<T>(std::move(name));
        }

        template <PropertyValue T>
        [[nodiscard]] Property<T> GetEdgeProperty(std::string_view name)
        {
            return m_Edges.Get<T>(name);
        }

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> GetEdgeProperty(std::string_view name) const
        {
            return m_Edges.Get<T>(name);
        }

        [[nodiscard]] ConstPropertySet NodeProperties() const noexcept
        {
            return ConstPropertySet{m_Nodes};
        }

        [[nodiscard]] ConstPropertySet EdgeProperties() const noexcept
        {
            return ConstPropertySet{m_Edges};
        }

    private:
        PropertySet m_Nodes{};
        PropertySet m_Edges{};
        std::vector<EdgeEndpoints> m_Endpoints{};
        std::vector<std::vector<EdgeHandle>> m_IncomingEdges{};
        std::vector<std::vector<EdgeHandle>> m_OutgoingEdges{};
        DagEpoch m_HandleEpoch{1u};
        DagRevision m_TopologyRevision{};
    };
}
