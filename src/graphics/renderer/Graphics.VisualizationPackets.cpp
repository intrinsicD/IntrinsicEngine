module;

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include <glm/glm.hpp>

module Extrinsic.Graphics.VisualizationPackets;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4& value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
        }

        [[nodiscard]] bool ValidRange(const float minValue, const float maxValue) noexcept
        {
            return IsFinite(minValue) && IsFinite(maxValue) && minValue < maxValue;
        }

        [[nodiscard]] bool SupportedColormap(const Colormap::Type type) noexcept
        {
            return static_cast<std::uint8_t>(type) < static_cast<std::uint8_t>(Colormap::Type::Count);
        }

        void CountDomain(const VisualizationAttributeDomain domain,
                         const VisualizationPacketBatch& batch,
                         VisualizationDiagnostics& diagnostics) noexcept
        {
            if (batch.EnforceDomain && domain != batch.ExpectedDomain)
            {
                ++diagnostics.DomainMismatchCount;
            }
        }

        void AcceptOrError(const bool valid, VisualizationDiagnostics& diagnostics) noexcept
        {
            if (valid)
            {
                ++diagnostics.AcceptedPacketCount;
            }
            else
            {
                diagnostics.HasErrors = true;
            }
        }

        void CountTexcoordProvenance(const VisualizationTexcoordProvenance provenance,
                                     VisualizationOverlaySummary& summary) noexcept
        {
            switch (provenance)
            {
            case VisualizationTexcoordProvenance::Authored:
                ++summary.AuthoredUvBakeAtlasDescriptorCount;
                break;
            case VisualizationTexcoordProvenance::GeneratedAtlas:
                ++summary.GeneratedUvBakeAtlasDescriptorCount;
                break;
            case VisualizationTexcoordProvenance::RuntimeResolved:
                ++summary.RuntimeResolvedUvBakeAtlasDescriptorCount;
                break;
            case VisualizationTexcoordProvenance::Unknown:
                break;
            }
        }

        [[nodiscard]] bool SupportedGeneratedTextureSemantic(
            const VisualizationGeneratedTextureSemantic semantic) noexcept
        {
            return static_cast<std::uint8_t>(semantic) <
                   static_cast<std::uint8_t>(
                       VisualizationGeneratedTextureSemantic::Count);
        }

        void CountGeneratedTextureSemantic(
            const VisualizationGeneratedTextureSemantic semantic,
            VisualizationOverlaySummary& summary) noexcept
        {
            switch (semantic)
            {
            case VisualizationGeneratedTextureSemantic::ScalarAttribute:
                ++summary.ScalarBakeTextureAssetDescriptorCount;
                break;
            case VisualizationGeneratedTextureSemantic::LabelAttribute:
                ++summary.LabelBakeTextureAssetDescriptorCount;
                break;
            case VisualizationGeneratedTextureSemantic::Vector2Attribute:
            case VisualizationGeneratedTextureSemantic::Vector3Attribute:
            case VisualizationGeneratedTextureSemantic::Vector4Attribute:
                ++summary.VectorBakeTextureAssetDescriptorCount;
                break;
            case VisualizationGeneratedTextureSemantic::PbrAlbedo:
            case VisualizationGeneratedTextureSemantic::PbrNormal:
            case VisualizationGeneratedTextureSemantic::PbrMetallicRoughness:
            case VisualizationGeneratedTextureSemantic::PbrEmissive:
                ++summary.PbrBakeTextureAssetDescriptorCount;
                break;
            case VisualizationGeneratedTextureSemantic::Displacement:
                ++summary.DisplacementBakeTextureAssetDescriptorCount;
                break;
            case VisualizationGeneratedTextureSemantic::Unknown:
            case VisualizationGeneratedTextureSemantic::Count:
                break;
            }
        }

        template <typename T>
        [[nodiscard]] bool IsFinitePayload(const std::span<const std::byte> bytes,
                                           const std::uint64_t componentCount) noexcept
        {
            const std::uint64_t expectedBytes =
                componentCount * sizeof(T);
            if (bytes.size() != expectedBytes)
            {
                return false;
            }

            for (std::uint64_t i = 0u; i < componentCount; ++i)
            {
                T value{};
                std::memcpy(&value,
                            bytes.data() + i * sizeof(T),
                            sizeof(T));
                if (!std::isfinite(static_cast<double>(value)))
                {
                    return false;
                }
            }
            return true;
        }
    }

    std::uint32_t ExpectedVisualizationValueStride(
        const VisualizationValueType type) noexcept
    {
        switch (type)
        {
        case VisualizationValueType::ScalarFloat:
            return sizeof(float);
        case VisualizationValueType::Rgba8:
            return 4u;
        case VisualizationValueType::VectorFloat3:
            return sizeof(float) * 3u;
        case VisualizationValueType::LabelUint32:
            return sizeof(std::uint32_t);
        case VisualizationValueType::ScalarDouble:
            return sizeof(double);
        case VisualizationValueType::RgbaFloat4:
            return sizeof(float) * 4u;
        case VisualizationValueType::Count:
            break;
        }
        return 0u;
    }

    bool ValidateVisualizationPropertyBufferUploadDescriptor(
        const VisualizationPropertyBufferUploadDescriptor& descriptor,
        VisualizationPropertyBufferDiagnostics& diagnostics) noexcept
    {
        ++diagnostics.InputBufferCount;

        bool valid = true;
        if (descriptor.SourceKey.empty())
        {
            ++diagnostics.InvalidSourceKeyCount;
            valid = false;
        }

        const std::uint32_t expectedStride =
            ExpectedVisualizationValueStride(descriptor.ValueType);
        if (expectedStride == 0u)
        {
            ++diagnostics.UnsupportedTypeCount;
            valid = false;
        }
        else if (descriptor.StrideBytes != expectedStride)
        {
            ++diagnostics.InvalidStrideCount;
            valid = false;
        }
        if (descriptor.ElementCount == 0u)
        {
            ++diagnostics.ZeroElementCount;
            valid = false;
        }

        const std::uint64_t expectedByteSize =
            static_cast<std::uint64_t>(descriptor.ElementCount) *
            static_cast<std::uint64_t>(descriptor.StrideBytes);
        if (expectedByteSize > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
            descriptor.Bytes.size() != expectedByteSize)
        {
            ++diagnostics.InvalidByteSizeCount;
            valid = false;
        }

        if (valid)
        {
            bool finite = true;
            switch (descriptor.ValueType)
            {
            case VisualizationValueType::ScalarFloat:
                finite = IsFinitePayload<float>(
                    descriptor.Bytes, descriptor.ElementCount);
                break;
            case VisualizationValueType::VectorFloat3:
                finite = IsFinitePayload<float>(
                    descriptor.Bytes,
                    static_cast<std::uint64_t>(descriptor.ElementCount) * 3u);
                break;
            case VisualizationValueType::ScalarDouble:
                finite = IsFinitePayload<double>(
                    descriptor.Bytes, descriptor.ElementCount);
                break;
            case VisualizationValueType::RgbaFloat4:
                finite = IsFinitePayload<float>(
                    descriptor.Bytes,
                    static_cast<std::uint64_t>(descriptor.ElementCount) * 4u);
                break;
            case VisualizationValueType::Rgba8:
            case VisualizationValueType::LabelUint32:
                break;
            case VisualizationValueType::Count:
                finite = false;
                break;
            }

            if (!finite)
            {
                ++diagnostics.NonFiniteValueCount;
                valid = false;
            }
        }

        if (valid)
        {
            ++diagnostics.AcceptedBufferCount;
        }
        else
        {
            diagnostics.HasErrors = true;
        }
        return valid;
    }

    VisualizationPropertyBufferDiagnostics ValidateVisualizationPropertyBufferUploads(
        const std::span<const VisualizationPropertyBufferUploadDescriptor> descriptors) noexcept
    {
        VisualizationPropertyBufferDiagnostics diagnostics{};
        for (const VisualizationPropertyBufferUploadDescriptor& descriptor : descriptors)
        {
            (void)ValidateVisualizationPropertyBufferUploadDescriptor(descriptor, diagnostics);
        }
        return diagnostics;
    }

    VisualizationDiagnostics ValidateVisualizationPackets(const VisualizationPacketBatch& batch) noexcept
    {
        VisualizationDiagnostics diagnostics{};
        diagnostics.InputPacketCount = static_cast<std::uint32_t>(batch.AttributeBuffers.size() +
                                                                  batch.Scalars.size() +
                                                                  batch.Colors.size() +
                                                                  batch.VectorFields.size() +
                                                                  batch.Isolines.size() +
                                                                  batch.HtexAtlases.size() +
                                                                  batch.FragmentBakeAtlases.size());

        for (const VisualizationAttributeBufferPacket& packet : batch.AttributeBuffers)
        {
            CountDomain(packet.Domain, batch, diagnostics);
            const bool valid = !packet.Name.empty() && packet.ElementCount > 0u && packet.BufferBDA != 0u;
            if (!valid)
            {
                ++diagnostics.InvalidResourceCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const ScalarAttributePacket& packet : batch.Scalars)
        {
            CountDomain(packet.Domain, batch, diagnostics);
            bool valid = !packet.Name.empty() && packet.ElementCount > 0u && packet.ScalarBufferBDA != 0u;
            if (!ValidRange(packet.RangeMin, packet.RangeMax))
            {
                valid = false;
                ++diagnostics.InvalidRangeCount;
            }
            if (!SupportedColormap(packet.Colormap))
            {
                valid = false;
                ++diagnostics.UnsupportedColormapCount;
            }
            if (packet.Name.empty() || packet.ElementCount == 0u || packet.ScalarBufferBDA == 0u)
            {
                ++diagnostics.MissingAttributeCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const ColorAttributePacket& packet : batch.Colors)
        {
            CountDomain(packet.Domain, batch, diagnostics);
            const bool valid = !packet.Name.empty() && packet.ElementCount > 0u && packet.ColorBufferBDA != 0u;
            if (!valid)
            {
                ++diagnostics.MissingAttributeCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const VectorFieldOverlayPacket& packet : batch.VectorFields)
        {
            CountDomain(packet.Domain, batch, diagnostics);
            bool valid = !packet.Name.empty() && packet.ElementCount > 0u &&
                         packet.PositionBufferBDA != 0u && packet.VectorBufferBDA != 0u &&
                         IsFinite(packet.Scale) && packet.Scale > 0.f && IsFinite(packet.Color);
            if (packet.Name.empty() || packet.ElementCount == 0u || packet.PositionBufferBDA == 0u || packet.VectorBufferBDA == 0u)
            {
                ++diagnostics.MissingAttributeCount;
            }
            if (!IsFinite(packet.Scale) || packet.Scale <= 0.f || !IsFinite(packet.Color))
            {
                ++diagnostics.InvalidRangeCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const IsolineOverlayPacket& packet : batch.Isolines)
        {
            CountDomain(packet.Domain, batch, diagnostics);
            bool valid = !packet.SourceScalarName.empty() && packet.IsoValueCount > 0u &&
                         ValidRange(packet.RangeMin, packet.RangeMax) &&
                         IsFinite(packet.LineWidth) && packet.LineWidth > 0.f && IsFinite(packet.Color);
            if (packet.SourceScalarName.empty() || packet.IsoValueCount == 0u)
            {
                ++diagnostics.MissingAttributeCount;
            }
            if (!ValidRange(packet.RangeMin, packet.RangeMax) || !IsFinite(packet.LineWidth) || packet.LineWidth <= 0.f || !IsFinite(packet.Color))
            {
                ++diagnostics.InvalidRangeCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const HtexPatchPreviewAtlasPacket& packet : batch.HtexAtlases)
        {
            const bool valid = !packet.Name.empty() && packet.PatchCount > 0u && packet.AtlasWidth > 0u && packet.AtlasHeight > 0u;
            if (!valid)
            {
                ++diagnostics.InvalidResourceCount;
            }
            else
            {
                ++diagnostics.TextureResidencyDeferredCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        for (const FragmentBakeAtlasPacket& packet : batch.FragmentBakeAtlases)
        {
            bool valid = !packet.Name.empty() && !packet.SourceAttributeName.empty() &&
                         packet.FaceCount > 0u && packet.AtlasWidth > 0u && packet.AtlasHeight > 0u;
            if (!valid)
            {
                ++diagnostics.InvalidResourceCount;
            }
            if (!SupportedGeneratedTextureSemantic(
                    packet.GeneratedTextureSemantic))
            {
                valid = false;
                ++diagnostics.InvalidResourceCount;
            }

            switch (packet.Mapping)
            {
            case VisualizationFragmentBakeMapping::ExistingTexcoords:
                if (!packet.MeshHasTexcoords || packet.TexcoordBufferBDA == 0u)
                {
                    valid = false;
                    ++diagnostics.MissingTexcoordCount;
                }
                break;
            case VisualizationFragmentBakeMapping::ExistingHtex:
                break;
            case VisualizationFragmentBakeMapping::RecreateHtex:
                ++diagnostics.HtexRecreateRequestCount;
                break;
            }

            if (valid)
            {
                ++diagnostics.TextureResidencyDeferredCount;
            }
            AcceptOrError(valid, diagnostics);
        }

        diagnostics.HasErrors = diagnostics.HasErrors || diagnostics.DomainMismatchCount > 0u;
        return diagnostics;
    }

    VisualizationOverlaySummary BuildVisualizationOverlaySummary(const VisualizationPacketBatch& batch) noexcept
    {
        VisualizationOverlaySummary summary{};
        for (const VectorFieldOverlayPacket& packet : batch.VectorFields)
        {
            if (!packet.Name.empty() && packet.ElementCount > 0u)
            {
                ++summary.VectorFieldCount;
                summary.VectorGlyphCount += packet.ElementCount;
            }
        }
        for (const IsolineOverlayPacket& packet : batch.Isolines)
        {
            if (!packet.SourceScalarName.empty() && packet.IsoValueCount > 0u)
            {
                ++summary.IsolineLayerCount;
                summary.IsolineValueCount += packet.IsoValueCount;
            }
        }
        for (const HtexPatchPreviewAtlasPacket& packet : batch.HtexAtlases)
        {
            if (!packet.Name.empty() && packet.PatchCount > 0u && packet.AtlasWidth > 0u && packet.AtlasHeight > 0u)
            {
                ++summary.HtexAtlasDescriptorCount;
                summary.RequiresTextureResidency = true;
            }
        }
        for (const FragmentBakeAtlasPacket& packet : batch.FragmentBakeAtlases)
        {
            const bool validShape = !packet.Name.empty() && !packet.SourceAttributeName.empty() &&
                                    packet.FaceCount > 0u && packet.AtlasWidth > 0u && packet.AtlasHeight > 0u;
            if (!validShape)
            {
                continue;
            }
            if (packet.AtlasTextureAsset.IsValid())
            {
                ++summary.FragmentBakeTextureAssetDescriptorCount;
                CountGeneratedTextureSemantic(
                    packet.GeneratedTextureSemantic,
                    summary);
            }

            switch (packet.Mapping)
            {
            case VisualizationFragmentBakeMapping::ExistingTexcoords:
                if (packet.MeshHasTexcoords && packet.TexcoordBufferBDA != 0u)
                {
                    ++summary.UvBakeAtlasDescriptorCount;
                    CountTexcoordProvenance(packet.TexcoordProvenance, summary);
                    summary.RequiresTextureResidency = true;
                }
                break;
            case VisualizationFragmentBakeMapping::ExistingHtex:
                ++summary.HtexBakeAtlasDescriptorCount;
                summary.RequiresTextureResidency = true;
                break;
            case VisualizationFragmentBakeMapping::RecreateHtex:
                ++summary.HtexBakeAtlasDescriptorCount;
                ++summary.HtexRecreateRequestCount;
                summary.RequiresTextureResidency = true;
                break;
            }
        }
        return summary;
    }
}
