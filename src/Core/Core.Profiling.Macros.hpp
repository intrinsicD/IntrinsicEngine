#pragma once

import Core;

// Macro for easy usage
#define PROFILE_SCOPE(name) Core::Profiling::ScopedTimer timer##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__func__)