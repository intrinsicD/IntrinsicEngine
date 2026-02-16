export module Graphics;

export import :Camera;
export import :Geometry;
export import :Material;
export import :MaterialSystem;
export import :Model;
export import :ModelLoader;
export import :RenderSystem;
export import :TextureLoader;
export import :RenderGraph;
export import :Components;
export import :AssetErrors;
export import :IORegistry;
export import :ShaderRegistry;
export import :PipelineLibrary;
export import :Pipelines;
export import :GPUScene;
export import :Systems.GPUSceneSync;
export import :Systems.MeshRendererLifecycle;
export import :DebugDraw;
export import :OctreeDebugDraw;
export import :BoundingDebugDraw;

// NOTE: We intentionally do NOT re-export pipeline implementations here.
// Import `Graphics.Pipelines` explicitly in apps/tools when you need DefaultPipeline/etc.

// Feature modules still exported for now (useful for custom pipelines)
export import :Passes.Picking;
export import :Passes.Forward;
export import :Passes.Line;
export import :Passes.DebugView;
export import :Passes.ImGui;
