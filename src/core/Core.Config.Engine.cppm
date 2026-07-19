module;

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Core.Config.Engine;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Simulation;
import Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
        // Selector for app-owned initial reference content. Applications such
        // as Sandbox interpret ReferenceSceneConfig through the plain
        // Extrinsic.Runtime.ReferenceScene bootstrap functions; generic Engine
        // deliberately does not. Core carries only the value-type enum so
        // EngineConfig stays free of runtime/graphics imports.
        export enum class ReferenceSceneSelector : std::uint32_t
        {
            Triangle = 0,
        };

        export struct ReferenceSceneConfig
        {
            bool Enabled{false};
            ReferenceSceneSelector Selector{ReferenceSceneSelector::Triangle};
        };

        export enum class CameraControllerKind : std::uint32_t
        {
            Orbit = 0,
            Fly = 1,
            FreeLook = 2,
            TopDown = 3,
        };

        export struct CameraConfig
        {
            bool Enabled{true};
            CameraControllerKind Controller{CameraControllerKind::Orbit};
        };

        export struct EngineConfigSection
        {
            std::string Name{};
            std::string SchemaId{};
            std::uint32_t SchemaVersion{0u};
            std::string PayloadJson{"{}"};

            [[nodiscard]] friend bool operator==(
                const EngineConfigSection&,
                const EngineConfigSection&) noexcept = default;
        };

        export [[nodiscard]] inline EngineConfigSection* FindEngineConfigSection(
            std::vector<EngineConfigSection>& sections,
            const std::string_view name) noexcept
        {
            const auto it = std::lower_bound(
                sections.begin(),
                sections.end(),
                name,
                [](const EngineConfigSection& section, const std::string_view key)
                {
                    return section.Name < key;
                });
            return it != sections.end() && it->Name == name ? &*it : nullptr;
        }

        export [[nodiscard]] inline const EngineConfigSection* FindEngineConfigSection(
            const std::vector<EngineConfigSection>& sections,
            const std::string_view name) noexcept
        {
            const auto it = std::lower_bound(
                sections.begin(),
                sections.end(),
                name,
                [](const EngineConfigSection& section, const std::string_view key)
                {
                    return section.Name < key;
                });
            return it != sections.end() && it->Name == name ? &*it : nullptr;
        }

        export inline void UpsertEngineConfigSection(
            std::vector<EngineConfigSection>& sections,
            EngineConfigSection section)
        {
            const auto it = std::lower_bound(
                sections.begin(),
                sections.end(),
                section.Name,
                [](const EngineConfigSection& current, const std::string_view key)
                {
                    return current.Name < key;
                });
            if (it != sections.end() && it->Name == section.Name)
            {
                *it = std::move(section);
                return;
            }
            sections.insert(it, std::move(section));
        }

        export struct EngineConfig
        {
            RenderConfig    Render;
            SimulationConfig Simulation;
            WindowConfig    Window;
            ReferenceSceneConfig ReferenceScene;
            CameraConfig Camera;
            std::vector<EngineConfigSection> AppSections{};
        };

}
