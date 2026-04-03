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
import Graphics.Components;
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

    auto& reg = m_Engine->GetSceneManager().GetScene().GetRegistry();
    context.HasSelection = context.Selected != entt::null && reg.valid(context.Selected);
    context.HasSurface = context.HasSelection
        && GetGeometryProcessingCapabilities(reg, context.Selected).HasEditableSurfaceMesh;
    context.HasGraph = context.HasSelection && reg.all_of<ECS::Graph::Data>(context.Selected);
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
        ImGui::TextDisabled("Selected entity does not expose an editable collider-backed surface mesh.");
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

void GeometryWorkflowController::OpenMeshQualityPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Mesh Quality", [this]() { DrawMeshQualityPanel(); });
    Interface::GUI::OpenPanel("Geometry - Mesh Quality");
}

void GeometryWorkflowController::OpenMeshSpectralPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Mesh Spectral", [this]() { DrawMeshSpectralPanel(); });
    Interface::GUI::OpenPanel("Geometry - Mesh Spectral");
}

void GeometryWorkflowController::OpenGraphSpectralPanel()
{
    Interface::GUI::RegisterPanel("Geometry - Graph Spectral", [this]() { DrawGraphSpectralPanel(); });
    Interface::GUI::OpenPanel("Geometry - Graph Spectral");
}

void GeometryWorkflowController::OpenWorkflowStack()
{
    OpenWorkflowPanel();
    OpenMeshSpectralPanel();
    OpenGraphSpectralPanel();
    OpenRemeshingPanel();
    OpenSimplificationPanel();
    OpenSmoothingPanel();
    OpenSubdivisionPanel();
    OpenRepairPanel();
    OpenMeshQualityPanel();
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

    if (ImGui::BeginMenu("Spectral"))
    {
        if (ImGui::MenuItem("Mesh Spectral"))
            OpenMeshSpectralPanel();
        if (ImGui::MenuItem("Graph Spectral"))
            OpenGraphSpectralPanel();
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

    if (ImGui::BeginMenu("Analysis"))
    {
        if (ImGui::MenuItem("Mesh Quality"))
            OpenMeshQualityPanel();
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
                                ? "Editable halfedge surface mesh detected. Geometry operators are available."
                                : "Selected entity does not expose an editable collider-backed surface mesh.");
    }
    else
    {
        ImGui::TextDisabled("Select an entity with a surface mesh to apply geometry operators.");
    }

    ImGui::SeparatorText("Open Panels");
    if (ImGui::Button("Open Workflow Stack"))
        OpenWorkflowStack();
    if (ImGui::Button("Open Mesh Spectral"))
        OpenMeshSpectralPanel();
    ImGui::SameLine();
    if (ImGui::Button("Open Graph Spectral"))
        OpenGraphSpectralPanel();
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
    ImGui::SameLine();
    if (ImGui::Button("Open Mesh Quality"))
        OpenMeshQualityPanel();

    ImGui::SeparatorText("Approach Map");
    ImGui::BulletText("Spectral: Mesh modes publish scalar vertex fields; graph spectral layout can publish properties or rewrite node positions.");
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

void GeometryWorkflowController::DrawMeshQualityPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Compute aggregate mesh quality diagnostics and per-metric histograms for angle, aspect ratio, edge length, valence, and area."))
    {
        static_cast<void>(DrawMeshQualityWidget(*m_Engine, context.Selected, m_MeshQualityUi));
    }
}

void GeometryWorkflowController::DrawMeshSpectralPanel()
{
    const auto context = GetSelectionContext();
    if (DrawOperatorPanelHeader(context,
                                "Compute low-frequency scalar modes of the cotangent Laplacian on the selected surface mesh. The published properties can be visualized immediately through the mesh vertex color source selector."))
    {
        static_cast<void>(DrawMeshSpectralWidget(*m_Engine, context.Selected, m_MeshSpectralUi));
    }
}

void GeometryWorkflowController::DrawGraphSpectralPanel()
{
    const auto context = GetSelectionContext();

    ImGui::TextWrapped(
        "Compute a 2D spectral embedding of the selected graph using the first non-constant Laplacian modes. Results can be published as per-vertex scalar properties or applied directly to graph node positions.");
    ImGui::Spacing();

    if (!context.HasSelection)
    {
        ImGui::TextDisabled("Select a graph entity to run spectral layout.");
        return;
    }

    ImGui::Text("Selected Entity: %u",
                static_cast<uint32_t>(static_cast<entt::id_type>(context.Selected)));
    if (!context.HasGraph)
    {
        ImGui::TextDisabled("Selected entity does not have a Graph component.");
        return;
    }

    ImGui::Separator();
    static_cast<void>(DrawGraphSpectralWidget(*m_Engine, context.Selected, m_GraphSpectralUi));
}

} // namespace Runtime::EditorUI
