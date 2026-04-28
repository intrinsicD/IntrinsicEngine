module;
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

export module Runtime.SceneSerializer;

import Core.Error;
import Core.IOBackend;
import Runtime.Engine;

export namespace Runtime
{
    // Scene serialization error codes.
    enum class SceneError : uint32_t
    {
        WriteError,
        ReadError,
        ParseError,
        InvalidSchema,
        AssetImportFailed,
    };

    [[nodiscard]] constexpr std::string_view SceneErrorToString(SceneError e)
    {
        switch (e)
        {
        case SceneError::WriteError:        return "WriteError";
        case SceneError::ReadError:         return "ReadError";
        case SceneError::ParseError:        return "ParseError";
        case SceneError::InvalidSchema:     return "InvalidSchema";
        case SceneError::AssetImportFailed: return "AssetImportFailed";
        }
        return "Unknown";
    }

    // Save the current scene to a JSON file at the given path.
    //
    // Serializes: entity names, transforms, hierarchy relationships,
    // asset source paths, visibility flags, and rendering parameters
    // (point cloud/graph settings, wireframe/vertex display toggles).
    //
    // GPU-only state (handles, slots, caches) is NOT serialized — it is
    // reconstructed on load from the asset files.
    [[nodiscard]] std::expected<void, SceneError> SaveScene(
        const Engine& engine,
        const std::string& path,
        Core::IO::IIOBackend& backend);

    // Load a scene from a JSON file, replacing the current scene contents.
    //
    // Clears the current scene, parses the JSON file, recreates entities
    // with their transforms and hierarchy, and re-imports assets from
    // their recorded source paths. Missing asset files are logged and
    // skipped (the entity is created but without geometry).
    //
    // Returns a diagnostic struct on success with counts of loaded/failed
    // entities and assets. On hard failure (file not found, parse error),
    // returns an error.
    struct LoadDiagnostics
    {
        uint32_t EntitiesLoaded = 0;
        uint32_t AssetsLoaded = 0;
        uint32_t AssetsFailed = 0;
        std::string FailureDetail;
    };

    [[nodiscard]] std::expected<LoadDiagnostics, SceneError> LoadScene(
        Engine& engine,
        const std::string& path,
        Core::IO::IIOBackend& backend);

    // Scene dirty tracking. The dirty flag is set when entity transforms,
    // components, or hierarchy change since the last save/load. Used by
    // the editor to prompt before discarding unsaved changes.
    class SceneDirtyTracker
    {
    public:
        void MarkDirty() { m_Dirty = true; }
        void ClearDirty() { m_Dirty = false; }
        [[nodiscard]] bool IsDirty() const { return m_Dirty; }

        // Path of the last-saved or last-loaded scene file.
        void SetCurrentPath(const std::string& path) { m_CurrentPath = path; }
        [[nodiscard]] const std::string& GetCurrentPath() const { return m_CurrentPath; }

    private:
        bool m_Dirty = false;
        std::string m_CurrentPath;
    };
}
