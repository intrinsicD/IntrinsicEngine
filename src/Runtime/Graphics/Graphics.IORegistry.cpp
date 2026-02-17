module;
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

module Graphics:IORegistry.Impl;
import :IORegistry;
import :AssetErrors;
import :Geometry;
import :Importers.OBJ;
import :Importers.PLY;
import :Importers.XYZ;
import :Importers.TGF;
import :Importers.GLTF;
import :Importers.STL;
import :Importers.OFF;
import Core.IOBackend;
import Core.Error;
import Core.Logging;

namespace Graphics
{
    namespace
    {
        std::string ToLowerStr(std::string_view sv)
        {
            std::string s(sv);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        AssetError MapCoreError(Core::ErrorCode code)
        {
            switch (code)
            {
            case Core::ErrorCode::FileNotFound:
                return AssetError::FileNotFound;
            case Core::ErrorCode::FileReadError:
                return AssetError::DecodeFailed;
            case Core::ErrorCode::InvalidPath:
            case Core::ErrorCode::OutOfRange:
                return AssetError::InvalidData;
            default:
                return AssetError::DecodeFailed;
            }
        }
    }

    bool IORegistry::RegisterLoader(std::unique_ptr<IAssetLoader> loader)
    {
        if (!loader) return false;

        auto extensions = loader->Extensions();
        bool anyRegistered = false;

        for (auto ext : extensions)
        {
            std::string key = ToLowerStr(ext);
            if (m_LoadersByExt.contains(key))
            {
                Core::Log::Warn("IORegistry: Extension '{}' already registered, skipping", key);
                continue;
            }
            m_LoadersByExt[key] = loader.get();
            anyRegistered = true;
        }

        if (anyRegistered)
        {
            m_Loaders.push_back(std::move(loader));
        }
        return anyRegistered;
    }

    bool IORegistry::RegisterExporter(std::unique_ptr<IAssetExporter> exporter)
    {
        if (!exporter) return false;

        auto extensions = exporter->Extensions();
        bool anyRegistered = false;

        for (auto ext : extensions)
        {
            std::string key = ToLowerStr(ext);
            if (m_ExportersByExt.contains(key))
            {
                Core::Log::Warn("IORegistry: Exporter extension '{}' already registered, skipping", key);
                continue;
            }
            m_ExportersByExt[key] = exporter.get();
            anyRegistered = true;
        }

        if (anyRegistered)
        {
            m_Exporters.push_back(std::move(exporter));
        }
        return anyRegistered;
    }

    IAssetLoader* IORegistry::FindLoader(std::string_view extension) const
    {
        std::string key = ToLowerStr(extension);
        auto it = m_LoadersByExt.find(key);
        if (it != m_LoadersByExt.end())
            return it->second;
        return nullptr;
    }

    IAssetExporter* IORegistry::FindExporter(std::string_view extension) const
    {
        std::string key = ToLowerStr(extension);
        auto it = m_ExportersByExt.find(key);
        if (it != m_ExportersByExt.end())
            return it->second;
        return nullptr;
    }

    bool IORegistry::CanImport(std::string_view extension) const
    {
        return FindLoader(extension) != nullptr;
    }

    std::vector<std::string_view> IORegistry::GetSupportedImportExtensions() const
    {
        std::vector<std::string_view> result;
        result.reserve(m_LoadersByExt.size());
        for (const auto& [ext, _] : m_LoadersByExt)
            result.emplace_back(ext);
        return result;
    }

    std::expected<ImportResult, AssetError> IORegistry::Import(
        const std::string& filepath,
        Core::IO::IIOBackend& backend,
        const ImportOptions& options) const
    {
        namespace fs = std::filesystem;

        fs::path fsPath(filepath);
        std::string ext = fsPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        auto* loader = FindLoader(ext);
        if (!loader)
            return std::unexpected(AssetError::UnsupportedFormat);

        // Read bytes via backend
        Core::IO::IORequest req;
        req.Path = filepath;
        auto readResult = backend.Read(req);
        if (!readResult)
            return std::unexpected(MapCoreError(readResult.error()));

        // Find base directory for relative resource references
        std::string baseDir = fsPath.parent_path().string();

        LoadContext ctx;
        ctx.SourcePath = filepath;
        ctx.BasePath = baseDir;
        ctx.Options = options;
        ctx.Backend = &backend;

        return loader->Load(readResult->Data, ctx);
    }

    // -------------------------------------------------------------------------
    // Vtable anchors: defining the destructor (the Itanium ABI key function)
    // HERE — in the TU that imports ALL loader partitions — ensures each
    // vtable is emitted in this object file. Retained as defensive practice
    // for robust vtable emission across module partition boundaries.
    // -------------------------------------------------------------------------
    OBJLoader::~OBJLoader() = default;
    PLYLoader::~PLYLoader() = default;
    XYZLoader::~XYZLoader() = default;
    TGFLoader::~TGFLoader() = default;
    GLTFLoader::~GLTFLoader() = default;
    STLLoader::~STLLoader() = default;
    OFFLoader::~OFFLoader() = default;

    void RegisterBuiltinLoaders(IORegistry& registry)
    {
        registry.RegisterLoader(std::make_unique<OBJLoader>());
        registry.RegisterLoader(std::make_unique<PLYLoader>());
        registry.RegisterLoader(std::make_unique<XYZLoader>());
        registry.RegisterLoader(std::make_unique<TGFLoader>());
        registry.RegisterLoader(std::make_unique<GLTFLoader>());
        registry.RegisterLoader(std::make_unique<STLLoader>());
        registry.RegisterLoader(std::make_unique<OFFLoader>());

        Core::Log::Info("IORegistry: Registered {} built-in loaders",
                        registry.GetSupportedImportExtensions().size());
    }
}
