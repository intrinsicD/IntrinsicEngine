module;

#include <cmath>
#include <cstdint>

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
    }

    VisualizationDiagnostics ValidateVisualizationPackets(const VisualizationPacketBatch& batch) noexcept
    {
        VisualizationDiagnostics diagnostics{};
        diagnostics.InputPacketCount = static_cast<std::uint32_t>(batch.AttributeBuffers.size() +
                                                                  batch.Scalars.size() +
                                                                  batch.Colors.size() +
                                                                  batch.VectorFields.size() +
                                                                  batch.Isolines.size() +
                                                                  batch.HtexAtlases.size());

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
        return summary;
    }
}


