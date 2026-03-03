module;

module Graphics:Passes.Graph.Impl;

import :Passes.Graph;

// GraphRenderPass is now a stub — all graph node/edge rendering is handled
// by PointPass (ECS::Point::Component) and LinePass (ECS::Line::Component).
// AddPasses() is defined inline in the .cppm as a no-op.
// This TU exists only to satisfy the module partition build requirement.
