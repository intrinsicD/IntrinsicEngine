module;

#include <bit>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Extrinsic.Graphics.VisualizationSyncSystem;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Bindless;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.GpuSceneSlot;
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
    // Helper: pack normalised float RGBA into uint32_t RGBA8
    // ----------------------------------------------------------------
    static std::uint32_t PackColorVec4(glm::vec4 c) noexcept
    {
        return ColormapSystem::PackVec4(c);
    }

    // ----------------------------------------------------------------
    // Impl
    // ----------------------------------------------------------------
    struct VisualizationSyncSystem::Impl
    {
        RHI::IDevice*   Device  = nullptr;
        bool            Initialized = false;

        MaterialTypeHandle SciVisTypeHandle;

        // Per-entity override material leases.
        // Key = entt::entity (raw uint32_t cast to avoid hashing issues).
        using EntityInt = std::uint32_t;
        std::unordered_map<EntityInt, MaterialSystem::MaterialLease> OverrideLeases;

        // ----------------------------------------------------------------
        /// Get or create the override lease for an entity.
        MaterialSystem::MaterialLease& EnsureOverrideLease(entt::entity entity,
                                                           MaterialSystem& matSys)
        {
            const auto key = static_cast<EntityInt>(entity);
            auto it = OverrideLeases.find(key);
            if (it == OverrideLeases.end())
            {
                auto lease = matSys.CreateInstance(SciVisTypeHandle, {});
                it = OverrideLeases.emplace(key, std::move(lease)).first;
            }
            return it->second;
        }

        /// Release the override lease for an entity (if any).
        void ReleaseOverrideLease(entt::entity entity)
        {
            OverrideLeases.erase(static_cast<EntityInt>(entity));
        }

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

        /// Build MaterialParams for the ScalarField override.
        MaterialParams BuildScalarFieldParams(
            const Components::VisualizationConfig& cfg,
            ColormapSystem&                        colormapSys)
        {
            MaterialParams p{};
            p.Flags = MaterialFlags::Unlit;

            const RHI::BindlessIndex colormapIdx =
                colormapSys.GetBindlessIndex(cfg.Scalar.Map);

            // CustomData[0]: colourmap index, domain, range
            p.CustomData[0] = {
                std::bit_cast<float>(colormapIdx),
                std::bit_cast<float>(ToVisDomain(cfg.ScalarDomain)),
                cfg.Scalar.RangeMin,
                cfg.Scalar.RangeMax,
            };

            // CustomData[1]: isoline / binning
            const std::uint32_t packedColor =
                PackColorVec4(cfg.Scalar.Isolines.Color);
            p.CustomData[1] = {
                std::bit_cast<float>(cfg.Scalar.Isolines.Num),
                std::bit_cast<float>(packedColor),
                cfg.Scalar.Isolines.Width,
                std::bit_cast<float>(cfg.Scalar.BinCount),
            };

            // CustomData[2]: reserved for non-BDA constants.
            p.CustomData[2] = {
                1.f,
                0.f,
                0.f,
                0.f,
            };

            return p;
        }

        /// Build MaterialParams for a UniformColor override.
        static MaterialParams BuildUniformColorParams(glm::vec4 color)
        {
            MaterialParams p{};
            p.BaseColorFactor = color;
            p.Flags           = MaterialFlags::Unlit;
            p.CustomData[2]   = {
                color.a, 0.f, 0.f, 0.f,
            };
            return p;
        }

        /// Build MaterialParams for a per-element RGBA buffer override.
        static MaterialParams BuildPerElementParams()
        {
            MaterialParams p{};
            p.Flags = MaterialFlags::Unlit;
            p.CustomData[2] = {
                1.f, 0.f, 0.f, 0.f,
            };
            return p;
        }

        RHI::GpuEntityConfig BuildEntityConfig(
            const Components::VisualizationConfig* visCfg,
            const Components::GpuSceneSlot&        gpuSlot,
            ColormapSystem&                        colormapSys) const
        {
            RHI::GpuEntityConfig cfg{};
            cfg.ColorSourceMode = kMode_Material;
            cfg.VisualizationAlpha = 1.f;
            cfg.UniformColor = {1.f, 1.f, 1.f, 1.f};

            if (!visCfg)
                return cfg;

            cfg.ColormapID = colormapSys.GetBindlessIndex(visCfg->Scalar.Map);
            cfg.ScalarRangeMin = visCfg->Scalar.RangeMin;
            cfg.ScalarRangeMax = visCfg->Scalar.RangeMax;
            cfg.BinCount = visCfg->Scalar.BinCount;
            cfg.IsolineCount = static_cast<float>(visCfg->Scalar.Isolines.Num);
            cfg.IsolineWidth = visCfg->Scalar.Isolines.Width;
            cfg.IsolineColor = visCfg->Scalar.Isolines.Color;
            cfg.VisualizationAlpha = 1.f;
            cfg.VisDomain = ToVisDomain(visCfg->ScalarDomain);

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

            const auto source = visCfg->Source;
            if (source == Components::VisualizationConfig::ColorSource::UniformColor)
            {
                cfg.ColorSourceMode = kMode_UniformColor;
                cfg.UniformColor = visCfg->Color;
            }
            else if (source == Components::VisualizationConfig::ColorSource::ScalarField)
            {
                cfg.ColorSourceMode = kMode_ScalarField;
                setBdaAndCount(visCfg->ScalarFieldName, cfg.ScalarBDA);
            }
            else if (source == Components::VisualizationConfig::ColorSource::PerVertexBuffer ||
                     source == Components::VisualizationConfig::ColorSource::PerEdgeBuffer ||
                     source == Components::VisualizationConfig::ColorSource::PerFaceBuffer)
            {
                cfg.ColorSourceMode = kMode_PerElementRgba;
                setBdaAndCount(visCfg->ColorBufferName, cfg.ColorBDA);
            }

            setBdaAndCount("normals", cfg.VertexNormalBDA);
            setBdaAndCount("sizes", cfg.PointSizeBDA);
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
    void VisualizationSyncSystem::Initialize(MaterialSystem& matSys,
                                             RHI::IDevice&   device)
    {
        assert(!m_Impl->Initialized);
        m_Impl->Device = &device;

        // Register the SciVis material type.  Must happen BEFORE any
        // other custom type so it gets kMaterialTypeID_SciVis (= 1).
        const MaterialTypeDesc sciVisDesc{
            .Name = "SciVis",
            .CustomParams = {
                {"ColormapAndDomain",  "colourmap bindless idx, domain, rangeMin, rangeMax"},
                {"IsolinesAndBins",    "isolineCount, packedColor, isolineWidth, binCount"},
                {"ScalarBDA",          "BDA lo/hi, elementCount, colorSourceMode"},
                {"Reserved",           "reserved for future use"},
            },
        };

        m_Impl->SciVisTypeHandle = matSys.RegisterType(sciVisDesc);
        assert(m_Impl->SciVisTypeHandle.IsValid() &&
               "SciVis type registration failed — is another type already registered?");

        m_Impl->Initialized = true;
    }

    // ----------------------------------------------------------------
    void VisualizationSyncSystem::Shutdown()
    {
        m_Impl->OverrideLeases.clear();
        m_Impl->Device       = nullptr;
        m_Impl->Initialized  = false;
    }

    // ----------------------------------------------------------------
    bool VisualizationSyncSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    // ----------------------------------------------------------------
    void VisualizationSyncSystem::Sync(entt::registry& registry,
                                       MaterialSystem& matSys,
                                       ColormapSystem& colormapSys,
                                       GpuWorld&       gpuWorld)
    {
        using namespace Components;
        using ColorSource = VisualizationConfig::ColorSource;

        // ---- Pass 1: entities with MaterialInstance + GpuSceneSlot -------
        auto view = registry.view<MaterialInstance, GpuSceneSlot>();

        for (const auto [entity, matInst, gpuSlot] : view.each())
        {
            // Apply TintOverride to the BASE material if set.
            if (matInst.TintOverride.has_value() && matInst.Lease.IsValid())
            {
                matSys.Patch(matInst.Lease.GetHandle(),
                    [tint = *matInst.TintOverride](MaterialParams& p)
                    {
                        p.BaseColorFactor = tint;
                    });
            }

            const auto* visCfg = registry.try_get<VisualizationConfig>(entity);

            if (gpuSlot.HasInstance())
            {
                gpuWorld.SetEntityConfig(
                    gpuSlot.ToInstanceHandle(),
                    m_Impl->BuildEntityConfig(visCfg, gpuSlot, colormapSys));
            }

            if (!visCfg || visCfg->Source == ColorSource::Material)
            {
                // No sci-vis override — use the base material slot directly.
                if (matInst.Lease.IsValid())
                    matInst.EffectiveSlot = matSys.GetMaterialSlot(matInst.Lease.GetHandle());
                m_Impl->ReleaseOverrideLease(entity);
                continue;
            }

            // Build params for the requested colour source.
            MaterialParams overrideParams{};
            switch (visCfg->Source)
            {
            case ColorSource::UniformColor:
                overrideParams = Impl::BuildUniformColorParams(visCfg->Color);
                break;

            case ColorSource::ScalarField:
                overrideParams = m_Impl->BuildScalarFieldParams(*visCfg, colormapSys);
                break;

            case ColorSource::PerVertexBuffer:
            case ColorSource::PerEdgeBuffer:
            case ColorSource::PerFaceBuffer:
                overrideParams = Impl::BuildPerElementParams();
                break;

            default:
                break;
            }

            // Apply params to the per-entity override material.
            auto& lease = m_Impl->EnsureOverrideLease(entity, matSys);
            assert(lease.IsValid());
            matSys.SetParams(lease.GetHandle(), overrideParams);

            matInst.EffectiveSlot = matSys.GetMaterialSlot(lease.GetHandle());
        }

        // ---- Pass 2: release stale override leases -----------------------
        // Entities that no longer carry MaterialInstance have been destroyed
        // or had the component removed.  Their map entries must be cleaned up.
        std::vector<std::uint32_t> staleKeys;
        for (const auto& [key, lease] : m_Impl->OverrideLeases)
        {
            const auto entity = static_cast<entt::entity>(key);
            if (!registry.valid(entity) || !registry.all_of<MaterialInstance>(entity))
                staleKeys.push_back(key);
        }
        for (const auto key : staleKeys)
            m_Impl->OverrideLeases.erase(key);
    }

    // ----------------------------------------------------------------
    std::uint32_t VisualizationSyncSystem::GetOverrideLeaseCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_Impl->OverrideLeases.size());
    }

} // namespace Extrinsic::Graphics
