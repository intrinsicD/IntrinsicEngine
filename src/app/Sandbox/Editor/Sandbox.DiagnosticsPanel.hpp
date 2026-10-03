// App-private log-window state and drawing over runtime diagnostics only.
// Include after runtime imports; standard headers belong in the global fragment.
#pragma once
extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    struct DiagnosticsPanelState
    {
        std::array<bool, 4> Levels{true, true, true, true};
        std::array<char, 128> Category{};
        bool AutoScroll{true};
        std::uint64_t Cursor{};
        std::uint64_t ClearCursor{};
        std::uint64_t Dropped{};
        std::uint8_t AppliedLevels{0x0f};
        std::string AppliedCategory{};
        std::vector<Runtime::EditorDiagnosticLogEntry> Entries{};
        Runtime::EditorDeviceStatus Device{};
    };

    void DrawDiagnosticsPanel(Runtime::EditorDiagnosticsStream* stream, DiagnosticsPanelState& state);
}
}
