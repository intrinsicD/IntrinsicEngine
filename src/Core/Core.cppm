export module Core;

// Re-export all sub-systems so the user only needs 'import Core;'
export import :Error;
export import :Memory;
export import :Logging;
export import :Tasks;
export import :Window;
export import :Filesystem;
export import :Input;
export import :Assets;
export import :Profiling;
export import :Telemetry;
export import :ResourcePool;
export import :Hash;
export import :Handle;
export import :FrameGraph;
