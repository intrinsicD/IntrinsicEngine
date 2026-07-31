module;

module Extrinsic.Runtime.SandboxEditorFacades;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.SelectionController;

namespace Extrinsic::Runtime
{
    RuntimeInputActionDesc MakeSandboxDefaultFocusInputAction(
        CameraControllerRegistry& cameraControllers,
        SelectionController& selection)
    {
        return RuntimeInputActionDesc{
            .DebugName = "Sandbox.DefaultFocusCameraOnSelection",
            .Binding =
                RuntimeInputActionBinding{
                    .KeyCode = Platform::Input::Key::F,
                    .Trigger = RuntimeInputActionTrigger::KeyJustPressed,
                    .SuppressWhenImGuiCapturesKeyboard = true,
                },
            .Execute =
                [camera = &cameraControllers, selection = &selection](
                    const RuntimeInputActionContext& context,
                    RuntimeInputActionServices& services)
                {
                    if (services.Scene == nullptr ||
                        services.RenderInput == nullptr ||
                        services.Config == nullptr)
                    {
                        return Core::Err(Core::ErrorCode::InvalidState);
                    }

                    if (!services.Config->Camera.Enabled)
                        return Core::Ok();

                    if (!FocusCameraOnSelection(
                            *camera,
                            *selection,
                            *services.Scene,
                            CameraControllerSlot::Main))
                    {
                        return Core::Ok();
                    }

                    if (ICameraController* focused =
                            camera->ResolveOrNull(CameraControllerSlot::Main))
                    {
                        services.RenderInput->Camera =
                            focused->GetView(context.Viewport);
                        services.RenderInput->Camera.ExplicitCameraTransition =
                            camera->ConsumeCameraTransition(
                                CameraControllerSlot::Main);
                    }
                    return Core::Ok();
                },
        };
    }
}
