module;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.InputActions;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.SelectionController;

namespace Extrinsic::Runtime
{
    export enum class RuntimeInputActionTrigger : std::uint8_t
    {
        KeyJustPressed = 0,
    };

    export struct RuntimeInputActionBinding
    {
        int KeyCode{-1};
        RuntimeInputActionTrigger Trigger{
            RuntimeInputActionTrigger::KeyJustPressed};
        bool SuppressWhenImGuiCapturesKeyboard{true};
    };

    export struct RuntimeInputActionHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeInputActionHandle,
            RuntimeInputActionHandle) noexcept = default;
    };

    export struct RuntimeInputActionContext
    {
        RuntimeInputActionBinding Binding{};
        Core::Extent2D Viewport{};
        double FrameDeltaSeconds{0.0};
        std::uint64_t FrameIndex{0};
        bool ImGuiCapturesKeyboard{false};
    };

    export struct RuntimeInputActionServices
    {
        ECS::Scene::Registry*           Scene{nullptr};
        SelectionController*            Selection{nullptr};
        Graphics::RenderFrameInput*     RenderInput{nullptr};
        const Core::Config::EngineConfig* Config{nullptr};
    };

    export struct RuntimeInputActionDesc
    {
        std::string DebugName{};
        RuntimeInputActionBinding Binding{};
        std::function<Core::Result(
            const RuntimeInputActionContext&,
            RuntimeInputActionServices&)> Execute{};
    };

    struct RuntimeInputActionRecord
    {
        RuntimeInputActionHandle Handle{};
        RuntimeInputActionDesc Desc{};
    };

    export class RuntimeInputActionRegistry final
    {
    public:
        [[nodiscard]] RuntimeInputActionHandle Register(
            RuntimeInputActionDesc desc);
        void Unregister(RuntimeInputActionHandle handle);

        void DispatchForFrame(const Core::Config::EngineConfig& config,
                              SelectionController& selection,
                              ECS::Scene::Registry& scene,
                              const Platform::Input::Context& input,
                              const Platform::Extent2D& viewport,
                              bool imguiCapturesKeyboard,
                              double frameDt,
                              std::uint64_t frameIndex,
                              Graphics::RenderFrameInput& renderInput);

    private:
        std::vector<RuntimeInputActionRecord> m_Actions{};
        std::uint64_t m_NextHandle{1u};
    };
}
