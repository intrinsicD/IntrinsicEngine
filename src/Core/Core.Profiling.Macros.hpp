#pragma once

// Core.Profiling.Macros.hpp — Profiling macros for the global module fragment.
//
// Include this in .cpp global module fragments (before `module Foo;`) to get
// PROFILE_SCOPE / PROFILE_FUNCTION macros. These record timing samples via
// the Core::Telemetry::TelemetrySystem singleton.
//
// Requires: `import Core.Telemetry;` in the module purview.

import Core.Telemetry;

#define PROFILE_SCOPE(name) \
    static constexpr uint32_t _profileHash##__LINE__ = Core::Telemetry::HashString(name); \
    Core::Telemetry::ScopedTimer _profileTimer##__LINE__(name, _profileHash##__LINE__)

#define PROFILE_FUNCTION() PROFILE_SCOPE(__func__)
