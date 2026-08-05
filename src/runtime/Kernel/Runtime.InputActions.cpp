module;

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

module Extrinsic.Runtime.InputActions;

import Extrinsic.Core.Error;
import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool RuntimeInputActionTriggered(
            const RuntimeInputActionDesc& desc,
            const Platform::Input::Context& input,
            const bool imguiCapturesKeyboard) noexcept
        {
            if (desc.Binding.SuppressWhenImGuiCapturesKeyboard &&
                imguiCapturesKeyboard)
            {
                return false;
            }

            switch (desc.Binding.Trigger)
            {
            case RuntimeInputActionTrigger::KeyJustPressed:
                return input.IsKeyJustPressed(desc.Binding.KeyCode);
            }
            return false;
        }
    }

    RuntimeInputActionHandle RuntimeInputActionRegistry::Register(
        RuntimeInputActionDesc desc)
    {
        if (!desc.Execute || desc.Binding.KeyCode < 0)
            return {};

        RuntimeInputActionRecord action{};
        action.Handle = RuntimeInputActionHandle{m_NextHandle++};
        action.Desc = std::move(desc);
        m_Actions.push_back(std::move(action));
        return m_Actions.back().Handle;
    }

    void RuntimeInputActionRegistry::Unregister(
        const RuntimeInputActionHandle handle)
    {
        if (!handle.IsValid())
            return;

        std::erase_if(
            m_Actions,
            [handle](const RuntimeInputActionRecord& action) noexcept
            {
                return action.Handle == handle;
            });
    }

    void RuntimeInputActionRegistry::DispatchForFrame(
        const Core::Config::EngineConfig& config,
        ECS::Scene::Registry& scene,
        const Platform::Input::Context& input,
        const Platform::Extent2D& viewport,
        const bool imguiCapturesKeyboard,
        const double frameDt,
        const std::uint64_t frameIndex,
        Graphics::RenderFrameInput& renderInput)
    {
        RuntimeInputActionServices services{
            .Scene = &scene,
            .RenderInput = &renderInput,
            .Config = &config,
        };

        for (const RuntimeInputActionRecord& action : m_Actions)
        {
            if (!action.Desc.Execute ||
                !RuntimeInputActionTriggered(
                    action.Desc,
                    input,
                    imguiCapturesKeyboard))
            {
                continue;
            }

            Core::Result result = action.Desc.Execute(
                RuntimeInputActionContext{
                    .Binding = action.Desc.Binding,
                    .Viewport = viewport,
                    .FrameDeltaSeconds = frameDt,
                    .FrameIndex = frameIndex,
                    .ImGuiCapturesKeyboard = imguiCapturesKeyboard,
                },
                services);
            if (!result.has_value())
            {
                Core::Log::Warn(
                    "[Runtime] Input action '{}' failed: key={} error={}",
                    action.Desc.DebugName.empty()
                        ? "<unnamed>"
                        : action.Desc.DebugName,
                    action.Desc.Binding.KeyCode,
                    Core::Error::ToString(result.error()));
            }
        }
    }
}
