module;

export module Runtime.EditorUI;

import Runtime.Engine;

export namespace Runtime::EditorUI
{
    // Registers a small set of editor-facing panels and menus to improve
    // discoverability of core engine features (FeatureRegistry, FrameGraph,
    // Selection config).
    //
    // Contract:
    //  - Call once after Engine startup, before the first frame.
    //  - Safe to call multiple times; panels are de-duplicated by name.
    void RegisterDefaultPanels(Engine& engine);
}

