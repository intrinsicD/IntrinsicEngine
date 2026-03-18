// Runtime.EditorUI.GeometryWorkflowController — Geometry operator UI state,
// panel drawing, menu dispatch, and the shared ApplyGeometryOperator pipeline
// (CPU mesh -> operator -> GPU re-upload).

module;

#include <string>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Graphics;
import Geometry;
import ECS;
import Interface;

namespace Runtime::EditorUI
{
void GeometryWorkflowController::Init(Runtime::Engine& engine, entt::entity& cachedSelected)
{
    m_Engine = &engine;
    m_CachedSelected = &cachedSelected;
}

void GeometryWorkflowController::RegisterPanelsAndMenu()
{
    Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); }, true, 0, false);
    Interface::GUI::RegisterMainMenuBar("Geometry", [this]() { DrawMenu(); });
}

void GeometryWorkflowController::OpenAlgorithmPanel(GeometryProcessingAlgorithm algorithm)
{
    switch (algorithm)
    {
    case GeometryProcessingAlgorithm::Remeshing:
        OpenRemeshingPanel();
        break;
    case GeometryProcessingAlgorithm::Simplification:
        OpenSimplificationPanel();
        break;
    case GeometryProcessingAlgorithm::Smoothing:
        OpenSmoothingPanel();
        break;
    case GeometryProcessingAlgorithm::Subdivision:
        OpenSubdivisionPanel();
        break;
    case GeometryProcessingAlgorithm::Repair:
        OpenRepairPanel();
        break;
    case GeometryProcessingAlgorithm::KMeans:
    default:
        OpenWorkflowPanel();
        break;
    }
}

// --- Selection context ---

GeometryWorkflowController::SelectionContext GeometryWorkflowController::GetSelectionContext() const
{
    SelectionContext context{};
    context.Selected = *m_CachedSelected;

    auto& reg = m_Engine->GetScene().GetRegistry();
    context.HasSelection = context.Selected != entt::null && reg.valid(context.Selected);
    context.HasSurface = context.HasSelection && reg.all_of<ECS::Surface::Component>(context.Selected);
    return context;
}

bool GeometryWorkflowController::DrawOperatorPanelHeader(const SelectionContext& context,
                                                          const char* description)
{
    if (description != nullptr && description[0] != '\0')
    {
        ImGui::TextWrapped("%s", description);
        ImGui::Spacing();
    }

    if (!context.HasSelection)
    {
        ImGui::TextDisabled("Select an entity to process geometry.");
        return false;
    }

    ImGui::Text("Selected Entity: %u",
                static_cast<uint32_t>(static_cast<entt::id_type>(context.Selected)));

    if (!context.HasSurface)
    {
        ImGui::TextDisabled("Selected entity does not have a Surface component.");
        return false;
    }

    ImGui::TextDisabled(
        "Operators apply in sequence to the selected surface mesh, so panels can be mixed into one workflow.");
    ImGui::Separator();
    return true;
}

// --- Panel openers ---

void GeometryWorkflowController::OpenWorkflowPanel()
{
    Interface::GUI::RegisterPanel("Geometry Workflow", [this]() { DrawWorkflowPanel(); });
    Interface::GUI::OpenPanel("Geometry Workflow");
}

void GeometryWorkflowController::OpenRemeshingPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Remeshing", [this]() { DrawRemeshingPanel(); });
    Interface::GUI::OpenPanel("Geometry - Remeshing");
}

void GeometryWorkflowController::OpenSimplificationPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Simplification", [this]() { DrawSimplificationPanel(); });
    Interface::GUI::OpenPanel("Geometry - Simplification");
}

void GeometryWorkflowController::OpenSmoothingPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Smoothing", [this]() { DrawSmoothingPanel(); });
    Interface::GUI::OpenPanel("Geometry - Smoothing");
}

void GeometryWorkflowController::OpenSubdivisionPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Subdivision", [this]() { DrawSubdivisionPanel(); });
    Interface::GUI::OpenPanel("Geometry - Subdivision");
}

void GeometryWorkflowController::OpenRepairPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Repair", [this]() { DrawRepairPanel(); });
    Interface::GUI::OpenPanel("Geometry - Repair");
}

void GeometryWorkflowController::OpenWorkflowStack()
{
    OpenWorkflowPanel();
    OpenRemeshingPanel();
    OpenSimplificationPanel();
    OpenSmoothingPanel();
    OpenSubdivisionPanel();
    OpenRepairPanel();
}

// --- Menu ---

void GeometryWorkflowController::DrawMenu()
{
    if (!ImGui::BeginMenu("Geometry"))
        return;

    if (ImGui::BeginMenu("Workflow"))
    {
        if (ImGui::MenuItem("Overview"))
            OpenWorkflowPanel();
        if (ImGui::MenuItem("Open Workflow Stack"))
            OpenWorkflowStack();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Remeshing"))
    {
        if (ImGui::MenuItem("Isotropic Remeshing"))
            OpenRemeshingPanel();
        if (ImGui::MenuItem("Adaptive Remeshing"))
            OpenRemeshingPanel();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Simplification"))
    {
        if (ImGui::MenuItem("QEM Simplification"))
            OpenSimplificationPanel();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Smoothing"))
    {
        if (ImGui::MenuItem("Uniform Laplacian"))
            OpenSmoothingPanel();
        if (ImGui::MenuItem("Cotan Laplacian"))
            OpenSmoothingPanel();
        if (ImGui::MenuItem("Taubin Smoothing"))
            OpenSmoothingPanel();
        if (ImGui::MenuItem("Implicit Smoothing"))
            OpenSmoothingPanel();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Subdivision"))
    {
        if (ImGui::MenuItem("Loop Subdivision"))
            OpenSubdivisionPanel();
        if (ImGui::MenuItem("Catmull-Clark Subdivision"))
            OpenSubdivisionPanel();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Repair"))
    {
        if (ImGui::MenuItem("Mesh Repair"))
            OpenRepairPanel();
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

// --- Panel draw methods ---

void GeometryWorkflowController::DrawWorkflowPanel()
{
    const auto context = GetSelectionContext();

    ImGui::TextWrapped(
        "Geometry tools are organized by workflow and algorithm family. Open only the panels you need, or open the full stack when chaining remeshing, smoothing, simplification, subdivision, and repair together.");
    ImGui::SeparatorText("Selection");
    if (context.HasSelection)
    {
        ImGui::Text("Selected Entity: %u",
                    static_cast<uint32_t>(static_cast<entt::id_type>(context.Selected)));
        ImGui::TextDisabled(context.HasSurface
                                ? "Surface mesh detected. Geometry operators are available."
                                : "Selected entity does not have a Surface component.");
    }
    else
    {
        ImGui::TextDisabled("Select an entity with a surface mesh to apply geometry operators.");
    }

    ImGui::SeparatorText("Open Panels");
    if (ImGui::Button("Open Workflow Stack"))
        OpenWorkflowStack();
    if (ImGui::Button("Open Remeshing"))
        OpenRemeshingPanel();
    ImGui::SameLine();
    if (ImGui::Button("Open Simplification"))
        OpenSimplificationPanel();
    ImGui::SameLine();
    if (ImGui::Button("Open Smoothing"))
        OpenSmoothingPanel();
    if (ImGui::Button("Open Subdivision"))
        OpenSubdivisionPanel();
    ImGui::SameLine();
    if (ImGui::Button("Open Repair"))
        OpenRepairPanel();

    ImGui::SeparatorText("Approach Map");
    ImGui::BulletText("Remeshing: Isotropic and Adaptive remeshing share the same workflow surface.");
    ImGui::BulletText(
        "Smoothing: Uniform, Cotan, Taubin, and Implicit smoothing stay grouped together for side-by-side comparison.");
    ImGui::BulletText("Simplification, Subdivision, and Repair remain focused single-purpose panels.");
    ImGui::BulletText(
        "Panels compose naturally because each operator rewrites the selected surface mesh in place.");
}

void GeometryWorkflowController::DrawRemeshingPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Use remeshing to regularize edge lengths. Isotropic remeshing targets a uniform metric; adaptive remeshing keeps room for size-field-driven workflows while still sharing the same mesh pipeline."))
    {
        static_cast<void>(DrawRemeshingWidget(*m_Engine, context.Selected, m_RemeshingUi));
    }
}

void GeometryWorkflowController::DrawSimplificationPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Simplification reduces triangle count while preserving overall shape. Keep this panel separate from remeshing and smoothing so decimation can be inserted wherever a workflow needs it."))
    {
        static_cast<void>(DrawSimplificationWidget(*m_Engine, context.Selected, m_SimplificationUi));
    }
}

void GeometryWorkflowController::DrawSmoothingPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Smoothing approaches stay together so you can compare differential operators without hunting through unrelated UI. They remain independently accessible from the Geometry menu and can still be chained after remeshing or before subdivision."))
    {
        static_cast<void>(DrawSmoothingWidget(*m_Engine, context.Selected, m_SmoothingUi));
    }
}

void GeometryWorkflowController::DrawSubdivisionPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Subdivision is kept distinct from smoothing and remeshing because it changes topology with a refinement-first workflow. Open it alongside repair or smoothing when building higher-resolution assets."))
    {
        static_cast<void>(DrawSubdivisionWidget(*m_Engine, context.Selected, m_SubdivisionUi));
    }
}

void GeometryWorkflowController::DrawRepairPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Repair stays as a standalone cleanup pass so it can be inserted before or after heavier operators without dragging the rest of the geometry UI along."))
    {
        static_cast<void>(DrawRepairWidget(*m_Engine, context.Selected));
    }
}

} // namespace Runtime::EditorUI
