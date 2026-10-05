// Extrinsic.Core.Memory — arena-based allocation primitives: LinearArena and
// the ArenaAllocator adapter for standard containers.
// Allocation telemetry lives in Extrinsic.Core.Telemetry (Alloc namespace).
module;

export module Extrinsic.Core.Memory;

export import :Common;
export import :LinearArena;
export import :Polymorphic;
