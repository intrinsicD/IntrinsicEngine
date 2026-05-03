module;

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.DebugViewSystem;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] DebugViewResourceClass ClassifyResource(const FrameRecipeResourceDeclaration& resource) noexcept
        {
            if (resource.Backbuffer)
            {
                return DebugViewResourceClass::Backbuffer;
            }

            switch (resource.Kind)
            {
            case FrameRecipeResourceKind::SceneDepth:
            case FrameRecipeResourceKind::ShadowAtlas:
                return DebugViewResourceClass::DepthTexture;
            case FrameRecipeResourceKind::Backbuffer:
                return DebugViewResourceClass::Backbuffer;
            case FrameRecipeResourceKind::SceneTable:
            case FrameRecipeResourceKind::InstanceStatic:
            case FrameRecipeResourceKind::InstanceDynamic:
            case FrameRecipeResourceKind::EntityConfig:
            case FrameRecipeResourceKind::GeometryRecords:
            case FrameRecipeResourceKind::Bounds:
            case FrameRecipeResourceKind::Lights:
            case FrameRecipeResourceKind::MaterialBuffer:
            case FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs:
            case FrameRecipeResourceKind::SurfaceOpaqueCount:
            case FrameRecipeResourceKind::LinesIndexedArgs:
            case FrameRecipeResourceKind::LinesCount:
            case FrameRecipeResourceKind::PointsNonIndexedArgs:
            case FrameRecipeResourceKind::PointsCount:
            case FrameRecipeResourceKind::PickingReadback:
            case FrameRecipeResourceKind::PostProcessHistogram:
                return DebugViewResourceClass::Buffer;
            case FrameRecipeResourceKind::EntityId:
            case FrameRecipeResourceKind::PrimitiveId:
            case FrameRecipeResourceKind::SceneNormal:
            case FrameRecipeResourceKind::Albedo:
            case FrameRecipeResourceKind::Material0:
            case FrameRecipeResourceKind::SceneColorHDR:
            case FrameRecipeResourceKind::SceneColorLDR:
            case FrameRecipeResourceKind::SelectionOutline:
            case FrameRecipeResourceKind::DebugViewRGBA:
            case FrameRecipeResourceKind::PostProcessBloomScratch:
            case FrameRecipeResourceKind::PostProcessAATemp:
                return DebugViewResourceClass::Texture;
            }
            return DebugViewResourceClass::Unknown;
        }

        [[nodiscard]] bool IsSampleableClass(const DebugViewResourceClass resourceClass) noexcept
        {
            return resourceClass == DebugViewResourceClass::Texture ||
                   resourceClass == DebugViewResourceClass::DepthTexture;
        }

        [[nodiscard]] const DebugViewInspectableResource* FindResource(
            const std::vector<DebugViewInspectableResource>& resources,
            const std::string& name) noexcept
        {
            for (const DebugViewInspectableResource& resource : resources)
            {
                if (resource.Name == name)
                {
                    return &resource;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const char* ClassName(const DebugViewResourceClass resourceClass) noexcept
        {
            switch (resourceClass)
            {
            case DebugViewResourceClass::Texture:
                return "texture";
            case DebugViewResourceClass::DepthTexture:
                return "depth-texture";
            case DebugViewResourceClass::Buffer:
                return "buffer";
            case DebugViewResourceClass::Backbuffer:
                return "backbuffer";
            case DebugViewResourceClass::Alias:
                return "alias";
            case DebugViewResourceClass::Unknown:
                return "unknown";
            }
            return "unknown";
        }
    }

    struct DebugViewSystem::Impl
    {
        bool Initialized{false};
        DebugViewSettings Settings{};
        DebugViewDiagnostics Diagnostics{};
        DebugViewResolvedSelection Resolved{};
    };

    DebugViewSystem::DebugViewSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    DebugViewSystem::~DebugViewSystem() = default;

    void DebugViewSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void DebugViewSystem::Shutdown()
    {
        m_Impl->Initialized = false;
        m_Impl->Resolved = {};
    }

    void DebugViewSystem::SetSettings(DebugViewSettings settings)
    {
        if (settings.RequestedResourceName.empty())
        {
            settings.RequestedResourceName = "FrameRecipe.PresentSource";
        }
        m_Impl->Settings = std::move(settings);
        m_Impl->Diagnostics = {};
        m_Impl->Resolved = {};
    }

    bool DebugViewSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    const DebugViewSettings& DebugViewSystem::GetSettings() const noexcept
    {
        return m_Impl->Settings;
    }

    DebugViewDiagnostics DebugViewSystem::GetDiagnostics() const noexcept
    {
        return m_Impl->Diagnostics;
    }

    DebugViewResolvedSelection DebugViewSystem::GetResolvedSelection() const
    {
        return m_Impl->Resolved;
    }

    std::vector<DebugViewInspectableResource> DebugViewSystem::BuildInspectionTable(
        const FrameRecipeIntrospection& frameRecipe) const
    {
        std::vector<DebugViewInspectableResource> resources{};
        resources.reserve(frameRecipe.Resources.size());

        for (const FrameRecipeResourceDeclaration& resource : frameRecipe.Resources)
        {
            const DebugViewResourceClass resourceClass = ClassifyResource(resource);
            const bool sampleable = resource.Enabled && IsSampleableClass(resourceClass);
            const bool previewable = sampleable && resource.Kind != FrameRecipeResourceKind::DebugViewRGBA;
            resources.push_back(DebugViewInspectableResource{
                .Name = std::string{resource.Name},
                .Kind = resource.Kind,
                .ResourceClass = resourceClass,
                .Enabled = resource.Enabled,
                .Sampleable = sampleable,
                .Previewable = previewable,
                .Imported = resource.Imported,
                .Backbuffer = resource.Backbuffer,
            });
        }

        return resources;
    }

    DebugViewResolvedSelection DebugViewSystem::ResolveSelection(const FrameRecipeIntrospection& frameRecipe,
                                                                 std::string fallbackResourceName)
    {
        m_Impl->Diagnostics = {};
        DebugViewResolvedSelection resolved{};
        resolved.RequestedResourceName = m_Impl->Settings.RequestedResourceName;

        const std::vector<DebugViewInspectableResource> resources = BuildInspectionTable(frameRecipe);
        const auto fallback = [&resources, &fallbackResourceName]() -> const DebugViewInspectableResource* {
            if (const DebugViewInspectableResource* explicitFallback = FindResource(resources, fallbackResourceName);
                explicitFallback != nullptr && explicitFallback->Previewable)
            {
                return explicitFallback;
            }
            if (const DebugViewInspectableResource* hdr = FindResource(resources, "SceneColorHDR");
                hdr != nullptr && hdr->Previewable)
            {
                return hdr;
            }
            for (const DebugViewInspectableResource& resource : resources)
            {
                if (resource.Previewable)
                {
                    return &resource;
                }
            }
            return nullptr;
        };

        const auto useFallback = [&](const DebugViewFallbackReason reason) {
            const DebugViewInspectableResource* fallbackResource = fallback();
            resolved.UsedFallback = true;
            resolved.FallbackReason = reason;
            m_Impl->Diagnostics.UsedFallback = true;
            m_Impl->Diagnostics.LastFallbackReason = reason;
            if (fallbackResource == nullptr)
            {
                resolved.Enabled = false;
                resolved.FallbackReason = DebugViewFallbackReason::FallbackUnavailable;
                m_Impl->Diagnostics.LastFallbackReason = DebugViewFallbackReason::FallbackUnavailable;
                return;
            }

            resolved.Enabled = true;
            resolved.SelectedResourceName = fallbackResource->Name;
            resolved.SelectedKind = fallbackResource->Kind;
            resolved.SelectedClass = fallbackResource->ResourceClass;
        };

        if (!m_Impl->Settings.Enabled)
        {
            useFallback(DebugViewFallbackReason::DebugViewDisabled);
            resolved.Enabled = false;
            m_Impl->Resolved = resolved;
            return m_Impl->Resolved;
        }

        const bool requestedPresentSource = resolved.RequestedResourceName == "FrameRecipe.PresentSource";
        const DebugViewInspectableResource* requested = requestedPresentSource
            ? fallback()
            : FindResource(resources, resolved.RequestedResourceName);

        if (requested == nullptr)
        {
            ++m_Impl->Diagnostics.MissingResourceCount;
            useFallback(DebugViewFallbackReason::ResourceMissing);
            m_Impl->Resolved = resolved;
            return m_Impl->Resolved;
        }

        if (!requested->Enabled)
        {
            ++m_Impl->Diagnostics.DisabledResourceCount;
            useFallback(DebugViewFallbackReason::ResourceDisabled);
            m_Impl->Resolved = resolved;
            return m_Impl->Resolved;
        }

        if (!requested->Previewable)
        {
            ++m_Impl->Diagnostics.UnsupportedResourceCount;
            useFallback(DebugViewFallbackReason::UnsupportedResourceClass);
            m_Impl->Resolved = resolved;
            return m_Impl->Resolved;
        }

        resolved.Enabled = true;
        resolved.SelectedResourceName = requested->Name;
        resolved.SelectedKind = requested->Kind;
        resolved.SelectedClass = requested->ResourceClass;
        m_Impl->Resolved = resolved;
        return m_Impl->Resolved;
    }

    std::string DebugViewSystem::FormatInspectionDump(const FrameRecipeIntrospection& frameRecipe) const
    {
        const std::vector<DebugViewInspectableResource> resources = BuildInspectionTable(frameRecipe);
        std::ostringstream out;
        out << "debug-view-resources:";
        for (const DebugViewInspectableResource& resource : resources)
        {
            out << '\n'
                << resource.Name
                << " class=" << ClassName(resource.ResourceClass)
                << " enabled=" << (resource.Enabled ? "true" : "false")
                << " sampleable=" << (resource.Sampleable ? "true" : "false")
                << " previewable=" << (resource.Previewable ? "true" : "false");
        }
        return out.str();
    }

    DebugViewPushConstants DebugViewSystem::BuildPushConstants() const noexcept
    {
        return DebugViewPushConstants{
            .ResourceKind = static_cast<std::uint32_t>(m_Impl->Resolved.SelectedKind),
            .ResourceClass = static_cast<std::uint32_t>(m_Impl->Resolved.SelectedClass),
            .UsedFallback = m_Impl->Resolved.UsedFallback ? 1u : 0u,
            .Reserved = 0u,
        };
    }
}

