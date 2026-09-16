module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <variant>
#include <glm/glm.hpp>

module Extrinsic.Graphics.VisualizationSyncSystem;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Bindless;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;

// ============================================================
// VisualizationSyncSystem — implementation
// ============================================================

namespace Extrinsic::Graphics
{
    // ----------------------------------------------------------------
    // ColorSourceMode constants — stored in RHI::GpuEntityConfig.
    // ----------------------------------------------------------------
    static constexpr std::uint32_t kMode_Material       = 0u;
    static constexpr std::uint32_t kMode_UniformColor   = 1u;
    static constexpr std::uint32_t kMode_ScalarField    = 2u;
    static constexpr std::uint32_t kMode_PerElementRgba = 3u;

    // ----------------------------------------------------------------
    // Impl
    // ----------------------------------------------------------------
    struct VisualizationSyncSystem::Impl
    {
        RHI::IDevice*   Device  = nullptr;
        bool            Initialized = false;

        static std::uint32_t ToVisDomain(Components::VisualizationConfig::Domain d) noexcept
        {
            using Domain = Components::VisualizationConfig::Domain;
            switch (d)
            {
            case Domain::Vertex: return 0u;
            case Domain::Face:   return 1u;
            case Domain::Edge:   return 2u;
            }
            return 0u;
        }

        static std::uint32_t ToVisDomain(Components::VisualizationConfig::ColorSource source) noexcept
        {
            using ColorSource = Components::VisualizationConfig::ColorSource;
            switch (source)
            {
            case ColorSource::PerVertexBuffer: return 0u;
            case ColorSource::PerFaceBuffer:   return 1u;
            case ColorSource::PerEdgeBuffer:   return 2u;
            case ColorSource::Material:
            case ColorSource::UniformColor:
            case ColorSource::ScalarField:
                return 0u;
            }
            return 0u;
        }

        static bool IsColorBufferSource(
            const Components::VisualizationConfig::ColorSource source) noexcept
        {
            using ColorSource = Components::VisualizationConfig::ColorSource;
            return source == ColorSource::PerVertexBuffer ||
                   source == ColorSource::PerEdgeBuffer ||
                   source == ColorSource::PerFaceBuffer;
        }

        static VisualizationAttributeDomain ToAttributeDomain(
            Components::VisualizationConfig::Domain d) noexcept
        {
            using Domain = Components::VisualizationConfig::Domain;
            switch (d)
            {
            case Domain::Vertex: return VisualizationAttributeDomain::Vertex;
            case Domain::Face:   return VisualizationAttributeDomain::Face;
            case Domain::Edge:   return VisualizationAttributeDomain::Edge;
            }
            return VisualizationAttributeDomain::Vertex;
        }

        static VisualizationAttributeDomain ToColorAttributeDomain(
            Components::VisualizationConfig::ColorSource source) noexcept
        {
            using ColorSource = Components::VisualizationConfig::ColorSource;
            switch (source)
            {
            case ColorSource::PerVertexBuffer:
                return VisualizationAttributeDomain::Vertex;
            case ColorSource::PerEdgeBuffer:
                return VisualizationAttributeDomain::Edge;
            case ColorSource::PerFaceBuffer:
                return VisualizationAttributeDomain::Face;
            default:
                return VisualizationAttributeDomain::Vertex;
            }
        }

        [[nodiscard]] static bool IsDefaultWhiteUniform(
            const Components::VisualizationConfig& cfg) noexcept
        {
            return cfg.Source == Components::VisualizationConfig::ColorSource::UniformColor &&
                   cfg.Color == glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        template <typename TypePredicate>
        [[nodiscard]] static const VisualizationPropertyBufferAddress*
        FindPropertyBufferAddress(
            std::span<const VisualizationPropertyBufferAddress> addresses,
            const std::string_view explicitKey,
            const std::string_view fallbackKey,
            const VisualizationAttributeDomain domain,
            TypePredicate typeMatches) noexcept
        {
            const std::string_view key =
                explicitKey.empty() ? fallbackKey : explicitKey;
            if (key.empty())
            {
                return nullptr;
            }

            for (const VisualizationPropertyBufferAddress& address : addresses)
            {
                if (address.SourceKey == key && address.Domain == domain &&
                    typeMatches(address.ValueType))
                {
                    return &address;
                }
            }
            return nullptr;
        }

        [[nodiscard]] static const ScalarAttributePacket*
        FindScalarPacket(
            std::span<const ScalarAttributePacket> packets,
            const std::string_view explicitKey,
            const std::string_view fallbackKey,
            const VisualizationAttributeDomain domain) noexcept
        {
            const std::string_view key =
                explicitKey.empty() ? fallbackKey : explicitKey;
            if (key.empty())
            {
                return nullptr;
            }

            for (const ScalarAttributePacket& packet : packets)
            {
                if (packet.Domain != domain)
                {
                    continue;
                }
                if (!explicitKey.empty())
                {
                    if (packet.SourceBufferKey == key)
                    {
                        return &packet;
                    }
                    continue;
                }
                if (packet.SourceBufferKey == key || packet.Name == key)
                {
                    return &packet;
                }
            }
            return nullptr;
        }

        [[nodiscard]] static std::uint32_t ToPointMode(
            const Components::RenderPoints::RenderType type) noexcept
        {
            switch (type)
            {
            case Components::RenderPoints::RenderType::Flat:
                return 0u;
            case Components::RenderPoints::RenderType::Sphere:
                return 1u;
            case Components::RenderPoints::RenderType::Surfel:
                return 2u;
            }
            return 1u;
        }

        static void ApplyPointRenderConfig(
            RHI::GpuEntityConfig& cfg,
            const Components::RenderPoints* points) noexcept
        {
            if (points == nullptr)
                return;

            cfg.Point.PointMode = ToPointMode(points->Type);
            if (const auto* uniform =
                    std::get_if<float>(&points->SizeSource);
                uniform != nullptr)
            {
                cfg.Point.PointSize = *uniform;
            }
        }

        static void ApplyLineRenderConfig(
            RHI::GpuEntityConfig& cfg,
            const Components::RenderEdges* edges) noexcept
        {
            if (edges == nullptr)
                return;

            if (const auto* uniform =
                    std::get_if<float>(&edges->WidthSource);
                uniform != nullptr)
            {
                cfg.Line.LineWidth = *uniform;
            }
        }

        RHI::GpuEntityConfig BuildEntityConfig(
            const VisualizationSyncRecord& record,
            const Components::GpuSceneSlot&        gpuSlot,
            ColormapSystem&                        colormapSys,
            std::span<const VisualizationPropertyBufferAddress> propertyBufferAddresses,
            std::span<const ScalarAttributePacket> scalarPackets) const
        {
            const Components::VisualizationConfig* visCfg = record.Visualization;
            RHI::GpuEntityConfig cfg{};
            cfg.ColorSourceMode = kMode_Material;
            cfg.VisualizationAlpha = 1.f;
            cfg.UniformColor = {1.f, 1.f, 1.f, 1.f};
            ApplyLineRenderConfig(cfg, record.Edges);
            ApplyPointRenderConfig(cfg, record.Points);

            if (Device)
            {
                auto setBda = [&](std::string_view name, std::uint64_t& outBda)
                {
                    const RHI::BufferHandle handle = gpuSlot.Find(name);
                    if (handle.IsValid())
                    {
                        outBda = Device->GetBufferDeviceAddress(handle);
                    }
                };

                setBda("normals", cfg.VertexNormalBDA);
                if (record.Points != nullptr)
                {
                    if (const auto* sizeName =
                            std::get_if<std::string>(&record.Points->SizeSource);
                        sizeName != nullptr)
                    {
                        setBda(*sizeName, cfg.Point.PointSizeBDA);
                    }
                    else
                    {
                        setBda("sizes", cfg.Point.PointSizeBDA);
                    }
                }
                if (record.Edges != nullptr)
                {
                    if (const auto* widthName =
                            std::get_if<std::string>(&record.Edges->WidthSource);
                        widthName != nullptr)
                    {
                        setBda(*widthName, cfg.Line.LineWidthBDA);
                    }
                }
            }

            if (!visCfg)
                return cfg;

            cfg.ColormapID = colormapSys.GetBindlessIndex(visCfg->Scalar.Map);
            cfg.ScalarRangeMin = visCfg->Scalar.RangeMin;
            cfg.ScalarRangeMax = visCfg->Scalar.RangeMax;
            cfg.BinCount = visCfg->Scalar.BinCount;
            cfg.IsolineCount = static_cast<float>(visCfg->Scalar.Isolines.Num);
            cfg.IsolineWidth = visCfg->Scalar.Isolines.Width;
            cfg.IsolineColor = visCfg->Scalar.Isolines.Color;
            // UI-032 — bounded explicit highlight isovalues; non-finite
            // entries are skipped fail-closed so the shader only sees
            // renderable values.
            {
                const auto& isolines = visCfg->Scalar.Isolines;
                const std::uint32_t requested = std::min<std::uint32_t>(
                    isolines.ValueCount,
                    Components::ScalarFieldConfig::kMaxIsolineValues);
                std::uint32_t written = 0u;
                for (std::uint32_t i = 0u; i < requested; ++i)
                {
                    const float value = isolines.Values[i];
                    if (!std::isfinite(value))
                        continue;
                    if (written < 4u)
                        cfg.IsoValuesA[static_cast<int>(written)] = value;
                    else
                        cfg.IsoValuesB[static_cast<int>(written - 4u)] = value;
                    ++written;
                }
                cfg.IsoValueCount = written;
            }
            cfg.VisualizationAlpha = 1.f;
            cfg.VisDomain = IsColorBufferSource(visCfg->Source)
                ? ToVisDomain(visCfg->Source)
                : ToVisDomain(visCfg->ScalarDomain);

            if (!Device)
                return cfg;

            auto setBdaAndCount = [&](std::string_view name, std::uint64_t& outBda)
            {
                const RHI::BufferHandle handle = gpuSlot.Find(name);
                if (handle.IsValid())
                {
                    outBda = Device->GetBufferDeviceAddress(handle);
                    if (const auto* entry = gpuSlot.FindEntry(name))
                        cfg.ElementCount = entry->ElementCount;
                }
            };

            auto setAddressAndCount = [&](const VisualizationPropertyBufferAddress* address,
                                          std::uint64_t& outBda)
            {
                if (address == nullptr || address->BufferBDA == 0u)
                {
                    return;
                }
                outBda = address->BufferBDA;
                cfg.ElementCount = address->ElementCount;
            };

            const auto source = visCfg->Source;
            if (source == Components::VisualizationConfig::ColorSource::UniformColor)
            {
                cfg.ColorSourceMode = kMode_UniformColor;
                cfg.UniformColor = visCfg->Color;
            }
            else if (source == Components::VisualizationConfig::ColorSource::ScalarField)
            {
                cfg.ColorSourceMode = kMode_ScalarField;
                const VisualizationAttributeDomain scalarDomain =
                    ToAttributeDomain(visCfg->ScalarDomain);
                const ScalarAttributePacket* scalarPacket =
                    FindScalarPacket(
                        scalarPackets,
                        record.ScalarPropertyBufferSourceKey,
                        visCfg->ScalarFieldName,
                        scalarDomain);
                if (scalarPacket != nullptr && visCfg->Scalar.AutoRange)
                {
                    cfg.ScalarRangeMin = scalarPacket->RangeMin;
                    cfg.ScalarRangeMax = scalarPacket->RangeMax;
                }
                // BUG-059 — a degenerate or non-finite range would normalize
                // every fragment to t=0 (the shader's flat-range guard) and
                // render the colormap's darkest bin everywhere. The adapters
                // reject such packets, but the component range reaches this
                // config directly; fall back to the validated packet range,
                // else expand around the requested value like the adapters'
                // flat-auto-range expansion.
                if (!(std::isfinite(cfg.ScalarRangeMin) &&
                      std::isfinite(cfg.ScalarRangeMax) &&
                      cfg.ScalarRangeMin < cfg.ScalarRangeMax))
                {
                    if (scalarPacket != nullptr)
                    {
                        cfg.ScalarRangeMin = scalarPacket->RangeMin;
                        cfg.ScalarRangeMax = scalarPacket->RangeMax;
                    }
                    else
                    {
                        const float center = std::isfinite(cfg.ScalarRangeMin)
                            ? cfg.ScalarRangeMin
                            : 0.f;
                        cfg.ScalarRangeMin = center - 0.5f;
                        cfg.ScalarRangeMax = center + 0.5f;
                    }
                }
                setBdaAndCount(visCfg->ScalarFieldName, cfg.ScalarBDA);
                if (cfg.ScalarBDA == 0u && scalarPacket != nullptr &&
                    scalarPacket->ScalarBufferBDA != 0u)
                {
                    cfg.ScalarBDA = scalarPacket->ScalarBufferBDA;
                    cfg.ElementCount = scalarPacket->ElementCount;
                }
                if (cfg.ScalarBDA == 0u)
                {
                    const VisualizationPropertyBufferAddress* address =
                        FindPropertyBufferAddress(
                            propertyBufferAddresses,
                            record.ScalarPropertyBufferSourceKey,
                            visCfg->ScalarFieldName,
                            scalarDomain,
                            [](const VisualizationValueType candidate)
                            {
                                return candidate == VisualizationValueType::ScalarFloat ||
                                       candidate == VisualizationValueType::ScalarDouble;
                            });
                    setAddressAndCount(address, cfg.ScalarBDA);
                }
            }
            else if (source == Components::VisualizationConfig::ColorSource::PerVertexBuffer ||
                     source == Components::VisualizationConfig::ColorSource::PerEdgeBuffer ||
                     source == Components::VisualizationConfig::ColorSource::PerFaceBuffer)
            {
                cfg.ColorSourceMode = kMode_PerElementRgba;
                setBdaAndCount(visCfg->ColorBufferName, cfg.ColorBDA);
                if (cfg.ColorBDA == 0u)
                {
                    const VisualizationPropertyBufferAddress* address =
                        FindPropertyBufferAddress(
                            propertyBufferAddresses,
                            record.ColorPropertyBufferSourceKey,
                            visCfg->ColorBufferName,
                            ToColorAttributeDomain(source),
                            [](const VisualizationValueType candidate)
                            {
                                return candidate == VisualizationValueType::Rgba8 ||
                                       candidate == VisualizationValueType::RgbaFloat4;
                            });
                    setAddressAndCount(address, cfg.ColorBDA);
                }
            }

            return cfg;
        }
    };

    // ----------------------------------------------------------------
    // VisualizationSyncSystem
    // ----------------------------------------------------------------
    VisualizationSyncSystem::VisualizationSyncSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    VisualizationSyncSystem::~VisualizationSyncSystem() = default;

    // ----------------------------------------------------------------
    void VisualizationSyncSystem::Initialize(RHI::IDevice& device)
    {
        assert(!m_Impl->Initialized);
        m_Impl->Device = &device;

        m_Impl->Initialized = true;
    }

    // ----------------------------------------------------------------
    void VisualizationSyncSystem::Shutdown()
    {
        m_Impl->Device       = nullptr;
        m_Impl->Initialized  = false;
    }

    // ----------------------------------------------------------------
    bool VisualizationSyncSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    // ----------------------------------------------------------------
    void VisualizationSyncSystem::Sync(std::span<VisualizationSyncRecord> records,
                                       MaterialSystem& matSys,
                                       ColormapSystem& colormapSys,
                                       GpuWorld&       gpuWorld,
                                       std::span<const VisualizationPropertyBufferAddress> propertyBufferAddresses,
                                       std::span<const ScalarAttributePacket> scalarPackets)
    {
        using namespace Components;
        using ColorSource = VisualizationConfig::ColorSource;

        for (VisualizationSyncRecord& record : records)
        {
            if (!record.GpuSlot)
                continue;

            const auto& gpuSlot = *record.GpuSlot;
            MaterialInstance* const matInst = record.Material;

            // Apply TintOverride to the BASE material if set.
            if (matInst != nullptr &&
                matInst->TintOverride.has_value() && matInst->Lease.IsValid())
            {
                matSys.Patch(matInst->Lease.GetHandle(),
                    [tint = *matInst->TintOverride](MaterialParams& p)
                    {
                        p.BaseColorFactor = tint;
                    });
            }

            const auto* visCfg = record.Visualization;

            if (matInst == nullptr &&
                (visCfg == nullptr || visCfg->Source == ColorSource::Material ||
                 Impl::IsDefaultWhiteUniform(*visCfg)))
            {
                continue;
            }

            const GpuInstanceHandle targetInstance =
                record.TargetInstance.IsValid()
                    ? record.TargetInstance
                    : (gpuSlot.HasInstance() ? gpuSlot.ToInstanceHandle() : GpuInstanceHandle{});
            if (targetInstance.IsValid())
            {
                gpuWorld.SetEntityConfig(
                    targetInstance,
                    m_Impl->BuildEntityConfig(record, gpuSlot, colormapSys,
                                              propertyBufferAddresses,
                                              scalarPackets));
            }

            if (matInst == nullptr)
            {
                continue;
            }

            matInst->EffectiveSlot = matSys.GetMaterialSlot(matInst->Lease.GetHandle());
        }
    }

} // namespace Extrinsic::Graphics
