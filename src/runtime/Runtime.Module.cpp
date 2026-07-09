module;

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
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

    void ModuleRegistrationSink::ApplySimSystems(Core::FrameGraph&     graph,
                                                 ECS::Scene::Registry& scene) const
    {
        // Each SimSystemDesc becomes a FrameGraph pass. `Declare` runs at
        // setup to lay down Read/Write tokens + named signals; `Execute` runs
        // when the graph fires. The pass callbacks bind to the desc through a
        // stable pointer into m_SimSystems and to the caller's `scene`; both
        // outlive the per-substep graph (the sink is engine-owned and the
        // graph is compiled/executed within the same frame), so the captures
        // stay valid.
        for (const SimSystemDesc& system : m_SimSystems)
        {
            const SimSystemDesc* const desc = &system;
            graph.AddPass(
                desc->PassName,
                [desc](Core::FrameGraphBuilder& builder)
                {
                    if (desc->Declare)
                        desc->Declare(builder);
                },
                [desc, &scene]
                {
                    if (desc->Execute)
                        desc->Execute(scene);
                });
        }
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
