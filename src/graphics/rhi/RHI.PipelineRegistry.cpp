module;

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

module Extrinsic.RHI.PipelineRegistry;

namespace Extrinsic::RHI
{
    namespace
    {
        [[nodiscard]] bool IsComputeKey(const PipelineKey& key) noexcept
        {
            return !key.Compute.Path.empty();
        }

        [[nodiscard]] bool UsesShaderPath(const PipelineKey& key, std::string_view path) noexcept
        {
            return key.Vertex.Path == path || key.Fragment.Path == path || key.Compute.Path == path;
        }

        [[nodiscard]] bool KeyMatchesDesc(const PipelineKey& key, const PipelineDesc& desc) noexcept
        {
            return key.Vertex.Path == desc.VertexShaderPath &&
                   key.Fragment.Path == desc.FragmentShaderPath &&
                   key.Compute.Path == desc.ComputeShaderPath &&
                   key.State == MakePipelineStateKey(desc);
        }

        [[nodiscard]] bool HasRequiredShaders(const PipelineKey& key) noexcept
        {
            if (IsComputeKey(key))
            {
                return !key.Compute.Path.empty();
            }
            return !key.Vertex.Path.empty() && !key.Fragment.Path.empty();
        }
    }

    PipelineStateKey MakePipelineStateKey(const PipelineDesc& desc)
    {
        PipelineStateKey key{};
        key.PrimitiveTopology = desc.PrimitiveTopology;
        key.Rasterizer = desc.Rasterizer;
        key.DepthStencil = desc.DepthStencil;
        for (std::uint32_t i = 0; i < MaxColorTargets; ++i)
        {
            key.ColorBlend[i] = desc.ColorBlend[i];
            key.ColorTargetFormats[i] = desc.ColorTargetFormats[i];
        }
        key.ColorTargetCount = desc.ColorTargetCount;
        key.DepthTargetFormat = desc.DepthTargetFormat;
        key.PushConstantSize = desc.PushConstantSize;
        return key;
    }

    PipelineKey MakePipelineKey(const PipelineDesc& desc,
                                std::uint64_t vertexGeneration,
                                std::uint64_t fragmentGeneration,
                                std::uint64_t computeGeneration)
    {
        return PipelineKey{
            .Vertex = ShaderModuleId{.Path = desc.VertexShaderPath, .Generation = vertexGeneration},
            .Fragment = ShaderModuleId{.Path = desc.FragmentShaderPath, .Generation = fragmentGeneration},
            .Compute = ShaderModuleId{.Path = desc.ComputeShaderPath, .Generation = computeGeneration},
            .State = MakePipelineStateKey(desc),
        };
    }

    struct PipelineRegistry::Impl
    {
        struct Entry
        {
            PipelineKey Key{};
            PipelineDesc Desc{};
            PipelineManager::PipelineLease Lease{};
        };

        explicit Impl(PipelineManager& pipelines)
            : Pipelines(pipelines)
        {
        }

        PipelineManager& Pipelines;
        std::vector<Entry> Entries;
        PipelineRegistryDiagnostics Diagnostics{};
    };

    PipelineRegistry::PipelineRegistry(PipelineManager& pipelines)
        : m_Impl(std::make_unique<Impl>(pipelines))
    {
    }

    PipelineRegistry::~PipelineRegistry() = default;

    Core::Expected<PipelineHandle> PipelineRegistry::GetOrCreatePipeline(const PipelineKey& key,
                                                                         const PipelineDesc& desc)
    {
        if (!HasRequiredShaders(key))
        {
            ++m_Impl->Diagnostics.MissingShaderCount;
            return Core::Err<PipelineHandle>(Core::ErrorCode::ResourceNotFound);
        }

        if (!KeyMatchesDesc(key, desc))
        {
            ++m_Impl->Diagnostics.InvalidKeyCount;
            return Core::Err<PipelineHandle>(Core::ErrorCode::InvalidArgument);
        }

        const auto found = std::ranges::find_if(m_Impl->Entries,
                                                [&](const Impl::Entry& entry)
                                                {
                                                    return entry.Key == key;
                                                });
        if (found != m_Impl->Entries.end())
        {
            ++m_Impl->Diagnostics.CacheHitCount;
            return Core::Expected<PipelineHandle>(found->Lease.GetHandle());
        }

        ++m_Impl->Diagnostics.CacheMissCount;
        auto leaseOr = m_Impl->Pipelines.Create(desc);
        if (!leaseOr.has_value())
        {
            ++m_Impl->Diagnostics.PipelineCreationFailureCount;
            return Core::Err<PipelineHandle>(leaseOr.error());
        }

        const PipelineHandle handle = leaseOr->GetHandle();
        m_Impl->Entries.push_back(Impl::Entry{
            .Key = key,
            .Desc = desc,
            .Lease = std::move(*leaseOr),
        });
        m_Impl->Diagnostics.LivePipelineCount = static_cast<std::uint32_t>(m_Impl->Entries.size());
        return Core::Expected<PipelineHandle>(handle);
    }

    std::uint32_t PipelineRegistry::InvalidateShaderPath(std::string_view path)
    {
        std::uint32_t removed = 0;
        for (std::size_t i = 0; i < m_Impl->Entries.size();)
        {
            if (!UsesShaderPath(m_Impl->Entries[i].Key, path))
            {
                ++i;
                continue;
            }
            m_Impl->Entries.erase(m_Impl->Entries.begin() + static_cast<std::ptrdiff_t>(i));
            ++removed;
        }

        m_Impl->Diagnostics.ReloadInvalidationCount += removed;
        m_Impl->Diagnostics.LivePipelineCount = static_cast<std::uint32_t>(m_Impl->Entries.size());
        return removed;
    }

    void PipelineRegistry::Clear()
    {
        m_Impl->Entries.clear();
        m_Impl->Diagnostics.LivePipelineCount = 0;
    }

    PipelineRegistryDiagnostics PipelineRegistry::GetDiagnostics() const noexcept
    {
        PipelineRegistryDiagnostics diagnostics = m_Impl->Diagnostics;
        diagnostics.LivePipelineCount = static_cast<std::uint32_t>(m_Impl->Entries.size());
        return diagnostics;
    }
}



