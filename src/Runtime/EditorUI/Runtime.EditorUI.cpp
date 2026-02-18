module;

#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>

module Runtime.EditorUI;

import Interface;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.Hash;
import Runtime.Selection;
import Runtime.SelectionModule;

namespace Runtime::EditorUI
{
    using namespace Core::Hash;

    static void RegisterFeatureBrowserPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Features", [&engine]()
        {
            auto& reg = engine.GetFeatureRegistry();

            static int cat = 0;
            const char* cats[] = {"RenderFeature", "System", "Panel", "GeometryOperator"};

            ImGui::TextDisabled("Feature Registry");
            ImGui::Separator();
            ImGui::Combo("Category", &cat, cats, IM_ARRAYSIZE(cats));

            Core::FeatureCategory category = Core::FeatureCategory::RenderFeature;
            switch (cat)
            {
            case 0: category = Core::FeatureCategory::RenderFeature; break;
            case 1: category = Core::FeatureCategory::System; break;
            case 2: category = Core::FeatureCategory::Panel; break;
            case 3: category = Core::FeatureCategory::GeometryOperator; break;
            default: break;
            }

            const auto list = reg.GetByCategory(category);

            if (ImGui::BeginTable("##features", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto* info : list)
                {
                    if (!info) continue;

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    bool enabled = reg.IsEnabled(info->Id);
                    const std::string chkId = std::string("##en_") + info->Name;
                    if (ImGui::Checkbox(chkId.c_str(), &enabled))
                        reg.SetEnabled(info->Id, enabled);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info->Name.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", info->Description.c_str());
                }

                ImGui::EndTable();
            }
        });

        // Add a menu entry to open it.
        Interface::GUI::RegisterMainMenuBar("Tools", []
        {
            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem("Features"))
                {
                    // Panel menu is global; toggling is handled there.
                }
                ImGui::EndMenu();
            }
        });
    }

    static void RegisterFrameGraphInspectorPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Frame Graph", [&engine]()
        {
            const auto& fg = engine.GetRenderOrchestrator().GetFrameGraph();
            const auto& layers = fg.GetExecutionLayers();

            ImGui::Text("Passes: %u", fg.GetPassCount());
            ImGui::Text("Layers: %u", (std::uint32_t)layers.size());
            ImGui::Separator();

            for (uint32_t li = 0; li < (uint32_t)layers.size(); ++li)
            {
                const auto& layer = layers[li];
                const std::string label = "Layer " + std::to_string(li) + " (" + std::to_string(layer.size()) + " passes)";
                if (ImGui::TreeNode(label.c_str()))
                {
                    for (uint32_t passIndex : layer)
                    {
                        ImGui::BulletText("%s", std::string(fg.GetPassName(passIndex)).c_str());
                    }
                    ImGui::TreePop();
                }
            }
        });
    }

    static void RegisterSelectionPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Selection", [&engine]()
        {
            auto& sel = engine.GetSelection();
            auto& cfg = sel.GetConfig();

            ImGui::SeparatorText("Config");
            ImGui::SliderInt("Mouse Button", &cfg.MouseButton, 0, 2);

            const char* backends[] = {"CPU", "GPU"};
            int backend = (cfg.Backend == Runtime::Selection::PickBackend::GPU) ? 1 : 0;
            if (ImGui::Combo("Pick Backend", &backend, backends, IM_ARRAYSIZE(backends)))
                cfg.Backend = backend == 1 ? Runtime::Selection::PickBackend::GPU : Runtime::Selection::PickBackend::CPU;

            const char* modes[] = {"Replace", "Add", "Toggle"};
            int mode = 0;
            switch (cfg.Mode)
            {
            case Runtime::Selection::PickMode::Replace: mode = 0; break;
            case Runtime::Selection::PickMode::Add: mode = 1; break;
            case Runtime::Selection::PickMode::Toggle: mode = 2; break;
            }
            if (ImGui::Combo("Default Mode (unused on click)", &mode, modes, IM_ARRAYSIZE(modes)))
            {
                cfg.Mode = (mode == 1) ? Runtime::Selection::PickMode::Add
                    : (mode == 2)       ? Runtime::Selection::PickMode::Toggle
                                        : Runtime::Selection::PickMode::Replace;
            }

            bool active = (cfg.Active == Runtime::SelectionModule::Activation::Enabled);
            if (ImGui::Checkbox("Active", &active))
                cfg.Active = active ? Runtime::SelectionModule::Activation::Enabled : Runtime::SelectionModule::Activation::Disabled;

            ImGui::SeparatorText("State");
            const entt::entity selected = sel.GetSelectedEntity(engine.GetScene());
            ImGui::Text("Selected entity: %u", (uint32_t)selected);

            if (ImGui::Button("Clear Selection"))
                sel.ClearSelection(engine.GetScene());
        });
    }

    void RegisterDefaultPanels(Runtime::Engine& engine)
    {
        RegisterFeatureBrowserPanel(engine);
        RegisterFrameGraphInspectorPanel(engine);
        RegisterSelectionPanel(engine);
    }
}

