module;

export module Graphics.FeatureCatalog;

import Core.FeatureRegistry;

export namespace Graphics::FeatureCatalog
{
    using Core::MakeFeatureDescriptor;

    inline constexpr Core::FeatureDescriptor PickingPass = MakeFeatureDescriptor(
        "PickingPass",
        Core::FeatureCategory::RenderFeature,
        "Entity ID picking for mouse selection");

    inline constexpr Core::FeatureDescriptor SurfacePass = MakeFeatureDescriptor(
        "SurfacePass",
        Core::FeatureCategory::RenderFeature,
        "Main surface PBR rendering pass");

    inline constexpr Core::FeatureDescriptor ShadowPass = MakeFeatureDescriptor(
        "ShadowPass",
        Core::FeatureCategory::RenderFeature,
        "CSM shadow-atlas depth pass");

    inline constexpr Core::FeatureDescriptor SelectionOutlinePass = MakeFeatureDescriptor(
        "SelectionOutlinePass",
        Core::FeatureCategory::RenderFeature,
        "Selection outline overlay for selected/hovered entities");

    inline constexpr Core::FeatureDescriptor LinePass = MakeFeatureDescriptor(
        "LinePass",
        Core::FeatureCategory::RenderFeature,
        "Unified BDA line rendering (retained wireframe/graph edges + transient DebugDraw)");

    inline constexpr Core::FeatureDescriptor PointPass = MakeFeatureDescriptor(
        "PointPass",
        Core::FeatureCategory::RenderFeature,
        "Unified BDA point rendering (retained points/nodes/vertices + transient DebugDraw)");

    inline constexpr Core::FeatureDescriptor PostProcessPass = MakeFeatureDescriptor(
        "PostProcessPass",
        Core::FeatureCategory::RenderFeature,
        "Bloom + HDR tone mapping (ACES/Reinhard/Uncharted2) + optional FXAA");

    inline constexpr Core::FeatureDescriptor DebugViewPass = MakeFeatureDescriptor(
        "DebugViewPass",
        Core::FeatureCategory::RenderFeature,
        "Render target debug visualization");

    inline constexpr Core::FeatureDescriptor HtexPatchPreviewPass = MakeFeatureDescriptor(
        "HtexPatchPreviewPass",
        Core::FeatureCategory::RenderFeature,
        "Float-only Htex-style halfedge patch preview scalar atlas");

    inline constexpr Core::FeatureDescriptor ImGuiPass = MakeFeatureDescriptor(
        "ImGuiPass",
        Core::FeatureCategory::RenderFeature,
        "ImGui UI overlay");

    inline constexpr Core::FeatureDescriptor DeferredLighting = MakeFeatureDescriptor(
        "DeferredLighting",
        Core::FeatureCategory::RenderFeature,
        "Deferred lighting path (G-buffer + fullscreen composition)",
        false);

    inline constexpr Core::FeatureDescriptor DepthPrepass = MakeFeatureDescriptor(
        "DepthPrepass",
        Core::FeatureCategory::RenderFeature,
        "Depth-only early-Z prepass for efficient shading and shadow mapping foundation");
}
