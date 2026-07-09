module;

#include <optional>

export module Extrinsic.Runtime.ReferenceSceneControl;

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.ReferenceScene;

namespace Extrinsic::Runtime
{
    export class ReferenceSceneControl final
    {
    public:
        [[nodiscard]] ReferenceSceneRegistry& Registry() noexcept;
        [[nodiscard]] const ReferenceSceneRegistry& Registry() const noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] const std::optional<Graphics::CameraViewInput>& CameraSeed() const noexcept;

        void InstallIfEnabled(const Core::Config::ReferenceSceneConfig& config,
                              ECS::Scene::Registry& scene);
        void TeardownIfInstalled(const Core::Config::ReferenceSceneConfig& config,
                                 ECS::Scene::Registry* scene);

    private:
        ReferenceSceneRegistry m_Registry{};
        ReferenceScenePopulation m_Population{};
        std::optional<Graphics::CameraViewInput> m_CameraSeed{};
        bool m_Installed{false};
    };
}
