module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Module;

import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] Core::Expected<std::vector<const SimSystemDesc*>> BuildSystemOrder(
            const std::vector<SimSystemDesc>& systems)
        {
            const std::size_t systemCount = systems.size();
            std::unordered_set<std::string> passNames;
            for (const SimSystemDesc& system : systems)
            {
                if (!passNames.insert(system.PassName).second)
                {
                    Core::Log::Error(
                        "[Runtime.Module] Duplicate SimSystem PassName '{}'",
                        system.PassName);
                    return Core::Err<std::vector<const SimSystemDesc*>>(
                        Core::ErrorCode::InvalidArgument);
                }
            }

            std::unordered_map<std::uint32_t, std::vector<std::size_t>> signalers;
            for (std::size_t index = 0; index < systemCount; ++index)
            {
                for (const Core::Hash::StringID signal : systems[index].EmitSignals)
                    signalers[signal.Value].push_back(index);
            }

            std::vector<std::unordered_set<std::size_t>> successors(systemCount);
            std::vector<std::size_t>                     inDegree(systemCount, 0);
            for (std::size_t waiter = 0; waiter < systemCount; ++waiter)
            {
                for (const Core::Hash::StringID signal : systems[waiter].WaitForSignals)
                {
                    const auto found = signalers.find(signal.Value);
                    if (found == signalers.end())
                        continue;

                    for (const std::size_t signaler : found->second)
                    {
                        if (signaler == waiter)
                        {
                            Core::Log::Error(
                                "[Runtime.Module] SimSystem '{}' waits for its own signal {}",
                                systems[waiter].PassName,
                                signal.Value);
                            return Core::Err<std::vector<const SimSystemDesc*>>(
                                Core::ErrorCode::InvalidState);
                        }
                        if (successors[signaler].insert(waiter).second)
                            ++inDegree[waiter];
                    }
                }
            }

            const auto stableLess = [&systems](const std::size_t lhs,
                                               const std::size_t rhs)
            {
                if (systems[lhs].PassName != systems[rhs].PassName)
                    return systems[lhs].PassName < systems[rhs].PassName;
                return lhs < rhs;
            };
            std::set<std::size_t, decltype(stableLess)> ready(stableLess);
            for (std::size_t index = 0; index < systemCount; ++index)
            {
                if (inDegree[index] == 0)
                    ready.insert(index);
            }

            std::vector<const SimSystemDesc*> ordered;
            ordered.reserve(systemCount);
            while (!ready.empty())
            {
                const std::size_t index = *ready.begin();
                ready.erase(ready.begin());
                ordered.push_back(&systems[index]);

                for (const std::size_t successor : successors[index])
                {
                    if (--inDegree[successor] == 0)
                        ready.insert(successor);
                }
            }

            if (ordered.size() != systemCount)
            {
                for (std::size_t index = 0; index < systemCount; ++index)
                {
                    if (inDegree[index] != 0)
                    {
                        Core::Log::Error(
                            "[Runtime.Module] SimSystem '{}' participates in a cyclic named-signal dependency",
                            systems[index].PassName);
                    }
                }
                return Core::Err<std::vector<const SimSystemDesc*>>(
                    Core::ErrorCode::InvalidState);
            }

            return ordered;
        }
    }

    SystemHandle ModuleRegistrationSink::AddSimSystem(SimSystemDesc desc)
    {
        const SystemHandle handle{m_NextSystemHandle++};
        m_SimSystems.push_back(std::move(desc));
        return handle;
    }

    FrameHookHandle ModuleRegistrationSink::AddFrameHook(
        FramePhase phase, std::function<void(FrameHookContext&)> hook)
    {
        const FrameHookHandle handle{m_NextHookHandle++};
        m_FrameHooks.push_back(FrameHookRecord{handle, phase, std::move(hook)});
        return handle;
    }

    Core::Result ModuleRegistrationSink::ApplySimSystems(Core::FrameGraph&     graph,
                                                         ECS::Scene::Registry& scene) const
    {
        // Named signals establish causal direction before the sequential
        // FrameGraph hazard builder sees the passes; PassName provides the
        // deterministic order for otherwise-independent systems.
        const auto orderedSystems = BuildSystemOrder(m_SimSystems);
        if (!orderedSystems.has_value())
            return Core::Err(orderedSystems.error());

        for (const SimSystemDesc* const desc : *orderedSystems)
        {
            graph.AddPass(
                desc->PassName,
                [desc](Core::FrameGraphBuilder& builder)
                {
                    for (const Core::Hash::StringID signal : desc->WaitForSignals)
                        builder.WaitFor(signal);
                    if (desc->Declare)
                        desc->Declare(builder);
                    for (const Core::Hash::StringID signal : desc->EmitSignals)
                        builder.Signal(signal);
                },
                [desc, &scene]
                {
                    if (desc->Execute)
                        desc->Execute(scene);
                });
        }
        return Core::Ok();
    }

    void ModuleRegistrationSink::InvokeFrameHooks(FramePhase        phase,
                                                  FrameHookContext& context) const
    {
        for (const FrameHookRecord& record : m_FrameHooks)
        {
            if (record.Phase == phase && record.Hook)
                record.Hook(context);
        }
    }

    std::size_t ModuleRegistrationSink::FrameHookCount(FramePhase phase) const noexcept
    {
        std::size_t count = 0;
        for (const FrameHookRecord& record : m_FrameHooks)
        {
            if (record.Phase == phase)
                ++count;
        }
        return count;
    }

    void ModuleRegistrationSink::Clear() noexcept
    {
        m_SimSystems.clear();
        m_FrameHooks.clear();
    }
}
