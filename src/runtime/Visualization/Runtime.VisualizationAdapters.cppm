module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.VisualizationAdapters;

import Geometry.Properties;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.StreamingExecutor;

export namespace Extrinsic::Runtime
{
    struct VisualizationAdapterBatch
    {
        std::vector<Graphics::VisualizationPropertyBufferUploadDescriptor> PropertyBuffers{};
        std::vector<std::vector<std::byte>> PropertyBufferPayloads{};
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
        std::uint64_t ColorBufferBDA{0u};
        std::uint64_t PositionBufferBDA{0u};
        std::uint64_t VectorBufferBDA{0u};
        std::string PropertyBufferSourceKey{};
        std::string PositionBufferSourceKey{};
        std::string VectorBufferSourceKey{};
        std::uint64_t DirtyStamp{0u};
        bool AutoRange{true};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        Graphics::Colormap::Type Colormap{Graphics::Colormap::Type::Viridis};
        std::uint32_t IsoValueCount{0u};
        float LineWidth{1.0f};
        glm::vec4 OverlayColor{0.0f, 0.0f, 0.0f, 1.0f};
        float VectorScale{1.0f};
        glm::vec4 VectorColor{1.0f};
        bool DepthTested{true};
        bool EmitPrincipalDirections{false};
        bool EmitPrincipalDirection1{true};
        bool EmitPrincipalDirection2{true};
        std::string PrincipalDirection1SourceName{};
        std::string PrincipalDirection2SourceName{};
        bool EmitHtexPreview{false};
        bool EmitFragmentBake{false};
        std::string SourceAttributeName{};
        Graphics::VisualizationFragmentBakeMapping FragmentBakeMapping{
            Graphics::VisualizationFragmentBakeMapping::ExistingTexcoords};
        bool MeshHasTexcoords{false};
        std::uint32_t PatchCount{0u};
        std::uint32_t FaceCount{0u};
        std::uint32_t AtlasWidth{0u};
        std::uint32_t AtlasHeight{0u};
        std::uint64_t TexcoordBufferBDA{0u};
        Assets::AssetId AtlasTextureAsset{};
        Graphics::VisualizationGeneratedTextureSemantic GeneratedTextureSemantic{
            Graphics::VisualizationGeneratedTextureSemantic::Unknown};
        std::uint64_t SourceAttributeDirtyStamp{0u};
        std::uint64_t HtexRecreatePayloadToken{0u};
    };

    struct VisualizationAdapterStats
    {
        std::uint32_t AdapterInvocationCount{0u};
        std::uint32_t PacketAppendCount{0u};
        std::uint32_t MissingSourceCount{0u};
        std::uint32_t UnsupportedSourceTypeCount{0u};
        std::uint32_t EmptySourceCount{0u};
        std::uint32_t InvalidBufferCount{0u};
        std::uint32_t InvalidResourceCount{0u};
        std::uint32_t MissingTexcoordCount{0u};
        std::uint32_t InvalidRangeCount{0u};
        std::uint32_t NonFiniteValueCount{0u};
        std::uint32_t ElementCountOverflowCount{0u};
        std::uint32_t ManualRangeCount{0u};
        std::uint32_t FlatAutoRangeExpandedCount{0u};
        std::uint32_t RobustAutoRangeClampedCount{0u};
        std::uint64_t ScalarValueScanCount{0u};
        std::uint32_t HtexRecreateScheduledCount{0u};
        StreamingTaskHandle LastHtexRecreateTask{};
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

    class KMeansLabelAdapter final : public IVisualizationAdapter
    {
    public:
        explicit KMeansLabelAdapter(Geometry::ConstPropertySet properties) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        Geometry::ConstPropertySet m_Properties{};
    };

    class VectorFieldAdapter final : public IVisualizationAdapter
    {
    public:
        explicit VectorFieldAdapter(Geometry::ConstPropertySet properties) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        Geometry::ConstPropertySet m_Properties{};
    };

    class CurvatureVisualizationAdapter final : public IVisualizationAdapter
    {
    public:
        explicit CurvatureVisualizationAdapter(Geometry::ConstPropertySet properties) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        Geometry::ConstPropertySet m_Properties{};
    };

    class IsolineAdapter final : public IVisualizationAdapter
    {
    public:
        explicit IsolineAdapter(Geometry::ConstPropertySet properties) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        Geometry::ConstPropertySet m_Properties{};
    };

    class HtexMetadataAdapter final : public IVisualizationAdapter
    {
    public:
        explicit HtexMetadataAdapter(StreamingExecutor* executor = nullptr) noexcept;

        void Append(VisualizationAdapterBatch& out,
                    const VisualizationAdapterOptions& options,
                    VisualizationAdapterStats& stats) const override;

    private:
        StreamingExecutor* m_Executor{nullptr};
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
