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
import Core.Logging;
import Core.IOBackend;
import Runtime.Selection;
import Runtime.SelectionModule;
import Runtime.SceneSerializer;

namespace Runtime::EditorUI
{
    using namespace Core::Hash;

    // File-local scene dirty tracker (one per process).
    static SceneDirtyTracker s_DirtyTracker;

    SceneDirtyTracker& GetSceneDirtyTracker()
    {
        return s_DirtyTracker;
    }

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

    static void RegisterSceneFileMenu(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterMainMenuBar("File", [&engine]
        {
            if (ImGui::BeginMenu("File"))
            {
                // ---- Save Scene ----
                if (ImGui::MenuItem("Save Scene"))
                {
                    ImGui::OpenPopup("SaveScenePopup");
                }

                // ---- Save Scene As... ----
                if (ImGui::MenuItem("Save Scene As..."))
                {
                    ImGui::OpenPopup("SaveSceneAsPopup");
                }

                // ---- Load Scene ----
                if (ImGui::MenuItem("Load Scene"))
                {
                    ImGui::OpenPopup("LoadScenePopup");
                }

                ImGui::EndMenu();
            }

            // ---- Save Scene popup (use current path or ask) ----
            if (ImGui::BeginPopupModal("SaveScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto& tracker = GetSceneDirtyTracker();
                static char savePath[512] = "";

                // Pre-fill with current path if empty
                if (savePath[0] == '\0' && !tracker.GetCurrentPath().empty())
                {
                    std::snprintf(savePath, sizeof(savePath), "%s", tracker.GetCurrentPath().c_str());
                }

                if (savePath[0] == '\0')
                {
                    std::snprintf(savePath, sizeof(savePath), "scene.json");
                }

                ImGui::Text("Save scene to:");
                ImGui::InputText("##savepath", savePath, sizeof(savePath));

                if (ImGui::Button("Save", ImVec2(120, 0)))
                {
                    auto result = SaveScene(engine, std::string(savePath), engine.GetIOBackend());
                    if (result)
                    {
                        tracker.SetCurrentPath(savePath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene saved to: {}", savePath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // ---- Save As popup ----
            if (ImGui::BeginPopupModal("SaveSceneAsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static char saveAsPath[512] = "scene.json";

                ImGui::Text("Save scene to:");
                ImGui::InputText("##saveaspath", saveAsPath, sizeof(saveAsPath));

                if (ImGui::Button("Save", ImVec2(120, 0)))
                {
                    auto result = SaveScene(engine, std::string(saveAsPath), engine.GetIOBackend());
                    if (result)
                    {
                        auto& tracker = GetSceneDirtyTracker();
                        tracker.SetCurrentPath(saveAsPath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene saved to: {}", saveAsPath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // ---- Load Scene popup ----
            if (ImGui::BeginPopupModal("LoadScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static char loadPath[512] = "scene.json";

                // Dirty-state warning
                auto& tracker = GetSceneDirtyTracker();
                if (tracker.IsDirty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                        "Warning: unsaved changes will be lost.");
                    ImGui::Separator();
                }

                ImGui::Text("Load scene from:");
                ImGui::InputText("##loadpath", loadPath, sizeof(loadPath));

                if (ImGui::Button("Load", ImVec2(120, 0)))
                {
                    auto result = LoadScene(engine, std::string(loadPath), engine.GetIOBackend());
                    if (result)
                    {
                        tracker.SetCurrentPath(loadPath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene loaded: {} entities, {} assets, {} failed",
                                        result->EntitiesLoaded, result->AssetsLoaded, result->AssetsFailed);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        });
    }

    void RegisterDefaultPanels(Runtime::Engine& engine)
    {
        RegisterFeatureBrowserPanel(engine);
        RegisterFrameGraphInspectorPanel(engine);
        RegisterSelectionPanel(engine);
        RegisterSceneFileMenu(engine);
    }
}
