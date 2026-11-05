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

    // A window's edited copy follows the active section whenever that changes (also through config_apply), so it
    // never shows or applies settings older than the active ones; a missing or invalid section leaves no draft, and
    // the next valid one is taken again. `seen` is the serialized section the draft came from; a window that applies
    // its draft stores the draft's serialization there. True when the draft was replaced or dropped.
    template <const auto& Section>
    bool FollowPluginSection(const std::optional<DerivedSectionConfig<Section>>& active,
                             std::optional<DerivedSectionConfig<Section>>& draft, std::string& seen)
    {
        if (!active)
        {
            const bool had = draft.has_value();
            draft.reset();
            seen.clear();
            return had;
        }
        auto serialized = SerializeDerivedSection<Section>(*active);
        if (serialized == seen) return false;
        draft = active;
        seen = std::move(serialized);
        return true;
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
