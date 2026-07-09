module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ModuleSchedule;

import Extrinsic.Core.Hash;
import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool HasLabel(
            const std::vector<Core::Hash::StringID>& labels,
            const Core::Hash::StringID needle) noexcept
        {
            return std::find(labels.begin(), labels.end(), needle) != labels.end();
        }
    }

    bool RuntimeModuleSchedule::SimSystemLess(
        const RuntimeModuleSimSystemRecord& lhs,
        const RuntimeModuleSimSystemRecord& rhs)
    {
        if (lhs.ModuleName != rhs.ModuleName)
            return lhs.ModuleName < rhs.ModuleName;
        if (lhs.Desc.Name != rhs.Desc.Name)
            return lhs.Desc.Name < rhs.Desc.Name;
        return lhs.Sequence < rhs.Sequence;
    }

    bool RuntimeModuleSchedule::FrameHookLess(
        const RuntimeModuleFrameHookRecord& lhs,
        const RuntimeModuleFrameHookRecord& rhs)
    {
        if (lhs.Phase != rhs.Phase)
            return lhs.Phase < rhs.Phase;
        if (lhs.ModuleName != rhs.ModuleName)
            return lhs.ModuleName < rhs.ModuleName;
        return lhs.Sequence < rhs.Sequence;
    }

    void RuntimeModuleSchedule::Clear()
    {
        m_SimSystems.clear();
        m_FrameHooks.clear();
        m_NextRegistrationSequence = 0;
    }

    void RuntimeModuleSchedule::RegisterSimSystem(
        std::string moduleName,
        SimSystemDesc desc)
    {
        m_SimSystems.push_back(RuntimeModuleSimSystemRecord{
            .ModuleName = std::move(moduleName),
            .Desc = std::move(desc),
            .Sequence = m_NextRegistrationSequence++,
        });
    }

    void RuntimeModuleSchedule::RegisterFrameHook(
        std::string moduleName,
        const FramePhase phase,
        RuntimeFrameHook hook)
    {
        m_FrameHooks.push_back(RuntimeModuleFrameHookRecord{
            .ModuleName = std::move(moduleName),
            .Phase = phase,
            .Hook = std::move(hook),
            .Sequence = m_NextRegistrationSequence++,
        });
    }

    void RuntimeModuleSchedule::FinalizeForBoot()
    {
        std::sort(m_SimSystems.begin(),
                  m_SimSystems.end(),
                  SimSystemLess);

        const std::size_t count = m_SimSystems.size();
        std::vector<std::vector<std::size_t>> edges(count);
        std::vector<std::uint32_t> indegree(count, 0u);

        for (std::size_t waiter = 0; waiter < count; ++waiter)
        {
            for (const Core::Hash::StringID waitLabel :
                 m_SimSystems[waiter].Desc.WaitForSignals)
            {
                bool matched = false;
                for (std::size_t signaler = 0; signaler < count; ++signaler)
                {
                    if (signaler == waiter ||
                        !HasLabel(m_SimSystems[signaler].Desc.SignalLabels,
                                  waitLabel))
                    {
                        continue;
                    }

                    if (std::find(edges[signaler].begin(),
                                  edges[signaler].end(),
                                  waiter) == edges[signaler].end())
                    {
                        edges[signaler].push_back(waiter);
                        indegree[waiter] += 1u;
                    }
                    matched = true;
                }

                if (!matched)
                {
                    Core::Log::Error(
                        "[RuntimeModule] Sim system '{}.{}' waits for unprovided signal {}.",
                        m_SimSystems[waiter].ModuleName,
                        m_SimSystems[waiter].Desc.Name,
                        waitLabel.Value);
                    std::terminate();
                }
            }
        }

        std::vector<std::size_t> ready;
        ready.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (indegree[index] == 0u)
                ready.push_back(index);
        }

        const auto indexLess =
            [this](const std::size_t lhs,
                   const std::size_t rhs)
        {
            return SimSystemLess(m_SimSystems[lhs],
                                 m_SimSystems[rhs]);
        };
        std::sort(ready.begin(), ready.end(), indexLess);

        std::vector<std::size_t> order;
        order.reserve(count);
        while (!ready.empty())
        {
            const std::size_t current = ready.front();
            ready.erase(ready.begin());
            order.push_back(current);

            for (const std::size_t dependent : edges[current])
            {
                indegree[dependent] -= 1u;
                if (indegree[dependent] == 0u)
                {
                    ready.push_back(dependent);
                    std::sort(ready.begin(), ready.end(), indexLess);
                }
            }
        }

        if (order.size() != count)
        {
            Core::Log::Error(
                "[RuntimeModule] Sim system dependency cycle detected during boot.");
            std::terminate();
        }

        std::vector<RuntimeModuleSimSystemRecord> sorted;
        sorted.reserve(count);
        for (const std::size_t index : order)
            sorted.push_back(std::move(m_SimSystems[index]));
        m_SimSystems = std::move(sorted);

        std::sort(m_FrameHooks.begin(),
                  m_FrameHooks.end(),
                  FrameHookLess);
    }

    void RuntimeModuleSchedule::RegisterSimSystemsForTick(
        RuntimeModuleSimSystemScheduleContext context) const
    {
        for (const RuntimeModuleSimSystemRecord& record : m_SimSystems)
        {
            std::string passName = record.ModuleName;
            passName += ".";
            passName += record.Desc.Name;

            context.Graph.AddPass(
                passName,
                record.Desc.Options,
                [setup = record.Desc.Setup](Core::FrameGraphBuilder& builder)
                {
                    if (setup)
                        setup(builder);
                },
                [execute = record.Desc.Execute,
                 &activeWorld = context.ActiveWorld,
                 activeWorldHandle = context.ActiveWorldHandle,
                 &commands = context.Commands,
                 &events = context.Events,
                 &jobs = context.Jobs,
                 &worlds = context.Worlds,
                 &services = context.Services,
                 frameIndex = context.FrameIndex,
                 fixedDt = context.FixedDeltaSeconds]
                {
                    SimSystemContext simContext{
                        .ActiveWorld = activeWorld,
                        .ActiveWorldHandle = activeWorldHandle,
                        .Commands = commands,
                        .Events = events,
                        .Jobs = jobs,
                        .Worlds = worlds,
                        .Services = services,
                        .FrameIndex = frameIndex,
                        .FixedDeltaSeconds = fixedDt,
                    };
                    execute(simContext);
                });
        }
    }

    void RuntimeModuleSchedule::RunFrameHooks(
        RuntimeModuleFrameHookDispatchContext context) const
    {
        RuntimeFrameHookContext hookContext{
            .Phase = context.Phase,
            .ActiveWorld = context.ActiveWorld,
            .ActiveWorldHandle = context.ActiveWorldHandle,
            .Commands = context.Commands,
            .Events = context.Events,
            .Jobs = context.Jobs,
            .Worlds = context.Worlds,
            .Services = context.Services,
            .FrameIndex = context.FrameIndex,
            .FrameDeltaSeconds = context.FrameDeltaSeconds,
            .FixedStepAlpha = context.FixedStepAlpha,
        };

        for (const RuntimeModuleFrameHookRecord& hook : m_FrameHooks)
        {
            if (hook.Phase == context.Phase && hook.Hook)
                hook.Hook(hookContext);
        }
    }
}
