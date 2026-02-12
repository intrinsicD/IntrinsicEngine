export module Core;

// Re-export all sub-systems so the user only needs 'import Core;'
export import Core.Error;
export import Core.Memory;
export import Core.Logging;
export import Core.Tasks;
export import Core.Window;
export import Core.Filesystem;
export import Core.Input;
export import Core.Assets;
export import Core.Profiling;
export import Core.Telemetry;
export import Core.ResourcePool;
export import Core.Hash;
export import Core.Handle;
export import Core.FrameGraph;
export import Core.DAGScheduler;
export import Core.FeatureRegistry;
