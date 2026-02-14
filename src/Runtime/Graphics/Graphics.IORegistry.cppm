module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

export module Graphics:IORegistry;

import :Geometry;
import :AssetErrors;
import Core.IOBackend;

export namespace Graphics
{
    // --- Import Options ---
    enum class ImportHint : uint8_t { Auto, MeshOnly, Scene };

    struct ImportOptions
    {
        ImportHint Hint = ImportHint::Auto;
    };

    // --- Load Context (everything the loader needs, no I/O ownership) ---
    struct LoadContext
    {
        std::string_view SourcePath;       // Original file path (for error messages)
        std::string_view BasePath;         // Directory containing the file (for relative refs)
        ImportOptions Options;
        Core::IO::IIOBackend* Backend = nullptr;  // For requesting additional resources (e.g. glTF .bin)
    };

    // --- Import Result (extensible variant) ---
    struct MeshImportData
    {
        std::vector<GeometryCpuData> Meshes;
    };
    // Phase 1: struct SceneImportData { ... };

    using ImportResult = std::variant<MeshImportData>;

    // --- Loader Base Class ---
    // Loaders are pure transforms: bytes -> CPU object.
    // They NEVER open files. They receive bytes from the I/O backend.
    class IAssetLoader
    {
    public:
        virtual ~IAssetLoader() = default;

        [[nodiscard]] virtual std::string_view FormatName() const = 0;
        [[nodiscard]] virtual std::span<const std::string_view> Extensions() const = 0;

        // Transform raw bytes into a CPU-side representation.
        [[nodiscard]] virtual std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) = 0;
    };

    // --- Exporter Base Class (stub for Phase 0) ---
    class IAssetExporter
    {
    public:
        virtual ~IAssetExporter() = default;

        [[nodiscard]] virtual std::string_view FormatName() const = 0;
        [[nodiscard]] virtual std::span<const std::string_view> Extensions() const = 0;
    };

    // --- Registry ---
    // Non-copyable, non-movable subsystem. Cold-path, main-thread registration.
    class IORegistry
    {
    public:
        IORegistry() = default;
        ~IORegistry() = default;

        IORegistry(const IORegistry&) = delete;
        IORegistry& operator=(const IORegistry&) = delete;
        IORegistry(IORegistry&&) = delete;
        IORegistry& operator=(IORegistry&&) = delete;

        bool RegisterLoader(std::unique_ptr<IAssetLoader> loader);
        bool RegisterExporter(std::unique_ptr<IAssetExporter> exporter);

        [[nodiscard]] IAssetLoader* FindLoader(std::string_view extension) const;
        [[nodiscard]] IAssetExporter* FindExporter(std::string_view extension) const;
        [[nodiscard]] bool CanImport(std::string_view extension) const;
        [[nodiscard]] std::vector<std::string_view> GetSupportedImportExtensions() const;

        // Convenience: read bytes via backend, find loader by extension, decode.
        [[nodiscard]] std::expected<ImportResult, AssetError> Import(
            const std::string& filepath,
            Core::IO::IIOBackend& backend,
            const ImportOptions& options = {}) const;

    private:
        std::unordered_map<std::string, IAssetLoader*> m_LoadersByExt;
        std::vector<std::unique_ptr<IAssetLoader>> m_Loaders;
        std::unordered_map<std::string, IAssetExporter*> m_ExportersByExt;
        std::vector<std::unique_ptr<IAssetExporter>> m_Exporters;
    };

    // Registers all built-in format loaders (OBJ, PLY, XYZ, TGF, GLTF).
    void RegisterBuiltinLoaders(IORegistry& registry);
}
