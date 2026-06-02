module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module Extrinsic.Runtime.VisualizationAdapters;

import Geometry.Properties;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;

export namespace Extrinsic::Runtime
{
    struct VisualizationAdapterBatch
    {
        std::vector<Graphics::VisualizationAttributeBufferPacket> AttributeBuffers{};
        std::vector<Graphics::ScalarAttributePacket> Scalars{};
        std::vector<Graphics::ColorAttributePacket> Colors{};
        std::vector<Graphics::VectorFieldOverlayPacket> VectorFields{};
        std::vector<Graphics::IsolineOverlayPacket> Isolines{};
        std::vector<Graphics::HtexPatchPreviewAtlasPacket> HtexAtlases{};
        std::vector<Graphics::FragmentBakeAtlasPacket> FragmentBakeAtlases{};

        void Clear() noexcept;

        [[nodiscard]] Graphics::VisualizationPacketBatch AsPacketBatch(
            bool enforceDomain = false,
            Graphics::VisualizationAttributeDomain expectedDomain =
                Graphics::VisualizationAttributeDomain::Vertex) const noexcept;
    };

    struct VisualizationAdapterOptions
    {
        std::string SourceName{};
        std::string OutputName{};
        Graphics::VisualizationAttributeDomain Domain{
            Graphics::VisualizationAttributeDomain::Vertex};
        std::uint64_t BufferBDA{0u};
        bool AutoRange{true};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        Graphics::Colormap::Type Colormap{Graphics::Colormap::Type::Viridis};
    };

    struct VisualizationAdapterStats
    {
        std::uint32_t AdapterInvocationCount{0u};
        std::uint32_t PacketAppendCount{0u};
        std::uint32_t MissingSourceCount{0u};
        std::uint32_t UnsupportedSourceTypeCount{0u};
        std::uint32_t EmptySourceCount{0u};
        std::uint32_t InvalidBufferCount{0u};
        std::uint32_t InvalidRangeCount{0u};
        std::uint32_t NonFiniteValueCount{0u};
        std::uint32_t ElementCountOverflowCount{0u};
        std::uint32_t ManualRangeCount{0u};
        std::uint32_t FlatAutoRangeExpandedCount{0u};
    };

    class IVisualizationAdapter
    {
    public:
        virtual ~IVisualizationAdapter() = default;

        virtual void Append(VisualizationAdapterBatch& out,
                            const VisualizationAdapterOptions& options,
                            VisualizationAdapterStats& stats) const = 0;
    };

    class PropertyScalarAdapter final : public IVisualizationAdapter
    {
    public:
        explicit PropertyScalarAdapter(Geometry::ConstPropertySet properties) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        Geometry::ConstPropertySet m_Properties{};
    };

    class VisualizationAdapterRegistry
    {
    public:
        using Key = std::uint64_t;

        void Register(Key key, const IVisualizationAdapter& adapter);
        bool Unregister(Key key) noexcept;
        [[nodiscard]] const IVisualizationAdapter* Find(Key key) const noexcept;
        [[nodiscard]] bool Contains(Key key) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        void Clear() noexcept;

    private:
        std::unordered_map<Key, const IVisualizationAdapter*> m_Adapters{};
    };
}
