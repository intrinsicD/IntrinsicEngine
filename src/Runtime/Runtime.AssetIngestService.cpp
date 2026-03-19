module;
#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

module Runtime.AssetIngestService;

import Core.Logging;
import Core.Tasks;
import Core.IOBackend;
import Graphics;
import ECS;
import Runtime.AssetPipeline;
import Runtime.SceneManager;

namespace
{
    struct ImportPathInfo
    {
        std::string CanonicalPath;
        std::string Extension;
        bool IsUnderAssetRoot = false;
    };

    [[nodiscard]] std::expected<ImportPathInfo, std::string> ResolveImportPath(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::path fsPath(path);

        if (!std::filesystem::exists(fsPath, ec) || ec)
            return std::unexpected("Asset file does not exist");

        std::filesystem::path canonicalPath = std::filesystem::canonical(fsPath, ec);
        if (ec)
            return std::unexpected("Failed to resolve canonical path");

        std::string extension = canonicalPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool isUnderAssetRoot = false;
        std::filesystem::path assetDir = std::filesystem::canonical("assets/", ec);
        if (!ec)
        {
            const auto relativePath = canonicalPath.lexically_relative(assetDir);
            isUnderAssetRoot = !relativePath.empty() && !relativePath.native().starts_with("..");
        }

        return ImportPathInfo{
            .CanonicalPath = canonicalPath.string(),
            .Extension = std::move(extension),
            .IsUnderAssetRoot = isUnderAssetRoot,
        };
    }
}

namespace Runtime
{
    AssetIngestService::AssetIngestService(std::shared_ptr<RHI::VulkanDevice> device,
                                           RHI::TransferManager& transferManager,
                                           Graphics::GeometryPool& geometryStorage,
                                           Graphics::MaterialSystem& materialSystem,
                                           AssetPipeline& assetPipeline,
                                           SceneManager& sceneManager,
                                           Graphics::IORegistry& ioRegistry,
                                           Core::IO::IIOBackend& ioBackend,
                                           uint32_t defaultTextureId)
        : m_Device(std::move(device))
        , m_TransferManager(transferManager)
        , m_GeometryStorage(geometryStorage)
        , m_MaterialSystem(materialSystem)
        , m_AssetPipeline(assetPipeline)
        , m_SceneManager(sceneManager)
        , m_IORegistry(ioRegistry)
        , m_IOBackend(ioBackend)
        , m_DefaultTextureId(defaultTextureId)
    {
        Core::Log::Info("AssetIngestService: Initialized.");
    }

    AssetIngestService::~AssetIngestService()
    {
        Core::Log::Info("AssetIngestService: Shutdown.");
    }

    void AssetIngestService::EnqueueDropImport(const std::string& path)
    {
        auto importPath = ResolveImportPath(path);
        if (!importPath)
        {
            Core::Log::Error("{}: {}", importPath.error(), path);
            return;
        }

        if (!importPath->IsUnderAssetRoot)
            Core::Log::Warn("Dropped file is outside of assets directory: {}", importPath->CanonicalPath);

        if (!m_IORegistry.CanImport(importPath->Extension))
        {
            Core::Log::Warn("Unsupported file extension: {}", importPath->Extension);
            return;
        }

        Core::Log::Info("Scheduling async ingest: {}", importPath->CanonicalPath);

        std::string canonicalPath = importPath->CanonicalPath;
        Core::Tasks::Scheduler::Dispatch([this, canonicalPath]()
        {
            auto loadResult = Graphics::ModelLoader::LoadAsync(
                m_Device,
                m_TransferManager,
                m_GeometryStorage,
                canonicalPath,
                m_IORegistry,
                m_IOBackend);

            if (!loadResult)
            {
                Core::Log::Error("Failed to load model: {} ({})",
                                 canonicalPath,
                                 Graphics::AssetErrorToString(loadResult.error()));
                return;
            }

            m_AssetPipeline.RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);
            m_AssetPipeline.RunOnMainThread([this, model = std::move(loadResult->ModelData), canonicalPath]() mutable
            {
                auto imported = MaterializeImportedModel(canonicalPath, std::move(model), "drop");
                if (!imported)
                {
                    Core::Log::Error("Failed to register imported model on main thread: {}", canonicalPath);
                    return;
                }

                entt::entity root = m_SceneManager.SpawnModel(
                    m_AssetPipeline.GetAssetManager(),
                    imported->ModelHandle,
                    imported->MaterialHandle,
                    glm::vec3(0.0f),
                    glm::vec3(0.01f));

                if (root != entt::null)
                {
                    m_SceneManager.GetRegistry().emplace_or_replace<ECS::Components::AssetSourceRef::Component>(
                        root,
                        canonicalPath);
                }

                Core::Log::Info("Successfully spawned imported asset: {}", canonicalPath);
            });
        });
    }

    std::optional<ImportedAssetHandles> AssetIngestService::ImportModelSync(const std::string& path,
                                                                            std::string_view assetNamespace)
    {
        auto importPath = ResolveImportPath(path);
        if (!importPath)
        {
            Core::Log::Error("{}: {}", importPath.error(), path);
            return std::nullopt;
        }

        if (!m_IORegistry.CanImport(importPath->Extension))
        {
            Core::Log::Warn("Unsupported file extension: {}", importPath->Extension);
            return std::nullopt;
        }

        auto loadResult = Graphics::ModelLoader::LoadAsync(
            m_Device,
            m_TransferManager,
            m_GeometryStorage,
            importPath->CanonicalPath,
            m_IORegistry,
            m_IOBackend);

        if (!loadResult)
        {
            Core::Log::Error("Failed to import asset: {} ({})",
                             importPath->CanonicalPath,
                             Graphics::AssetErrorToString(loadResult.error()));
            return std::nullopt;
        }

        m_AssetPipeline.RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);
        return MaterializeImportedModel(importPath->CanonicalPath, std::move(loadResult->ModelData), assetNamespace);
    }

    std::optional<ImportedAssetHandles> AssetIngestService::MaterializeImportedModel(
        const std::string& sourcePath,
        std::unique_ptr<Graphics::Model> model,
        std::string_view assetNamespace)
    {
        if (!model)
            return std::nullopt;

        auto& assetManager = m_AssetPipeline.GetAssetManager();

        std::filesystem::path fsPath(sourcePath);
        std::string baseName = fsPath.filename().string();

        uint64_t& counter = assetNamespace == "scene" ? m_ReimportAssetCounter : m_DropAssetCounter;
        std::string assetName = std::string(assetNamespace) + "::" + baseName + "::" + std::to_string(++counter);

        ImportedAssetHandles handles;
        handles.ModelHandle = assetManager.Create(assetName, std::move(model));

        Graphics::MaterialData matData;
        matData.AlbedoID = m_DefaultTextureId;
        matData.RoughnessFactor = 0.5f;

        auto defaultMat = std::make_unique<Graphics::Material>(m_MaterialSystem, matData);
        const std::string materialName = assetName + "::DefaultMaterial";
        handles.MaterialHandle = assetManager.Create(materialName, std::move(defaultMat));
        m_AssetPipeline.TrackMaterial(handles.MaterialHandle);

        return handles;
    }
}
