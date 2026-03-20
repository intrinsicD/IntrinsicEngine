export module Graphics;

export import Graphics.Camera;
export import Graphics.Geometry;
export import Graphics.Material;
export import Graphics.MaterialSystem;
export import Graphics.Model;
export import Graphics.ModelLoader;
export import Graphics.RenderSystem;
export import Graphics.RenderPipeline;
export import Graphics.TextureLoader;
export import Graphics.RenderGraph;
export import Graphics.Components;
export import Graphics.AssetErrors;
export import Graphics.IORegistry;
export import Graphics.ShaderRegistry;
export import Graphics.PipelineLibrary;
export import Graphics.FeatureCatalog;
export import Graphics.Pipelines;
export import Graphics.GPUScene;
export import Graphics.GpuColor;
export import Graphics.Systems.GPUSceneSync;
export import Graphics.Systems.MeshRendererLifecycle;
export import Graphics.Systems.PrimitiveBVHSync;
export import Graphics.Systems.GraphGeometrySync;
export import Graphics.Systems.PointCloudGeometrySync;
export import Graphics.Systems.MeshViewLifecycle;
export import Graphics.Systems.PropertySetDirtySync;
export import Graphics.DebugDraw;
export import Graphics.OctreeDebugDraw;
export import Graphics.BoundingDebugDraw;
export import Graphics.KDTreeDebugDraw;
export import Graphics.BVHDebugDraw;
export import Graphics.ConvexHullDebugDraw;
export import Graphics.TransformGizmo;
export import Graphics.Colormap;
export import Graphics.VisualizationConfig;
export import Graphics.PropertyEnumerator;
export import Graphics.ColorMapper;
export import Graphics.VectorFieldManager;
export import Graphics.IsolineExtractor;
export import Graphics.Passes.SelectionOutline;
export import Graphics.Passes.PostProcess;
export import Graphics.Passes.PostProcessSettings;
export import Graphics.Passes.SelectionOutlineSettings;

// NOTE: We intentionally do NOT re-export pipeline implementations here.
// Import `Graphics.Pipelines` explicitly in apps/tools when you need DefaultPipeline/etc.

// Feature modules exported for custom pipeline construction.
// The three primary rendering passes compose the full scene visualization stack.
export import Graphics.Passes.Picking;
export import Graphics.Passes.Surface;
export import Graphics.Passes.Line;
export import Graphics.Passes.Point;
export import Graphics.Passes.DebugView;
export import Graphics.Passes.HtexPatchPreview;
export import Graphics.Passes.ImGui;
