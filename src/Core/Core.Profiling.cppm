// Core.Profiling — Re-exports Core.Telemetry profiling primitives.
//
// The canonical profiling types (ScopedTimer, TelemetrySystem, PROFILE_SCOPE
// macros) live in Core.Telemetry. This module exists as a convenience alias
// so consumers can `import Core.Profiling;` without depending on the full
// Telemetry interface.
//
// Historical note: this module previously defined its own ScopedTimer that
// duplicated Core.Telemetry::ScopedTimer. Consolidated to a single
// implementation to avoid redundancy.

export module Core.Profiling;

export import Core.Telemetry;
