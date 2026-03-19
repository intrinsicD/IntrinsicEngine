export module Graphics;

export import :Camera;
export import :Geometry;
export import :Material;
export import :MaterialSystem;
export import :Model;
export import :ModelLoader;
export import :RenderSystem;
export import :RenderPipeline;
export import :TextureLoader;
export import :RenderGraph;
export import :Components;
export import :AssetErrors;
export import :IORegistry;
export import :ShaderRegistry;
export import :PipelineLibrary;
export import :FeatureCatalog;
export import :Pipelines;
export import :GPUScene;
export import :GpuColor;
export import :Systems.GPUSceneSync;
export import :Systems.MeshRendererLifecycle;
export import :Systems.PrimitiveBVHSync;
export import :Systems.GraphGeometrySync;
export import :Systems.PointCloudGeometrySync;
export import :Systems.MeshViewLifecycle;
export import :Systems.PropertySetDirtySync;
export import :DebugDraw;
export import :OctreeDebugDraw;
export import :BoundingDebugDraw;
export import :KDTreeDebugDraw;
export import :BVHDebugDraw;
export import :ConvexHullDebugDraw;
export import :TransformGizmo;
export import :Colormap;
export import :VisualizationConfig;
export import :PropertyEnumerator;
export import :ColorMapper;
export import :VectorFieldManager;
export import :IsolineExtractor;
export import :Passes.SelectionOutline;
export import :Passes.PostProcess;
export import :Passes.PostProcessSettings;
export import :Passes.SelectionOutlineSettings;

// NOTE: We intentionally do NOT re-export pipeline implementations here.
// Import `Graphics.Pipelines` explicitly in apps/tools when you need DefaultPipeline/etc.

// Feature modules exported for custom pipeline construction.
// The three primary rendering passes compose the full scene visualization stack.
export import :Passes.Picking;
export import :Passes.Surface;
export import :Passes.Line;
export import :Passes.Point;
export import :Passes.DebugView;
export import :Passes.HtexPatchPreview;
export import :Passes.ImGui;
