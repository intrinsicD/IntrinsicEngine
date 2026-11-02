// ARCH-022: a plugin's derived config section through the host's config control: its active value, and a change
// applied as every config edit is (preview, which validates the whole document, then the hot subset).
module;

#include <optional>
#include <string>

export module Extrinsic.Runtime.PluginSections;

export import Extrinsic.Runtime.DerivedConfigSections;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.PluginContract;

export namespace Extrinsic::Runtime
{
    // Nullopt without config control or with an unusable payload.
    template <const auto& Section>
    [[nodiscard]] std::optional<DerivedSectionConfig<Section>> ActivePluginSection()
    {
        auto* control = PluginService<EngineConfigControl>();
        if (control == nullptr) return std::nullopt;
        return GetDerivedSection<Section>(control->GetEngineConfigControlState().ActiveConfig);
    }

    // False with the reason when config control is missing or the settings are rejected.
    template <const auto& Section>
    [[nodiscard]] bool ApplyPluginSection(const DerivedSectionConfig<Section>& value, std::string& error)
    {
        auto* control = PluginService<EngineConfigControl>();
        if (control == nullptr)
        {
            error = "Config control is unavailable.";
            return false;
        }
        auto candidate = control->GetEngineConfigControlState().ActiveConfig;
        SetDerivedSection<Section>(candidate, value);
        const auto preview = control->PreviewEngineConfigControlDocument(
            Core::Config::SerializeEngineConfig(candidate), std::string{Section.Name});
        if (!Core::Config::IsConfigUsable(preview))
        {
            error = preview.Diagnostics.empty() ? "The settings were rejected." : preview.Diagnostics.front().Message;
            return false;
        }
        if (!control->ApplyEngineConfigHotSubset(preview).Succeeded())
        {
            error = "The settings could not be applied.";
            return false;
        }
        return true;
    }
}
