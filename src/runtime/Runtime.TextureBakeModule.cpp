module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.PropertyTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;

        constexpr float kAtlasEpsilon = 1.0e-4f;
        constexpr float kUvAreaEpsilon = 1.0e-10f;
        constexpr std::uint64_t kFnv1aOffset64 = 14695981039346656037ull;
        constexpr std::uint64_t kFnv1aPrime64 = 1099511628211ull;

        [[nodiscard]] std::uint64_t FingerprintIndices(
            const std::span<const std::uint32_t> values) noexcept
        {
            std::uint64_t fingerprint = kFnv1aOffset64;
            for (const std::uint32_t value : values)
            {
                for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
                {
                    fingerprint ^= static_cast<std::uint64_t>(
                        static_cast<std::uint8_t>(value >> shift));
                    fingerprint *= kFnv1aPrime64;
                }
            }
            return fingerprint == 0u ? 1u : fingerprint;
        }

        [[nodiscard]] PropertyTextureBakeResult UnavailableBakeResult()
        {
            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::NonOperationalBackend,
                .Diagnostic = "texture-bake module is unavailable",
            };
        }

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            const ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            return entity != ECS::InvalidEntityHandle &&
                           scene.Raw().valid(entity)
                ? entity
                : ECS::InvalidEntityHandle;
        }

        [[nodiscard]] bool Finite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool Finite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z) &&
                   std::isfinite(value.w);
        }

        [[nodiscard]] float Cross2(
            const glm::vec2 a,
            const glm::vec2 b) noexcept
        {
            return a.x * b.y - a.y * b.x;
        }

        [[nodiscard]] std::uint64_t EdgeKey(
            std::uint32_t a,
            std::uint32_t b) noexcept
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<std::uint64_t>(a) << 32u) |
                   static_cast<std::uint64_t>(b);
        }

        void AdvanceGeneration(std::uint64_t& generation) noexcept
        {
            ++generation;
            if (generation == 0u)
                generation = 1u;
        }

        [[nodiscard]] GeometryPresentationBindingRecipe* FindPresentation(
            GeometryPresentationRecipe& bindings,
            const TextureBakeConsumerBinding& consumer) noexcept
        {
            if (!consumer.PresentationKey.empty())
                return FindGeometryPresentationBinding(
                    bindings,
                    consumer.PresentationKey);
            for (GeometryPresentationBindingRecipe& presentation :
                 bindings.Presentations)
            {
                if (FindGeometryPresentationSlot(presentation, consumer.Semantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] const GeometryPresentationBindingRecipe* FindPresentation(
            const GeometryPresentationRecipe& bindings,
            const TextureBakeConsumerBinding& consumer) noexcept
        {
            if (!consumer.PresentationKey.empty())
                return FindGeometryPresentationBinding(
                    bindings,
                    consumer.PresentationKey);
            for (const GeometryPresentationBindingRecipe& presentation :
                 bindings.Presentations)
            {
                if (FindGeometryPresentationSlot(presentation, consumer.Semantic) != nullptr)
                    return &presentation;
            }
            return nullptr;
        }

        [[nodiscard]] bool IsScalar(
            const Geometry::PropertyValueKind kind) noexcept
        {
            return kind == Geometry::PropertyValueKind::Float ||
                   kind == Geometry::PropertyValueKind::Double;
        }

        [[nodiscard]] PropertyTextureBakeStatus StatusForResolution(
            const GeometryPropertyResolutionStatus status) noexcept
        {
            switch (status)
            {
            case GeometryPropertyResolutionStatus::Resolved:
                return PropertyTextureBakeStatus::Success;
            case GeometryPropertyResolutionStatus::MissingName:
            case GeometryPropertyResolutionStatus::MissingProperty:
                return PropertyTextureBakeStatus::MissingProperty;
            case GeometryPropertyResolutionStatus::ValueKindMismatch:
                return PropertyTextureBakeStatus::UnsupportedPropertyType;
            case GeometryPropertyResolutionStatus::ElementCountMismatch:
                return PropertyTextureBakeStatus::MismatchedPropertyCount;
            case GeometryPropertyResolutionStatus::UnsupportedDomain:
                return PropertyTextureBakeStatus::UnsupportedSourceDomain;
            case GeometryPropertyResolutionStatus::NonFiniteValues:
                return PropertyTextureBakeStatus::NonFinitePropertyValue;
            }
            return PropertyTextureBakeStatus::UnsupportedPropertyType;
        }

        [[nodiscard]] bool ConsumerCompatible(
            const TextureBakeConsumerBinding& consumer,
            const Geometry::PropertyValueKind valueKind,
            const PropertyTextureBakeStorage storage,
            const PropertyTextureBakeEncoding encoder) noexcept
        {
            return IsPropertyTextureBakeConsumerCompatible(
                consumer,
                valueKind,
                storage,
                encoder);
        }

        struct GeneratedPropertyTextureMetadata
        {
            std::uint32_t SchemaVersion{1u};
            std::uint32_t StableEntityId{0u};
            std::string OutputName{};
            GeometryElementDomain SourceDomain{
                GeometryElementDomain::Unknown};
            std::string SourcePropertyName{};
            std::string TexcoordPropertyName{};
            Geometry::PropertyValueKind ValueKind{
                Geometry::PropertyValueKind::Unknown};
            PropertyTextureBakeStorage Storage{
                PropertyTextureBakeStorage::Auto};
            PropertyTextureBakeEncoding Encoder{
                PropertyTextureBakeEncoding::Auto};
            Graphics::Colormap::Type EncodingColormap{
                Graphics::Colormap::Type::Viridis};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            std::uint64_t SourceGeneration{0u};
            std::uint64_t Serial{0u};
        };

        struct PreparedPropertyBake
        {
            PropertyTextureBakeStatus Status{
                PropertyTextureBakeStatus::Success};
            Geometry::PropertyValueKind ValueKind{
                Geometry::PropertyValueKind::Unknown};
            PropertyTextureBakeStorage Storage{
                PropertyTextureBakeStorage::RawFloat};
            PropertyTextureBakeEncoding Encoder{
                PropertyTextureBakeEncoding::Auto};
            Graphics::Colormap::Type EncodingColormap{
                Graphics::Colormap::Type::Viridis};
            Graphics::PropertyTextureBakeDomain Domain{
                Graphics::PropertyTextureBakeDomain::Vertex};
            Graphics::PropertyTextureBakeValueKind GpuValueKind{
                Graphics::PropertyTextureBakeValueKind::Scalar};
            Graphics::PropertyTextureBakeEncoding GpuEncoding{
                Graphics::PropertyTextureBakeEncoding::Raw};
            RHI::Format Format{RHI::Format::Undefined};
            std::vector<glm::vec2> Texcoords{};
            std::vector<glm::vec4> Values{};
            std::vector<std::uint32_t> SurfaceIndices{};
            std::uint64_t SurfaceIndexFingerprint{0u};
            std::size_t ExpectedElementCount{0u};
            std::uint64_t SourceGeneration{0u};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
            std::string OutputName{};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == PropertyTextureBakeStatus::Success;
            }
        };

        struct PropertyTextureConsumerRecord
        {
            std::string OutputName{};
            std::vector<TextureBakeConsumerBinding> Consumers{};
        };

        struct PropertyTextureConsumerBindings
        {
            std::vector<PropertyTextureConsumerRecord> Records{};
        };

        [[nodiscard]] PreparedPropertyBake PrepareFailure(
            const PropertyTextureBakeStatus status,
            std::string diagnostic)
        {
            return PreparedPropertyBake{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] bool CopyPropertyValues(
            const Geometry::ConstPropertySet& properties,
            const std::string& name,
            const Geometry::PropertyValueKind kind,
            std::vector<glm::vec4>& values)
        {
            values.clear();
            const auto append = [&values](const auto& source, auto convert)
            {
                values.reserve(source.size());
                for (const auto& value : source)
                    values.push_back(convert(value));
            };

            switch (kind)
            {
            case Geometry::PropertyValueKind::Float:
                if (const auto property = properties.Get<float>(name))
                {
                    append(property.Vector(), [](const float value)
                    {
                        return glm::vec4{value, 0.0f, 0.0f, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Double:
                if (const auto property = properties.Get<double>(name))
                {
                    append(property.Vector(), [](const double value)
                    {
                        return glm::vec4{
                            static_cast<float>(value), 0.0f, 0.0f, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::UInt32:
                if (const auto property = properties.Get<std::uint32_t>(name))
                {
                    append(property.Vector(), [](const std::uint32_t value)
                    {
                        return glm::vec4{
                            std::bit_cast<float>(value), 0.0f, 0.0f, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Vec2:
                if (const auto property = properties.Get<glm::vec2>(name))
                {
                    append(property.Vector(), [](const glm::vec2 value)
                    {
                        return glm::vec4{value, 0.0f, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Vec3:
                if (const auto property = properties.Get<glm::vec3>(name))
                {
                    append(property.Vector(), [](const glm::vec3 value)
                    {
                        return glm::vec4{value, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Vec4:
                if (const auto property = properties.Get<glm::vec4>(name))
                {
                    append(property.Vector(), [](const glm::vec4 value)
                    {
                        return value;
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                break;
            }
            return false;
        }
    }

    PropertyTextureBakeRepresentation ResolvePropertyTextureBakeRepresentation(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage requestedStorage,
        const PropertyTextureBakeEncoding requestedEncoding) noexcept
    {
        PropertyTextureBakeRepresentation representation{
            .Storage = requestedStorage,
            .Encoding = requestedEncoding,
        };
        if (representation.Storage == PropertyTextureBakeStorage::Auto)
        {
            representation.Storage =
                valueKind == Geometry::PropertyValueKind::UInt32
                ? PropertyTextureBakeStorage::EncodedRgba
                : PropertyTextureBakeStorage::RawFloat;
        }
        if (representation.Encoding != PropertyTextureBakeEncoding::Auto)
            return representation;

        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Float:
        case Geometry::PropertyValueKind::Double:
            representation.Encoding =
                PropertyTextureBakeEncoding::LinearScalar;
            break;
        case Geometry::PropertyValueKind::UInt32:
            representation.Encoding =
                PropertyTextureBakeEncoding::LabelPalette;
            break;
        case Geometry::PropertyValueKind::Vec2:
            representation.Encoding =
                PropertyTextureBakeEncoding::Vector2;
            break;
        case Geometry::PropertyValueKind::Vec3:
            representation.Encoding =
                PropertyTextureBakeEncoding::Vector3;
            break;
        case Geometry::PropertyValueKind::Vec4:
            representation.Encoding =
                PropertyTextureBakeEncoding::RgbaColor;
            break;
        case Geometry::PropertyValueKind::Unknown:
        case Geometry::PropertyValueKind::Bool:
        case Geometry::PropertyValueKind::Int32:
        case Geometry::PropertyValueKind::UInt64:
            break;
        }
        return representation;
    }

    bool IsPropertyTextureBakeRepresentationCompatible(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage storage,
        const PropertyTextureBakeEncoding encoding) noexcept
    {
        if (storage == PropertyTextureBakeStorage::Auto ||
            encoding == PropertyTextureBakeEncoding::Auto)
        {
            return false;
        }

        if (storage == PropertyTextureBakeStorage::RawFloat)
        {
            switch (valueKind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
                return encoding ==
                    PropertyTextureBakeEncoding::LinearScalar;
            case Geometry::PropertyValueKind::Vec2:
                return encoding == PropertyTextureBakeEncoding::Vector2;
            case Geometry::PropertyValueKind::Vec3:
                return encoding == PropertyTextureBakeEncoding::Vector3;
            case Geometry::PropertyValueKind::Vec4:
                return encoding == PropertyTextureBakeEncoding::RgbaColor;
            case Geometry::PropertyValueKind::UInt32:
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                return false;
            }
        }

        if (storage != PropertyTextureBakeStorage::EncodedRgba)
            return false;
        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Float:
        case Geometry::PropertyValueKind::Double:
            return encoding ==
                       PropertyTextureBakeEncoding::LinearScalar ||
                   encoding ==
                       PropertyTextureBakeEncoding::ScalarColormap;
        case Geometry::PropertyValueKind::UInt32:
            return encoding ==
                PropertyTextureBakeEncoding::LabelPalette;
        case Geometry::PropertyValueKind::Vec2:
            return encoding == PropertyTextureBakeEncoding::Vector2 ||
                   encoding == PropertyTextureBakeEncoding::RgbaColor;
        case Geometry::PropertyValueKind::Vec3:
            return encoding == PropertyTextureBakeEncoding::Vector3 ||
                   encoding == PropertyTextureBakeEncoding::RgbaColor ||
                   encoding == PropertyTextureBakeEncoding::Normal;
        case Geometry::PropertyValueKind::Vec4:
            return encoding == PropertyTextureBakeEncoding::RgbaColor;
        case Geometry::PropertyValueKind::Unknown:
        case Geometry::PropertyValueKind::Bool:
        case Geometry::PropertyValueKind::Int32:
        case Geometry::PropertyValueKind::UInt64:
            return false;
        }
        return false;
    }

    PropertyTextureBakeRepresentation ResolvePropertyTextureBakeRepresentation(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage requestedStorage,
        const PropertyTextureBakeEncoding requestedEncoding,
        const std::span<const TextureBakeConsumerBinding> consumers) noexcept
    {
        PropertyTextureBakeStorage storage = requestedStorage;
        PropertyTextureBakeEncoding encoding = requestedEncoding;
        const bool hasNormalConsumer = std::ranges::any_of(
            consumers,
            [](const TextureBakeConsumerBinding& consumer)
            {
                return consumer.Semantic ==
                    GeometryPresentationSlotSemantic::Normal;
            });
        const bool hasLinearPbrConsumer = std::ranges::any_of(
            consumers,
            [](const TextureBakeConsumerBinding& consumer)
            {
                return consumer.Semantic ==
                           GeometryPresentationSlotSemantic::Roughness ||
                       consumer.Semantic ==
                           GeometryPresentationSlotSemantic::Metallic;
            });

        if (storage == PropertyTextureBakeStorage::Auto)
        {
            storage = hasNormalConsumer ||
                              valueKind ==
                                  Geometry::PropertyValueKind::UInt32
                ? PropertyTextureBakeStorage::EncodedRgba
                : PropertyTextureBakeStorage::RawFloat;
        }
        if (encoding == PropertyTextureBakeEncoding::Auto)
        {
            if (hasNormalConsumer)
            {
                encoding = PropertyTextureBakeEncoding::Normal;
            }
            else if (hasLinearPbrConsumer &&
                     IsScalar(valueKind))
            {
                encoding =
                    PropertyTextureBakeEncoding::LinearScalar;
            }
        }
        return ResolvePropertyTextureBakeRepresentation(
            valueKind,
            storage,
            encoding);
    }

    bool IsPropertyTextureBakeConsumerCompatible(
        const TextureBakeConsumerBinding& consumer,
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage storage,
        const PropertyTextureBakeEncoding encoding) noexcept
    {
        if (!IsPropertyTextureBakeRepresentationCompatible(
                valueKind,
                storage,
                encoding))
        {
            return false;
        }
        const bool raw = storage == PropertyTextureBakeStorage::RawFloat;
        const bool scalar = IsScalar(valueKind);
        switch (consumer.Semantic)
        {
        case GeometryPresentationSlotSemantic::Normal:
            return valueKind == Geometry::PropertyValueKind::Vec3 &&
                   storage == PropertyTextureBakeStorage::EncodedRgba &&
                   encoding == PropertyTextureBakeEncoding::Normal;
        case GeometryPresentationSlotSemantic::Roughness:
        case GeometryPresentationSlotSemantic::Metallic:
            return scalar &&
                   (raw ||
                    encoding ==
                        PropertyTextureBakeEncoding::LinearScalar);
        case GeometryPresentationSlotSemantic::ScalarField:
            return (scalar &&
                    (raw ||
                     encoding ==
                         PropertyTextureBakeEncoding::LinearScalar ||
                     encoding ==
                         PropertyTextureBakeEncoding::ScalarColormap)) ||
                   (valueKind == Geometry::PropertyValueKind::UInt32 &&
                    !raw &&
                    encoding ==
                        PropertyTextureBakeEncoding::LabelPalette);
        case GeometryPresentationSlotSemantic::Albedo:
            if (scalar)
            {
                return raw ||
                       encoding ==
                           PropertyTextureBakeEncoding::LinearScalar ||
                       encoding ==
                           PropertyTextureBakeEncoding::ScalarColormap;
            }
            if (valueKind == Geometry::PropertyValueKind::UInt32)
            {
                return !raw &&
                       encoding ==
                           PropertyTextureBakeEncoding::LabelPalette;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec2)
            {
                return raw ||
                       encoding == PropertyTextureBakeEncoding::Vector2 ||
                       encoding == PropertyTextureBakeEncoding::RgbaColor;
            }
            if (valueKind == Geometry::PropertyValueKind::Vec3)
            {
                return raw ||
                       encoding == PropertyTextureBakeEncoding::Vector3 ||
                       encoding == PropertyTextureBakeEncoding::RgbaColor ||
                       encoding == PropertyTextureBakeEncoding::Normal;
            }
            return valueKind == Geometry::PropertyValueKind::Vec4 &&
                   (raw ||
                    encoding == PropertyTextureBakeEncoding::RgbaColor);
        case GeometryPresentationSlotSemantic::Displacement:
        case GeometryPresentationSlotSemantic::PointColor:
        case GeometryPresentationSlotSemantic::PointScalarField:
        case GeometryPresentationSlotSemantic::PointSize:
        case GeometryPresentationSlotSemantic::PointNormalOrientation:
        case GeometryPresentationSlotSemantic::LineColor:
        case GeometryPresentationSlotSemantic::LineScalarField:
        case GeometryPresentationSlotSemantic::LineWidth:
            return false;
        }
        return false;
    }

    const char* DebugNameForPropertyTextureBakeStatus(
        const PropertyTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case PropertyTextureBakeStatus::Success:
            return "PropertyTextureBake.Success";
        case PropertyTextureBakeStatus::Scheduled:
            return "PropertyTextureBake.Scheduled";
        case PropertyTextureBakeStatus::NonOperationalBackend:
            return "PropertyTextureBake.NonOperationalBackend";
        case PropertyTextureBakeStatus::MissingScene:
            return "PropertyTextureBake.MissingScene";
        case PropertyTextureBakeStatus::MissingAssetService:
            return "PropertyTextureBake.MissingAssetService";
        case PropertyTextureBakeStatus::StaleEntity:
            return "PropertyTextureBake.StaleEntity";
        case PropertyTextureBakeStatus::NonMeshSource:
            return "PropertyTextureBake.NonMeshSource";
        case PropertyTextureBakeStatus::InvalidResolution:
            return "PropertyTextureBake.InvalidResolution";
        case PropertyTextureBakeStatus::InvalidPadding:
            return "PropertyTextureBake.InvalidPadding";
        case PropertyTextureBakeStatus::InvalidRange:
            return "PropertyTextureBake.InvalidRange";
        case PropertyTextureBakeStatus::MissingProperty:
            return "PropertyTextureBake.MissingProperty";
        case PropertyTextureBakeStatus::UnsupportedPropertyType:
            return "PropertyTextureBake.UnsupportedPropertyType";
        case PropertyTextureBakeStatus::MismatchedPropertyCount:
            return "PropertyTextureBake.MismatchedPropertyCount";
        case PropertyTextureBakeStatus::UnsupportedSourceDomain:
            return "PropertyTextureBake.UnsupportedSourceDomain";
        case PropertyTextureBakeStatus::MissingTexcoords:
            return "PropertyTextureBake.MissingTexcoords";
        case PropertyTextureBakeStatus::NonFiniteTexcoord:
            return "PropertyTextureBake.NonFiniteTexcoord";
        case PropertyTextureBakeStatus::NonFinitePropertyValue:
            return "PropertyTextureBake.NonFinitePropertyValue";
        case PropertyTextureBakeStatus::DegenerateAllTriangles:
            return "PropertyTextureBake.DegenerateAllTriangles";
        case PropertyTextureBakeStatus::DegenerateUvTriangles:
            return "PropertyTextureBake.DegenerateUvTriangles";
        case PropertyTextureBakeStatus::ZeroCoverageBake:
            return "PropertyTextureBake.ZeroCoverageBake";
        case PropertyTextureBakeStatus::BakeFailed:
            return "PropertyTextureBake.BakeFailed";
        case PropertyTextureBakeStatus::AssetLoadFailed:
            return "PropertyTextureBake.AssetLoadFailed";
        case PropertyTextureBakeStatus::CommandFailed:
            return "PropertyTextureBake.CommandFailed";
        case PropertyTextureBakeStatus::JobSubmitFailed:
            return "PropertyTextureBake.JobSubmitFailed";
        case PropertyTextureBakeStatus::StaleCompletion:
            return "PropertyTextureBake.StaleCompletion";
        }
        return "PropertyTextureBake.Unknown";
    }

    struct TextureBakeService::Impl
    {
        struct BoundContext
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{DefaultWorldHandle};
            std::uint64_t BindingEpoch{0u};
            Assets::AssetService* AssetService{};
            EditorCommandHistory* CommandHistory{};
            JobService* Jobs{};
        };

        enum class WorkPhase : std::uint8_t
        {
            Queued,
            WaitingForReadyFrame,
            Failed,
        };

        struct Work
        {
            WorkPhase Phase{WorkPhase::Queued};
            WorldHandle World{};
            std::uint64_t BindingEpoch{0u};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            std::uint32_t StableEntityId{0u};
            std::string OutputName{};
            Assets::AssetId Asset{};
            std::uint64_t RecordGeneration{0u};
            PropertyTextureBakeRequest Request{};
            PreparedPropertyBake Prepared{};
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            std::optional<RHI::BufferManager::BufferLease> PropertyBuffer{};
            std::optional<RHI::BufferManager::BufferLease> TexcoordBuffer{};
            std::uint64_t CacheGeneration{0u};
            std::uint64_t GeometryRevision{0u};
            std::uint64_t ReadyFrame{0u};
        };

        struct PipelineEntry
        {
            RHI::Format Format{RHI::Format::Undefined};
            std::optional<RHI::PipelineManager::PipelineLease> Lease{};
        };

        BoundContext Context{};
        RuntimeObjectSpaceNormalBakeQueue* LegacyQueue{};
        RHI::IDevice* Device{};
        Graphics::GpuAssetCache* GpuAssets{};
        Graphics::IRenderer* Renderer{};
        RenderExtractionCache* Extraction{};
        TextureBakeModuleStats* Stats{};
        std::vector<Work> WorkItems{};
        std::vector<PipelineEntry> Pipelines{};
        std::uint64_t NextAssetSerial{1u};
        GpuQueueParticipantHandle Participant{};
        std::shared_ptr<void> ProducerLifetime{};

        [[nodiscard]] bool Available() const noexcept
        {
            return Context.Scene != nullptr &&
                   Context.AssetService != nullptr &&
                   Device != nullptr &&
                   GpuAssets != nullptr &&
                   Renderer != nullptr &&
                   Extraction != nullptr &&
                   Participant.IsValid() &&
                   Device->IsOperational();
        }

        [[nodiscard]] PropertyTextureBakeRecord* FindRecord(
            ECS::EntityHandle entity,
            const std::string_view outputName) noexcept
        {
            if (Context.Scene == nullptr ||
                entity == ECS::InvalidEntityHandle)
            {
                return nullptr;
            }
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return nullptr;
            const auto found = std::ranges::find(
                catalog->Records,
                outputName,
                &PropertyTextureBakeRecord::OutputName);
            return found != catalog->Records.end()
                ? &*found
                : nullptr;
        }

        [[nodiscard]] const PropertyTextureBakeRecord* FindRecord(
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            return const_cast<Impl*>(this)->FindRecord(entity, outputName);
        }

        [[nodiscard]] PropertyTextureConsumerRecord* FindConsumerRecord(
            const ECS::EntityHandle entity,
            const std::string_view outputName) noexcept
        {
            if (Context.Scene == nullptr ||
                entity == ECS::InvalidEntityHandle)
            {
                return nullptr;
            }
            auto* bindings = Context.Scene->Raw()
                .try_get<PropertyTextureConsumerBindings>(entity);
            if (bindings == nullptr)
                return nullptr;
            const auto found = std::ranges::find(
                bindings->Records,
                outputName,
                &PropertyTextureConsumerRecord::OutputName);
            return found != bindings->Records.end()
                ? &*found
                : nullptr;
        }

        [[nodiscard]] const PropertyTextureConsumerRecord* FindConsumerRecord(
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            return const_cast<Impl*>(this)->FindConsumerRecord(
                entity,
                outputName);
        }

        [[nodiscard]] std::span<const TextureBakeConsumerBinding>
        ConsumersFor(
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            const PropertyTextureConsumerRecord* record =
                FindConsumerRecord(entity, outputName);
            return record != nullptr
                ? std::span<const TextureBakeConsumerBinding>{
                      record->Consumers}
                : std::span<const TextureBakeConsumerBinding>{};
        }

        [[nodiscard]] bool AssetOwnedByAnotherRecord(
            const Assets::AssetId asset,
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            if (!asset.IsValid() || Context.Scene == nullptr)
                return false;

            auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [owner, catalog] : view.each())
            {
                for (const PropertyTextureBakeRecord& record :
                     catalog.Records)
                {
                    if (owner == entity && record.OutputName == outputName)
                        continue;
                    if (record.Texture == asset)
                        return true;
                }
            }
            return false;
        }

        template <typename T>
        [[nodiscard]] static bool ExactVectorEqual(
            const std::vector<T>& lhs,
            const std::vector<T>& rhs) noexcept
        {
            return lhs.size() == rhs.size() &&
                   (lhs.empty() ||
                    std::memcmp(
                        lhs.data(),
                        rhs.data(),
                        lhs.size() * sizeof(T)) == 0);
        }

        [[nodiscard]] bool SourceStillCurrent(
            const Work& work,
            std::string& diagnostic) const
        {
            const PreparedPropertyBake current = Prepare(work.Request);
            if (!current.Succeeded())
            {
                diagnostic = current.Diagnostic.empty()
                    ? "property texture bake source is no longer valid"
                    : current.Diagnostic;
                return false;
            }
            if (current.ValueKind != work.Prepared.ValueKind ||
                current.Domain != work.Prepared.Domain ||
                !ExactVectorEqual(
                    current.Texcoords,
                    work.Prepared.Texcoords) ||
                !ExactVectorEqual(
                    current.Values,
                    work.Prepared.Values) ||
                !ExactVectorEqual(
                    current.SurfaceIndices,
                    work.Prepared.SurfaceIndices))
            {
                diagnostic =
                    "property texture bake source changed; submit a rebake";
                return false;
            }
            return true;
        }

        [[nodiscard]] PreparedPropertyBake Prepare(
            const PropertyTextureBakeRequest& request) const
        {
            if (Context.Scene == nullptr)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingScene,
                    "texture bake has no active scene");
            }
            if (!request.World.IsValid() ||
                request.World != Context.World)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingScene,
                    "texture bake request does not target the active world");
            }

            if (request.Width == 0u || request.Height == 0u ||
                request.Width > 8192u || request.Height > 8192u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidResolution,
                    "texture bake extent must be within [1, 8192]");
            }
            if (request.PaddingTexels > 32u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidPadding,
                    "texture bake padding must be within [0, 32]");
            }
            if (request.PaddingTexels != 0u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidPadding,
                    "texture bake padding requires the unified dilation producer from RUNTIME-191 Slice B");
            }
            if (!request.Source.HasName())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingProperty,
                    "texture bake source property name must not be empty");
            }
            if (request.Texcoords.Domain !=
                    GeometryElementDomain::MeshVertex ||
                !request.Texcoords.HasName() ||
                (request.Texcoords.ValueKind !=
                     Geometry::PropertyValueKind::Unknown &&
                 request.Texcoords.ValueKind !=
                     Geometry::PropertyValueKind::Vec2))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingTexcoords,
                    "texture bake texcoords must reference a mesh-vertex vec2 property");
            }
            if (request.EncodingColormap >= Graphics::Colormap::Type::Count)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::CommandFailed,
                    "texture bake colormap is invalid");
            }

            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::StaleEntity,
                    "texture bake entity is stale");
            }

            const GS::ConstSourceView view =
                GS::BuildConstView(Context.Scene->Raw(), entity);
            if (view.ActiveDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.EdgeSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::NonMeshSource,
                    "texture bake requires complete mesh topology");
            }

            const Geometry::ConstPropertySet vertexProperties{
                view.VertexSource->Properties};
            const auto texcoords = vertexProperties.Get<glm::vec2>(
                request.Texcoords.Name);
            if (!texcoords.IsValid() ||
                texcoords.Vector().size() !=
                    view.VertexSource->Properties.Size())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingTexcoords,
                    "texture bake requires one vec2 atlas coordinate per vertex");
            }
            for (const glm::vec2 uv : texcoords.Vector())
            {
                if (!Finite(uv))
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::NonFiniteTexcoord,
                        "texture bake atlas coordinates must be finite");
                }
                if (uv.x < -kAtlasEpsilon || uv.y < -kAtlasEpsilon ||
                    uv.x > 1.0f + kAtlasEpsilon ||
                    uv.y > 1.0f + kAtlasEpsilon)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::MissingTexcoords,
                        "texture bake coordinates are not a normalized atlas");
                }
            }

            PreparedPropertyBake prepared{};
            prepared.OutputName = request.OutputName.empty()
                ? request.Source.Name
                : request.OutputName;
            if (prepared.OutputName.empty())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::CommandFailed,
                    "texture bake output name must not be empty");
            }
            prepared.Texcoords = texcoords.Vector();
            prepared.SourceGeneration =
                request.ExpectedSourceGeneration;

            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            prepared.ExpectedElementCount = ResolveGeometryElementCount(
                availability, request.Source.Domain);
            const GeometryPropertyResolution resolution =
                ResolveGeometryProperty(
                    availability,
                    request.Source,
                    prepared.ExpectedElementCount,
                    true,
                    request.ExpectedSourceGeneration);
            if (!resolution.Resolved())
            {
                return PrepareFailure(
                    StatusForResolution(resolution.Status),
                    std::string{ToString(resolution.Status)});
            }
            prepared.ValueKind = resolution.ResolvedValueKind;
            if (request.ExpectedPropertyGeneration != 0u &&
                request.ExpectedPropertyGeneration !=
                    resolution.ObservedSourceGeneration)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::StaleCompletion,
                    "texture bake property generation is stale");
            }

            const Geometry::PropertySet* propertySet =
                ResolveGeometryPropertySet(
                    availability, request.Source.Domain);
            if (propertySet == nullptr ||
                !CopyPropertyValues(
                    Geometry::ConstPropertySet{*propertySet},
                    request.Source.Name,
                    prepared.ValueKind,
                    prepared.Values))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingProperty,
                    "texture bake source property is missing or has changed type");
            }
            if (prepared.Values.size() != prepared.ExpectedElementCount)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MismatchedPropertyCount,
                    "texture bake source property count changed during preparation");
            }
            if (prepared.ValueKind != Geometry::PropertyValueKind::UInt32 &&
                !std::ranges::all_of(
                    prepared.Values,
                    [](const glm::vec4 value) noexcept
                    {
                        return Finite(value);
                    }))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::NonFinitePropertyValue,
                    "texture bake source property contains a non-finite value");
            }

            std::vector<std::uint32_t> triangleToFace{};
            const MeshSurfaceTopologyStatus topology = BuildMeshSurfaceTriangleTopology(
                view,
                prepared.SurfaceIndices,
                triangleToFace);
            if (topology != MeshSurfaceTopologyStatus::Success ||
                prepared.SurfaceIndices.empty() ||
                (prepared.SurfaceIndices.size() % 3u) != 0u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::BakeFailed,
                    DebugNameForMeshSurfaceTopologyStatus(topology));
            }
            prepared.SurfaceIndexFingerprint =
                FingerprintIndices(prepared.SurfaceIndices);

            for (std::size_t index = 0u;
                 index < prepared.SurfaceIndices.size();
                 index += 3u)
            {
                const std::uint32_t a = prepared.SurfaceIndices[index + 0u];
                const std::uint32_t b = prepared.SurfaceIndices[index + 1u];
                const std::uint32_t c = prepared.SurfaceIndices[index + 2u];
                if (a >= texcoords.Vector().size() ||
                    b >= texcoords.Vector().size() ||
                    c >= texcoords.Vector().size())
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::BakeFailed,
                        "texture bake topology references an invalid vertex");
                }
                const float area = std::abs(Cross2(
                    texcoords.Vector()[b] - texcoords.Vector()[a],
                    texcoords.Vector()[c] - texcoords.Vector()[a]));
                if (area <= kUvAreaEpsilon)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::DegenerateUvTriangles,
                        "texture bake atlas contains a degenerate UV triangle");
                }
            }

            switch (request.Source.Domain)
            {
            case GeometryElementDomain::MeshVertex:
                prepared.Domain = Graphics::PropertyTextureBakeDomain::Vertex;
                break;
            case GeometryElementDomain::MeshFace:
            {
                prepared.Domain = Graphics::PropertyTextureBakeDomain::Face;
                std::vector<glm::vec4> expanded{};
                expanded.reserve(triangleToFace.size());
                for (const std::uint32_t face : triangleToFace)
                {
                    if (face >= prepared.Values.size())
                    {
                        return PrepareFailure(
                            PropertyTextureBakeStatus::MismatchedPropertyCount,
                            "texture bake face map exceeds the property buffer");
                    }
                    expanded.push_back(prepared.Values[face]);
                }
                prepared.Values = std::move(expanded);
                break;
            }
            case GeometryElementDomain::MeshEdge:
            {
                prepared.Domain =
                    Graphics::PropertyTextureBakeDomain::NearestEdge;
                const Geometry::ConstPropertySet edgeProperties{
                    view.EdgeSource->Properties};
                const auto edgeV0 = edgeProperties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV0);
                const auto edgeV1 = edgeProperties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV1);
                if (!edgeV0.IsValid() || !edgeV1.IsValid() ||
                    edgeV0.Vector().size() != prepared.Values.size() ||
                    edgeV1.Vector().size() != prepared.Values.size())
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::BakeFailed,
                        "nearest-edge bake requires canonical edge endpoints");
                }
                std::unordered_map<std::uint64_t, std::uint32_t> edgeRows{};
                edgeRows.reserve(prepared.Values.size());
                for (std::uint32_t edge = 0u;
                     edge < prepared.Values.size();
                     ++edge)
                {
                    edgeRows.emplace(
                        EdgeKey(
                            edgeV0.Vector()[edge],
                            edgeV1.Vector()[edge]),
                        edge);
                }
                std::vector<glm::vec4> expanded{};
                expanded.reserve(prepared.SurfaceIndices.size());
                for (std::size_t index = 0u;
                     index < prepared.SurfaceIndices.size();
                     index += 3u)
                {
                    const std::uint32_t a =
                        prepared.SurfaceIndices[index + 0u];
                    const std::uint32_t b =
                        prepared.SurfaceIndices[index + 1u];
                    const std::uint32_t c =
                        prepared.SurfaceIndices[index + 2u];
                    const std::uint64_t keys[3]{
                        EdgeKey(b, c),
                        EdgeKey(c, a),
                        EdgeKey(a, b),
                    };
                    for (const std::uint64_t key : keys)
                    {
                        const auto found = edgeRows.find(key);
                        if (found == edgeRows.end())
                        {
                            return PrepareFailure(
                                PropertyTextureBakeStatus::BakeFailed,
                                "nearest-edge bake could not resolve a triangle edge");
                        }
                        expanded.push_back(prepared.Values[found->second]);
                    }
                }
                prepared.Values = std::move(expanded);
                break;
            }
            case GeometryElementDomain::Unknown:
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::GraphEdge:
            case GeometryElementDomain::PointCloudPoint:
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedSourceDomain,
                    "texture bake supports mesh vertex, edge, and face properties");
            }

            switch (prepared.ValueKind)
            {
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Scalar;
                break;
            case Geometry::PropertyValueKind::UInt32:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Label;
                break;
            case Geometry::PropertyValueKind::Vec2:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector2;
                break;
            case Geometry::PropertyValueKind::Vec3:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector3;
                break;
            case Geometry::PropertyValueKind::Vec4:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector4;
                break;
            case Geometry::PropertyValueKind::Unknown:
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedPropertyType,
                    "texture bake property type is not GPU-rasterizable");
            }

            const PropertyTextureBakeRepresentation representation =
                ResolvePropertyTextureBakeRepresentation(
                    prepared.ValueKind,
                    request.Storage,
                    request.Encoding);
            prepared.Storage = representation.Storage;
            prepared.Encoder = representation.Encoding;
            prepared.EncodingColormap = request.EncodingColormap;
            if (!IsPropertyTextureBakeRepresentationCompatible(
                    prepared.ValueKind,
                    prepared.Storage,
                    prepared.Encoder))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedPropertyType,
                    "texture bake encoder is incompatible with the selected property type and storage");
            }
            if (prepared.Storage == PropertyTextureBakeStorage::RawFloat)
            {
                prepared.GpuEncoding =
                    Graphics::PropertyTextureBakeEncoding::Raw;
                switch (prepared.ValueKind)
                {
                case Geometry::PropertyValueKind::Float:
                case Geometry::PropertyValueKind::Double:
                case Geometry::PropertyValueKind::UInt32:
                    prepared.Format = RHI::Format::R32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Vec2:
                    prepared.Format = RHI::Format::RG32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Vec3:
                case Geometry::PropertyValueKind::Vec4:
                    prepared.Format = RHI::Format::RGBA32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Unknown:
                case Geometry::PropertyValueKind::Bool:
                case Geometry::PropertyValueKind::Int32:
                case Geometry::PropertyValueKind::UInt64:
                    break;
                }
            }
            else
            {
                prepared.Format = RHI::Format::RGBA8_UNORM;
                switch (prepared.Encoder)
                {
                case PropertyTextureBakeEncoding::Normal:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::Normal;
                    break;
                case PropertyTextureBakeEncoding::ScalarColormap:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::ScalarColormap;
                    break;
                case PropertyTextureBakeEncoding::LabelPalette:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::LabelPalette;
                    break;
                case PropertyTextureBakeEncoding::RgbaColor:
                case PropertyTextureBakeEncoding::Vector2:
                case PropertyTextureBakeEncoding::Vector3:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::RgbaColor;
                    break;
                case PropertyTextureBakeEncoding::LinearScalar:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::LinearScalar;
                    break;
                case PropertyTextureBakeEncoding::Auto:
                    break;
                }
            }

            prepared.RangeMin = request.RangeMin;
            prepared.RangeMax = request.RangeMax;
            if (IsScalar(prepared.ValueKind))
            {
                if (request.RangePolicy ==
                    PropertyTextureBakeRangePolicy::AutoFinite)
                {
                    prepared.RangeMin = std::numeric_limits<float>::infinity();
                    prepared.RangeMax = -std::numeric_limits<float>::infinity();
                    for (const glm::vec4 value : prepared.Values)
                    {
                        prepared.RangeMin =
                            std::min(prepared.RangeMin, value.x);
                        prepared.RangeMax =
                            std::max(prepared.RangeMax, value.x);
                    }
                    if (prepared.RangeMin == prepared.RangeMax)
                    {
                        const float delta = std::max(
                            1.0e-6f,
                            std::abs(prepared.RangeMin) * 1.0e-6f);
                        prepared.RangeMin -= delta;
                        prepared.RangeMax += delta;
                    }
                }
                if (!std::isfinite(prepared.RangeMin) ||
                    !std::isfinite(prepared.RangeMax) ||
                    prepared.RangeMin >= prepared.RangeMax)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::InvalidRange,
                        "texture bake scalar range is invalid");
                }
            }
            else
            {
                prepared.RangeMin = 0.0f;
                prepared.RangeMax = 1.0f;
            }

            return prepared;
        }

        [[nodiscard]] bool ValidateConsumerSlots(
            const ECS::EntityHandle entity,
            std::vector<TextureBakeConsumerBinding>& consumers,
            std::string& diagnostic) const
        {
            if (consumers.empty())
                return true;
            if (Context.Scene == nullptr)
            {
                diagnostic = "texture bake has no active scene";
                return false;
            }
            const auto* bindings = Context.Scene->Raw()
                .try_get<GeometryPresentationRecipe>(entity);
            if (bindings == nullptr)
            {
                diagnostic = "texture bake consumers require progressive presentation bindings";
                return false;
            }
            for (TextureBakeConsumerBinding& consumer : consumers)
            {
                if (consumer.Colormap >= Graphics::Colormap::Type::Count)
                {
                    diagnostic = "texture bake consumer colormap is invalid";
                    return false;
                }
                const GeometryPresentationBindingRecipe* presentation =
                    FindPresentation(*bindings, consumer);
                if (presentation == nullptr ||
                    presentation->Kind !=
                        GeometryPresentationKind::SurfaceMaterial ||
                    FindGeometryPresentationSlot(*presentation, consumer.Semantic) == nullptr)
                {
                    diagnostic =
                        "texture bake consumer has no compatible surface slot";
                    return false;
                }
                consumer.PresentationKey = presentation->Key;
            }
            for (std::size_t index = 0u; index < consumers.size(); ++index)
            {
                for (std::size_t other = index + 1u;
                     other < consumers.size();
                     ++other)
                {
                    if (consumers[index].PresentationKey ==
                            consumers[other].PresentationKey &&
                        consumers[index].Semantic == consumers[other].Semantic)
                    {
                        diagnostic =
                            "texture bake consumer list contains a duplicate slot";
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] static std::uint32_t ConsumerChannel(
            const GeometryPresentationSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case GeometryPresentationSlotSemantic::Albedo:
            case GeometryPresentationSlotSemantic::ScalarField:
                return 0u;
            case GeometryPresentationSlotSemantic::Normal:
                return 1u;
            case GeometryPresentationSlotSemantic::Roughness:
            case GeometryPresentationSlotSemantic::Metallic:
                return 2u;
            case GeometryPresentationSlotSemantic::Displacement:
                return 3u;
            case GeometryPresentationSlotSemantic::PointColor:
            case GeometryPresentationSlotSemantic::PointScalarField:
            case GeometryPresentationSlotSemantic::PointSize:
            case GeometryPresentationSlotSemantic::PointNormalOrientation:
            case GeometryPresentationSlotSemantic::LineColor:
            case GeometryPresentationSlotSemantic::LineScalarField:
            case GeometryPresentationSlotSemantic::LineWidth:
                return 4u + static_cast<std::uint32_t>(semantic);
            }
            return std::numeric_limits<std::uint32_t>::max();
        }

        [[nodiscard]] static bool SameConsumerSlot(
            const TextureBakeConsumerBinding& lhs,
            const TextureBakeConsumerBinding& rhs) noexcept
        {
            return lhs.PresentationKey == rhs.PresentationKey &&
                   lhs.Semantic == rhs.Semantic;
        }

        [[nodiscard]] static bool SameConsumerChannel(
            const TextureBakeConsumerBinding& lhs,
            const TextureBakeConsumerBinding& rhs) noexcept
        {
            // MaterialTextureAssetBindings is currently one effective
            // per-renderable snapshot, not one snapshot per progressive
            // presentation. Treat its physical material channels as entity-
            // wide ownership even if two logical slots use different keys.
            return ConsumerChannel(lhs.Semantic) ==
                   ConsumerChannel(rhs.Semantic);
        }

        [[nodiscard]] bool ValidateConsumerOwnership(
            const ECS::EntityHandle entity,
            const std::string_view outputName,
            const std::span<const TextureBakeConsumerBinding> consumers,
            std::string& diagnostic) const
        {
            if (Context.Scene == nullptr)
                return false;
            const auto* bindings = Context.Scene->Raw()
                .try_get<PropertyTextureConsumerBindings>(entity);
            if (bindings == nullptr)
                return true;
            for (const PropertyTextureConsumerRecord& record :
                 bindings->Records)
            {
                if (record.OutputName == outputName)
                    continue;
                for (const TextureBakeConsumerBinding& existing :
                    record.Consumers)
                {
                    for (const TextureBakeConsumerBinding& candidate :
                         consumers)
                    {
                        if (ConsumerChannel(existing.Semantic) ==
                                ConsumerChannel(candidate.Semantic))
                        {
                            diagnostic =
                                "renderer texture channel is already owned by another baked texture";
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        enum class SlotUpdate : std::uint8_t
        {
            Pending,
            Ready,
            PropertyBuffer,
            Failed,
        };

        void UpdateSlots(
            const ECS::EntityHandle entity,
            const PropertyTextureBakeRecord& record,
            const std::span<const TextureBakeConsumerBinding> consumers,
            const SlotUpdate update)
        {
            if (Context.Scene == nullptr)
                return;
            auto& raw = Context.Scene->Raw();
            auto* recipe = raw
                .try_get<GeometryPresentationRecipe>(entity);
            if (recipe == nullptr)
                return;
            auto* runtimeState =
                raw.try_get<GeometryPresentationRuntimeState>(entity);
            if (runtimeState == nullptr)
            {
                runtimeState = &raw.emplace<GeometryPresentationRuntimeState>(
                    entity);
            }

            bool recipeChanged = false;
            for (const TextureBakeConsumerBinding& consumer :
                 consumers)
            {
                GeometryPresentationBindingRecipe* presentation =
                    FindPresentation(*recipe, consumer);
                if (presentation == nullptr)
                    continue;
                GeometryPresentationSlotRecipe* slot =
                    FindGeometryPresentationSlot(*presentation, consumer.Semantic);
                if (slot == nullptr)
                    continue;

                const GeometryPropertyRef property{
                    .Domain = record.Source.Domain,
                    .Name = record.Source.Name,
                    .ValueKind = record.Source.ValueKind,
                };
                const GeometryPresentationSourceKind sourceKind =
                    update == SlotUpdate::Pending ||
                            update == SlotUpdate::Ready
                        ? GeometryPresentationSourceKind::PropertyBake
                        : GeometryPresentationSourceKind::PropertyBuffer;
                recipeChanged = recipeChanged ||
                    slot->Property != property ||
                    slot->SourceKind != sourceKind ||
                    !slot->Enabled;
                slot->Property = property;
                slot->SourceKind = sourceKind;
                slot->Enabled = true;

                GeometryPresentationSlotStatus* status =
                    FindGeometryPresentationSlotStatus(
                        *runtimeState,
                        presentation->Key,
                        consumer.Semantic);
                if (status == nullptr)
                {
                    runtimeState->Slots.push_back(
                        GeometryPresentationSlotStatus{
                            .PresentationKey = presentation->Key,
                            .Semantic = consumer.Semantic,
                        });
                    status = &runtimeState->Slots.back();
                }
                status->SourceGeneration = record.SourceGeneration;
                switch (update)
                {
                case SlotUpdate::Pending:
                    status->Provenance =
                        GeometryPresentationProvenance::PropertyBinding;
                    status->Readiness = GeometryPresentationReadiness::Pending;
                    status->Diagnostic =
                        "GPU property texture bake pending";
                    break;
                case SlotUpdate::Ready:
                    status->GeneratedTexture = record.Texture;
                    status->Provenance = GeometryPresentationProvenance::
                        GeneratedTextureAsset;
                    status->Readiness = GeometryPresentationReadiness::Ready;
                    status->OutputGeneration = record.Generation;
                    status->Diagnostic =
                        "GPU property texture bake ready";
                    break;
                case SlotUpdate::PropertyBuffer:
                    status->GeneratedTexture = {};
                    status->Provenance =
                        GeometryPresentationProvenance::PropertyBuffer;
                    status->Readiness = GeometryPresentationReadiness::Ready;
                    status->Diagnostic =
                        "property texture removed; property-buffer source restored";
                    break;
                case SlotUpdate::Failed:
                    status->GeneratedTexture = {};
                    status->Provenance =
                        GeometryPresentationProvenance::PropertyBuffer;
                    status->Readiness = GeometryPresentationReadiness::Failed;
                    status->Diagnostic = record.Diagnostic;
                    break;
                }
            }
            if (recipeChanged)
                AdvanceGeneration(runtimeState->RecipeGeneration);
        }

        void UpdateSlots(
            const ECS::EntityHandle entity,
            const PropertyTextureBakeRecord& record,
            const SlotUpdate update)
        {
            UpdateSlots(
                entity,
                record,
                ConsumersFor(entity, record.OutputName),
                update);
        }

        void ApplyMaterialConsumers(
            const ECS::EntityHandle entity,
            const PropertyTextureBakeRecord& record,
            const std::span<const TextureBakeConsumerBinding> consumers,
            const bool bind)
        {
            if (Extraction == nullptr)
                return;
            Graphics::MaterialTextureAssetBindings material =
                Extraction->GetMaterialTextureAssetBindings(
                    SelectionController::ToStableEntityId(entity))
                    .value_or(Graphics::MaterialTextureAssetBindings{});
            const bool hasRoughnessConsumer = std::ranges::any_of(
                consumers,
                [](const TextureBakeConsumerBinding& consumer)
                {
                    return consumer.Semantic ==
                        GeometryPresentationSlotSemantic::Roughness;
                });
            const bool hasMetallicConsumer = std::ranges::any_of(
                consumers,
                [](const TextureBakeConsumerBinding& consumer)
                {
                    return consumer.Semantic ==
                        GeometryPresentationSlotSemantic::Metallic;
                });
            if (hasRoughnessConsumer || hasMetallicConsumer)
            {
                if (bind)
                {
                    material.MetallicRoughness = record.Texture;
                    material.RoughnessFromRed = hasRoughnessConsumer;
                    material.MetallicFromRed = hasMetallicConsumer;
                }
                else if (material.MetallicRoughness == record.Texture)
                {
                    material.MetallicRoughness = {};
                    material.RoughnessFromRed = false;
                    material.MetallicFromRed = false;
                }
            }
            for (const TextureBakeConsumerBinding& consumer :
                 consumers)
            {
                switch (consumer.Semantic)
                {
                case GeometryPresentationSlotSemantic::Albedo:
                case GeometryPresentationSlotSemantic::ScalarField:
                    if (bind)
                    {
                        material.Albedo = record.Texture;
                        const bool rawScalar =
                            IsScalar(record.Source.ValueKind) &&
                            record.Storage ==
                                PropertyTextureBakeStorage::RawFloat;
                        material.AlbedoInterpretation = rawScalar
                            ? Graphics::MaterialAlbedoTextureInterpretation::Scalar
                            : Graphics::MaterialAlbedoTextureInterpretation::Color;
                        material.AlbedoScalarColormap = consumer.Colormap;
                        material.AlbedoScalarRangeMin = record.RangeMin;
                        material.AlbedoScalarRangeMax = record.RangeMax;
                    }
                    else if (material.Albedo == record.Texture)
                    {
                        material.Albedo = {};
                        material.AlbedoInterpretation =
                            Graphics::MaterialAlbedoTextureInterpretation::Color;
                    }
                    break;
                case GeometryPresentationSlotSemantic::Normal:
                    if (bind)
                    {
                        material.Normal = record.Texture;
                        material.NormalSpace =
                            consumer.NormalSpace ==
                                PropertyTextureNormalSpace::World
                            ? Graphics::MaterialNormalTextureSpace::
                                  WorldSpaceNormal
                            : Graphics::MaterialNormalTextureSpace::
                                  ObjectSpaceNormal;
                    }
                    else if (material.Normal == record.Texture)
                    {
                        material.Normal = {};
                        material.NormalSpace = Graphics::
                            MaterialNormalTextureSpace::TangentSpaceNormal;
                    }
                    break;
                case GeometryPresentationSlotSemantic::Roughness:
                case GeometryPresentationSlotSemantic::Metallic:
                    break;
                case GeometryPresentationSlotSemantic::Displacement:
                case GeometryPresentationSlotSemantic::PointColor:
                case GeometryPresentationSlotSemantic::PointScalarField:
                case GeometryPresentationSlotSemantic::PointSize:
                case GeometryPresentationSlotSemantic::PointNormalOrientation:
                case GeometryPresentationSlotSemantic::LineColor:
                case GeometryPresentationSlotSemantic::LineScalarField:
                case GeometryPresentationSlotSemantic::LineWidth:
                    break;
                }
            }
            Extraction->SetMaterialTextureAssetBindings(
                SelectionController::ToStableEntityId(entity),
                material);
        }

        void ApplyMaterialConsumers(
            const ECS::EntityHandle entity,
            const PropertyTextureBakeRecord& record,
            const bool bind)
        {
            ApplyMaterialConsumers(
                entity,
                record,
                ConsumersFor(entity, record.OutputName),
                bind);
        }

        void CancelWork(
            const ECS::EntityHandle entity,
            const std::string_view outputName)
        {
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.Entity != entity || work.OutputName != outputName)
                {
                    ++index;
                    continue;
                }
                if (work.CacheGeneration != 0u && GpuAssets != nullptr)
                {
                    (void)GpuAssets->FailGpuProducedTexture(
                        work.Asset,
                        work.CacheGeneration);
                }
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        [[nodiscard]] Core::Expected<Assets::AssetId> CreateOrReloadAsset(
            const std::optional<Assets::AssetId> existing,
            const GeneratedPropertyTextureMetadata& metadata)
        {
            if (Context.AssetService == nullptr)
            {
                return Core::Err<Assets::AssetId>(
                    Core::ErrorCode::InvalidState);
            }
            if (existing.has_value() &&
                existing->IsValid() &&
                Context.AssetService->IsAlive(*existing))
            {
                const Core::Result reloaded = Context.AssetService->Reload<
                    GeneratedPropertyTextureMetadata>(
                        *existing,
                        [metadata](std::string_view, Assets::AssetId)
                            -> Core::Expected<GeneratedPropertyTextureMetadata>
                        {
                            return metadata;
                        });
                if (!reloaded.has_value())
                    return Core::Err<Assets::AssetId>(reloaded.error());
                return *existing;
            }

            const std::string path =
                "intrinsic-runtime-generated/property-texture/v1/entity-" +
                std::to_string(metadata.StableEntityId) + "-" +
                std::to_string(metadata.Serial) + ".metadata";
            return Context.AssetService->Load<GeneratedPropertyTextureMetadata>(
                path,
                [metadata](std::string_view, Assets::AssetId)
                    -> Core::Expected<GeneratedPropertyTextureMetadata>
                {
                    return metadata;
                });
        }

        [[nodiscard]] PropertyTextureBakeResult Schedule(
            const PropertyTextureBakeRequest& request)
        {
            PreparedPropertyBake prepared = Prepare(request);
            if (!prepared.Succeeded())
            {
                return PropertyTextureBakeResult{
                    .Status = prepared.Status,
                    .OutputName = std::move(prepared.OutputName),
                    .Diagnostic = std::move(prepared.Diagnostic),
                };
            }

            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::StaleEntity,
                    .Diagnostic = "texture bake entity is stale",
                };
            }

            auto& catalog = Context.Scene->Raw()
                .get_or_emplace<PropertyTextureBakeOutputs>(entity);
            auto found = std::ranges::find(
                catalog.Records,
                prepared.OutputName,
                &PropertyTextureBakeRecord::OutputName);
            const bool replacing = found != catalog.Records.end();
            if (replacing &&
                (found->Source.Domain != request.Source.Domain ||
                 found->Source.Name != request.Source.Name ||
                 found->Texcoords != request.Texcoords))
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::CommandFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic =
                        "output name already belongs to another property texture; rename it first",
                };
            }
            if (!replacing &&
                request.ExistingGeneratedTexture.IsValid() &&
                AssetOwnedByAnotherRecord(
                    request.ExistingGeneratedTexture,
                    entity,
                    prepared.OutputName))
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::CommandFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic =
                        "existing generated texture is owned by another bake record",
                };
            }
            std::uint64_t serial = NextAssetSerial++;
            if (serial == 0u)
                serial = NextAssetSerial++;
            const GeneratedPropertyTextureMetadata metadata{
                .StableEntityId = request.StableEntityId,
                .OutputName = prepared.OutputName,
                .SourceDomain = request.Source.Domain,
                .SourcePropertyName = request.Source.Name,
                .TexcoordPropertyName = request.Texcoords.Name,
                .ValueKind = prepared.ValueKind,
                .Storage = prepared.Storage,
                .Encoder = prepared.Encoder,
                .EncodingColormap = prepared.EncodingColormap,
                .RangeMin = prepared.RangeMin,
                .RangeMax = prepared.RangeMax,
                .Width = request.Width,
                .Height = request.Height,
                .SourceGeneration = prepared.SourceGeneration,
                .Serial = serial,
            };
            const std::optional<Assets::AssetId> existing = replacing
                ? std::optional<Assets::AssetId>{found->Texture}
                : (request.ExistingGeneratedTexture.IsValid()
                       ? std::optional<Assets::AssetId>{
                             request.ExistingGeneratedTexture}
                       : std::nullopt);
            auto asset = CreateOrReloadAsset(existing, metadata);
            if (!asset.has_value())
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::AssetLoadFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic = "failed to create generated property texture asset",
                };
            }
            CancelWork(entity, prepared.OutputName);

            PropertyTextureBakeRecord record{
                .OutputName = prepared.OutputName,
                .Source = GeometryPropertyRef{
                    .Domain = request.Source.Domain,
                    .Name = request.Source.Name,
                    .ValueKind = prepared.ValueKind,
                },
                .Texcoords = request.Texcoords,
                .Storage = prepared.Storage,
                .Encoding = prepared.Encoder,
                .EncodingColormap = prepared.EncodingColormap,
                .Texture = *asset,
                .ExpectedElementCount = prepared.ExpectedElementCount,
                .SourceGeneration = prepared.SourceGeneration,
                .PropertyGeneration =
                    request.ExpectedPropertyGeneration,
                .RangeMin = prepared.RangeMin,
                .RangeMax = prepared.RangeMax,
                .Width = request.Width,
                .Height = request.Height,
                .PaddingTexels = request.PaddingTexels,
                .Generation = replacing ? found->Generation : 1u,
                .State = PropertyTextureBakeOutputState::Pending,
                .Diagnostic = "GPU property texture bake pending",
            };
            if (replacing)
            {
                AdvanceGeneration(record.Generation);
                *found = record;
            }
            else
            {
                catalog.Records.push_back(record);
            }
            AdvanceGeneration(catalog.Generation);
            const std::span<const TextureBakeConsumerBinding> consumers =
                ConsumersFor(entity, record.OutputName);
            if (!consumers.empty())
            {
                UpdateSlots(
                    entity,
                    record,
                    consumers,
                    SlotUpdate::Pending);
                ApplyMaterialConsumers(
                    entity,
                    record,
                    consumers,
                    false);
            }

            WorkItems.push_back(Work{
                .World = Context.World,
                .BindingEpoch = Context.BindingEpoch,
                .Entity = entity,
                .StableEntityId = request.StableEntityId,
                .OutputName = record.OutputName,
                .Asset = record.Texture,
                .RecordGeneration = record.Generation,
                .Request = request,
                .Prepared = std::move(prepared),
                .Width = request.Width,
                .Height = request.Height,
            });

            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::Scheduled,
                .ExecutionMode =
                    PropertyTextureBakeExecutionMode::PropertyRasterGpu,
                .State = PropertyTextureBakeOutputState::Pending,
                .GeneratedTexture = record.Texture,
                .Generation = record.Generation,
                .SourceGeneration = record.SourceGeneration,
                .OutputName = record.OutputName,
                .Diagnostic = "GPU property texture bake scheduled",
            };
        }

        [[nodiscard]] RHI::PipelineHandle PipelineFor(
            const RHI::Format format)
        {
            if (Renderer == nullptr || Device == nullptr)
                return {};
            for (PipelineEntry& entry : Pipelines)
            {
                if (entry.Format == format &&
                    entry.Lease.has_value() &&
                    entry.Lease->IsValid())
                {
                    return Renderer->GetPipelineManager().GetDeviceHandle(
                        entry.Lease->GetHandle());
                }
            }

            auto lease = Renderer->GetPipelineManager().Create(
                Graphics::MakePropertyTextureBakePipelineDesc(
                    Core::Filesystem::GetShaderPath(
                        "shaders/property_texture_bake.vert.spv"),
                    Core::Filesystem::GetShaderPath(
                        "shaders/property_texture_bake.frag.spv"),
                    format));
            if (!lease.has_value())
                return {};
            Pipelines.push_back(PipelineEntry{
                .Format = format,
                .Lease = std::move(*lease),
            });
            return Renderer->GetPipelineManager().GetDeviceHandle(
                Pipelines.back().Lease->GetHandle());
        }

        void MarkFailed(Work& work, std::string diagnostic)
        {
            if (work.CacheGeneration != 0u && GpuAssets != nullptr)
            {
                (void)GpuAssets->FailGpuProducedTexture(
                    work.Asset,
                    work.CacheGeneration);
            }
            if (PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                record != nullptr &&
                record->Generation == work.RecordGeneration)
            {
                record->State = PropertyTextureBakeOutputState::Failed;
                record->Diagnostic = std::move(diagnostic);
                UpdateSlots(work.Entity, *record, SlotUpdate::Failed);
            }
            work.Phase = WorkPhase::Failed;
        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            if (!Available())
                return;

            std::size_t recorded = 0u;
            for (Work& work : WorkItems)
            {
                if (work.Phase != WorkPhase::Queued || recorded >= 4u)
                    continue;
                if (work.World != Context.World ||
                    work.BindingEpoch != Context.BindingEpoch ||
                    !Context.Scene->IsValid(work.Entity))
                {
                    MarkFailed(work, "property texture bake target became stale");
                    continue;
                }
                const PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                if (record == nullptr ||
                    record->Generation != work.RecordGeneration ||
                    record->Texture != work.Asset)
                {
                    MarkFailed(work, "property texture bake was superseded");
                    continue;
                }
                const auto renderable =
                    Extraction->FindGpuRenderableAvailability(
                        work.StableEntityId);
                if (!renderable.has_value() ||
                    !renderable->HasRenderable ||
                    !renderable->Surface.HasGeometry)
                {
                    continue;
                }
                Graphics::GpuGeometryResidencyView residency{};
                if (!Renderer->GetGpuWorld().TryGetGeometryResidencyView(
                        renderable->Surface.Geometry,
                        residency))
                {
                    continue;
                }
                if (!residency.IndexBuffer.IsValid() ||
                    residency.Record.IndexBufferBDA == 0u ||
                    residency.Record.SurfaceIndexCount !=
                        work.Prepared.SurfaceIndices.size() ||
                    residency.SurfaceIndexCount !=
                        work.Prepared.SurfaceIndices.size() ||
                    residency.SurfaceIndexByteCount !=
                        work.Prepared.SurfaceIndices.size() *
                            sizeof(std::uint32_t) ||
                    residency.SurfaceIndexFingerprint !=
                        work.Prepared.SurfaceIndexFingerprint ||
                    residency.VertexCount !=
                        work.Prepared.Texcoords.size() ||
                    residency.SurfaceIndexFormat != RHI::Format::R32_UINT ||
                    residency.SurfaceIndexElementBytes !=
                        sizeof(std::uint32_t) ||
                    residency.SurfaceIndexStrideBytes !=
                        sizeof(std::uint32_t))
                {
                    MarkFailed(
                        work,
                        "property texture bake geometry lacks matching GPU atlas residency");
                    continue;
                }
                std::string sourceDiagnostic{};
                if (!SourceStillCurrent(work, sourceDiagnostic))
                {
                    MarkFailed(work, std::move(sourceDiagnostic));
                    continue;
                }

                std::uint32_t encodingColormapId = 0u;
                if (work.Prepared.GpuEncoding ==
                    Graphics::PropertyTextureBakeEncoding::ScalarColormap)
                {
                    encodingColormapId = Renderer->GetColormapSystem()
                        .GetBindlessIndex(
                            work.Prepared.EncodingColormap);
                    if (encodingColormapId == RHI::kInvalidBindlessIndex)
                    {
                        // Colormap LUT uploads are asynchronous. Keep the
                        // bake queued until the selected LUT is sampleable.
                        continue;
                    }
                }

                const std::uint64_t texcoordBytes =
                    static_cast<std::uint64_t>(
                        work.Prepared.Texcoords.size()) *
                    sizeof(glm::vec2);
                RHI::BufferDesc texcoordDesc{};
                texcoordDesc.SizeBytes = texcoordBytes;
                texcoordDesc.Usage = RHI::BufferUsage::Storage |
                                     RHI::BufferUsage::TransferDst;
                texcoordDesc.HostVisible = true;
                texcoordDesc.DebugName =
                    "Runtime.PropertyTextureBake.Texcoords";
                auto texcoordBuffer =
                    Renderer->GetBufferManager().Create(texcoordDesc);
                if (!texcoordBuffer.has_value())
                {
                    MarkFailed(
                        work,
                        "property texture bake GPU atlas-buffer allocation failed");
                    continue;
                }
                Device->WriteBuffer(
                    texcoordBuffer->GetHandle(),
                    work.Prepared.Texcoords.data(),
                    texcoordBytes,
                    0u);
                const std::uint64_t texcoordBda =
                    Device->GetBufferDeviceAddress(
                        texcoordBuffer->GetHandle());
                if (texcoordBda == 0u)
                {
                    MarkFailed(
                        work,
                        "property texture bake atlas buffer has no device address");
                    continue;
                }

                const std::uint64_t propertyBytes =
                    static_cast<std::uint64_t>(work.Prepared.Values.size()) *
                    sizeof(glm::vec4);
                RHI::BufferDesc propertyDesc{};
                propertyDesc.SizeBytes = propertyBytes;
                propertyDesc.Usage = RHI::BufferUsage::Storage |
                                     RHI::BufferUsage::TransferDst;
                propertyDesc.HostVisible = true;
                propertyDesc.DebugName = "Runtime.PropertyTextureBake.Values";
                auto propertyBuffer =
                    Renderer->GetBufferManager().Create(propertyDesc);
                if (!propertyBuffer.has_value())
                {
                    MarkFailed(
                        work,
                        "property texture bake GPU value-buffer allocation failed");
                    continue;
                }
                Device->WriteBuffer(
                    propertyBuffer->GetHandle(),
                    work.Prepared.Values.data(),
                    propertyBytes,
                    0u);
                const std::uint64_t propertyBda =
                    Device->GetBufferDeviceAddress(
                        propertyBuffer->GetHandle());
                if (propertyBda == 0u)
                {
                    MarkFailed(
                        work,
                        "property texture bake value buffer has no device address");
                    continue;
                }

                const RHI::PipelineHandle pipeline =
                    PipelineFor(work.Prepared.Format);
                if (!pipeline.IsValid())
                {
                    MarkFailed(
                        work,
                        "property texture bake raster pipeline is unavailable");
                    continue;
                }

                Graphics::GpuProducedTextureRequest textureRequest{};
                textureRequest.Id = work.Asset;
                textureRequest.Desc = RHI::TextureDesc{
                    .Width = work.Width,
                    .Height = work.Height,
                    .MipLevels = 1u,
                    .Fmt = work.Prepared.Format,
                    .Usage = RHI::TextureUsage::Sampled |
                             RHI::TextureUsage::ColorTarget |
                             RHI::TextureUsage::TransferSrc,
                    .InitialLayout = RHI::TextureLayout::Undefined,
                    .DebugName = "Runtime.PropertyTextureBake.Output",
                };
                textureRequest.SamplerDesc = RHI::SamplerDesc{
                    .MagFilter = RHI::FilterMode::Linear,
                    .MinFilter = RHI::FilterMode::Linear,
                    .MipFilter = RHI::MipmapMode::Nearest,
                    .AddressU = RHI::AddressMode::ClampToEdge,
                    .AddressV = RHI::AddressMode::ClampToEdge,
                    .AddressW = RHI::AddressMode::ClampToEdge,
                    .DebugName = "Runtime.PropertyTextureBake.Sampler",
                };
                auto pending =
                    GpuAssets->BeginGpuProducedTexture(textureRequest);
                if (!pending.has_value())
                {
                    if (pending.error() == Core::ErrorCode::ResourceBusy)
                        continue;
                    MarkFailed(
                        work,
                        "property texture bake output allocation failed");
                    continue;
                }

                const Core::Result recordedResult =
                    Graphics::RecordPropertyTextureBake(
                        commandContext,
                        Graphics::PropertyTextureBakeRecordDesc{
                            .Pipeline = pipeline,
                            .OutputTexture = pending->Texture,
                            .IndexBuffer = residency.IndexBuffer,
                            .TexcoordBDA = texcoordBda,
                            .PropertyBDA = propertyBda,
                            .IndexBDA =
                                residency.Record.IndexBufferBDA +
                                static_cast<std::uint64_t>(
                                    residency.Record.SurfaceFirstIndex) *
                                    sizeof(std::uint32_t),
                            .FirstIndex =
                                residency.Record.SurfaceFirstIndex,
                            .IndexCount =
                                residency.Record.SurfaceIndexCount,
                            .Width = work.Width,
                            .Height = work.Height,
                            .Domain = work.Prepared.Domain,
                            .ValueKind = work.Prepared.GpuValueKind,
                            .Encoding = work.Prepared.GpuEncoding,
                            .ColormapID = encodingColormapId,
                            .RangeMin = work.Prepared.RangeMin,
                            .RangeMax = work.Prepared.RangeMax,
                        });
                if (!recordedResult.has_value())
                {
                    work.CacheGeneration = pending->Generation;
                    MarkFailed(
                        work,
                        "property texture bake command recording failed");
                    continue;
                }

                const std::uint64_t readyFrame =
                    Device->GetGlobalFrameNumber() +
                    std::max<std::uint32_t>(
                        Device->GetFramesInFlight(),
                        1u);
                if (Core::Result ready =
                        GpuAssets->SetGpuProducedTextureReadyFrame(
                            work.Asset,
                            pending->Generation,
                            readyFrame);
                    !ready.has_value())
                {
                    work.CacheGeneration = pending->Generation;
                    MarkFailed(
                        work,
                        "property texture bake ready-frame publication failed");
                    continue;
                }

                work.PropertyBuffer.emplace(std::move(*propertyBuffer));
                work.TexcoordBuffer.emplace(std::move(*texcoordBuffer));
                work.CacheGeneration = pending->Generation;
                work.GeometryRevision = residency.ContentRevision;
                work.ReadyFrame = readyFrame;
                work.Phase = WorkPhase::WaitingForReadyFrame;
                ++recorded;
            }

            std::erase_if(
                WorkItems,
                [](const Work& work)
                {
                    return work.Phase == WorkPhase::Failed;
                });
        }

        void DrainCompletedTransfers()
        {
            if (Device == nullptr || GpuAssets == nullptr)
                return;
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.Phase != WorkPhase::WaitingForReadyFrame ||
                    Device->GetGlobalFrameNumber() < work.ReadyFrame)
                {
                    ++index;
                    continue;
                }
                const Graphics::GpuAssetState state =
                    GpuAssets->GetState(work.Asset);
                if (state == Graphics::GpuAssetState::GpuUploading ||
                    state == Graphics::GpuAssetState::CpuPending)
                {
                    ++index;
                    continue;
                }

                PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                const auto view = GpuAssets->GetView(work.Asset);
                bool geometryCurrent = false;
                if (const auto renderable =
                        Extraction->FindGpuRenderableAvailability(
                            work.StableEntityId);
                    renderable.has_value() &&
                    renderable->HasRenderable &&
                    renderable->Surface.HasGeometry)
                {
                    Graphics::GpuGeometryResidencyView residency{};
                    geometryCurrent = Renderer->GetGpuWorld()
                        .TryGetGeometryResidencyView(
                            renderable->Surface.Geometry,
                            residency) &&
                        residency.ContentRevision == work.GeometryRevision;
                }
                const bool current =
                    Context.Scene != nullptr &&
                    work.World == Context.World &&
                    work.BindingEpoch == Context.BindingEpoch &&
                    Context.Scene->IsValid(work.Entity) &&
                    record != nullptr &&
                    record->Generation == work.RecordGeneration &&
                    record->Texture == work.Asset &&
                    geometryCurrent;
                std::string sourceDiagnostic{};
                const bool sourceCurrent =
                    current && SourceStillCurrent(work, sourceDiagnostic);
                if (state == Graphics::GpuAssetState::Ready &&
                    view.has_value() &&
                    view->Generation == work.CacheGeneration &&
                    sourceCurrent)
                {
                    record->State = PropertyTextureBakeOutputState::Ready;
                    record->Diagnostic = "GPU property texture bake ready";
                    UpdateSlots(work.Entity, *record, SlotUpdate::Ready);
                    ApplyMaterialConsumers(work.Entity, *record, true);
                }
                else if (current)
                {
                    record->State = PropertyTextureBakeOutputState::Failed;
                    record->Diagnostic = sourceCurrent
                        ? "GPU property texture bake completion became stale"
                        : std::move(sourceDiagnostic);
                    UpdateSlots(work.Entity, *record, SlotUpdate::Failed);
                }
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        [[nodiscard]] bool HasInFlightWork() const noexcept
        {
            return !WorkItems.empty();
        }

        [[nodiscard]] bool DestroyAsset(const Assets::AssetId asset)
        {
            if (!asset.IsValid())
                return true;
            if (Context.AssetService == nullptr)
                return false;
            if (Context.AssetService->IsAlive(asset))
            {
                if (const Core::Result destroyed =
                        Context.AssetService->Destroy(asset);
                    !destroyed.has_value())
                {
                    return false;
                }
            }
            if (GpuAssets != nullptr)
                GpuAssets->NotifyDestroyed(asset);
            return true;
        }

        void DetachTargets(
            const WorldHandle world,
            const std::uint64_t bindingEpoch,
            const bool destroyGeneratedAssets)
        {
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.World != world || work.BindingEpoch != bindingEpoch)
                {
                    ++index;
                    continue;
                }
                if (work.CacheGeneration != 0u && GpuAssets != nullptr)
                {
                    (void)GpuAssets->FailGpuProducedTexture(
                        work.Asset,
                        work.CacheGeneration);
                }
                if (PropertyTextureBakeRecord* record =
                        FindRecord(work.Entity, work.OutputName);
                    record != nullptr &&
                    record->Generation == work.RecordGeneration)
                {
                    record->State = PropertyTextureBakeOutputState::Failed;
                    record->Diagnostic =
                        "GPU property texture bake cancelled because its scene binding was detached";
                    UpdateSlots(work.Entity, *record, SlotUpdate::Failed);
                    ApplyMaterialConsumers(work.Entity, *record, false);
                }
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }

            if (Context.World == world &&
                Context.BindingEpoch == bindingEpoch &&
                Context.Scene != nullptr)
            {
                auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
                for (auto&& [entity, catalog] : view.each())
                {
                    for (const PropertyTextureBakeRecord& record :
                         catalog.Records)
                    {
                        ApplyMaterialConsumers(entity, record, false);
                        if (destroyGeneratedAssets)
                            (void)DestroyAsset(record.Texture);
                    }
                }
            }
        }

        void RestoreReadyBindings()
        {
            if (Context.Scene == nullptr || Context.AssetService == nullptr)
                return;

            auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [entity, catalog] : view.each())
            {
                for (PropertyTextureBakeRecord& record : catalog.Records)
                {
                    if (record.State != PropertyTextureBakeOutputState::Ready)
                        continue;
                    if (!record.Texture.IsValid() ||
                        !Context.AssetService->IsAlive(record.Texture))
                    {
                        record.State = PropertyTextureBakeOutputState::Failed;
                        record.Diagnostic =
                            "baked property texture asset is no longer alive";
                        UpdateSlots(entity, record, SlotUpdate::Failed);
                        continue;
                    }
                    UpdateSlots(entity, record, SlotUpdate::Ready);
                    ApplyMaterialConsumers(entity, record, true);
                }
            }
        }

        void DestroySceneAssets(ECS::Scene::Registry& scene)
        {
            auto view = scene.Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [entity, catalog] : view.each())
            {
                (void)entity;
                for (const PropertyTextureBakeRecord& record : catalog.Records)
                    (void)DestroyAsset(record.Texture);
            }
        }

        void ShutdownAfterDeviceIdle()
        {
            for (Work& work : WorkItems)
            {
                if (work.CacheGeneration != 0u && GpuAssets != nullptr)
                {
                    (void)GpuAssets->FailGpuProducedTexture(
                        work.Asset,
                        work.CacheGeneration);
                }
            }
            WorkItems.clear();
            Pipelines.clear();
        }

        [[nodiscard]] GpuQueueParticipantDesc MakeParticipantDesc()
        {
            return GpuQueueParticipantDesc{
                .DebugName = "Runtime.PropertyTextureBakeGpuQueue",
                .RecordFrameCommands =
                    [this](RHI::ICommandContext& commandContext)
                    {
                        RecordFrameCommands(commandContext);
                    },
                .DrainCompletedTransfers =
                    [this]
                    {
                        DrainCompletedTransfers();
                    },
                .HasInFlightWork =
                    [this]
                    {
                        return HasInFlightWork();
                    },
                .ShutdownAfterDeviceIdle =
                    [this]
                    {
                        ShutdownAfterDeviceIdle();
                    },
            };
        }

        [[nodiscard]] TextureBakeMutationResult Rename(
            const std::uint32_t stableEntityId,
            const std::string_view currentName,
            const std::string_view newName)
        {
            if (Context.Scene == nullptr)
                return {TextureBakeMutationStatus::MissingScene, "no active scene"};
            if (newName.empty())
                return {TextureBakeMutationStatus::InvalidName, "output name must not be empty"};
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return {TextureBakeMutationStatus::StaleEntity, "entity is stale"};
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            if (std::ranges::any_of(
                    catalog->Records,
                    [newName](const PropertyTextureBakeRecord& record)
                    {
                        return record.OutputName == newName;
                    }))
            {
                return {TextureBakeMutationStatus::DuplicateName, "output name already exists"};
            }
            auto found = std::ranges::find(
                catalog->Records,
                currentName,
                &PropertyTextureBakeRecord::OutputName);
            if (found == catalog->Records.end())
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            const std::string oldName = found->OutputName;
            found->OutputName = std::string{newName};
            AdvanceGeneration(found->Generation);
            AdvanceGeneration(catalog->Generation);
            for (Work& work : WorkItems)
            {
                if (work.Entity == entity && work.OutputName == oldName)
                {
                    work.OutputName = found->OutputName;
                    work.Request.OutputName = found->OutputName;
                    work.RecordGeneration = found->Generation;
                }
            }
            if (PropertyTextureConsumerRecord* bindings =
                    FindConsumerRecord(entity, oldName))
            {
                bindings->OutputName = found->OutputName;
            }
            return {TextureBakeMutationStatus::Success, "baked texture renamed"};
        }

        [[nodiscard]] TextureBakeMutationResult Remove(
            const std::uint32_t stableEntityId,
            const std::string_view outputName)
        {
            if (Context.Scene == nullptr)
                return {TextureBakeMutationStatus::MissingScene, "no active scene"};
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return {TextureBakeMutationStatus::StaleEntity, "entity is stale"};
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            auto found = std::ranges::find(
                catalog->Records,
                outputName,
                &PropertyTextureBakeRecord::OutputName);
            if (found == catalog->Records.end())
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};

            const PropertyTextureBakeRecord removed = *found;
            if (!DestroyAsset(removed.Texture))
            {
                return {
                    TextureBakeMutationStatus::AssetDestroyFailed,
                    "generated texture asset could not be destroyed",
                };
            }
            CancelWork(entity, removed.OutputName);
            const std::span<const TextureBakeConsumerBinding> consumers =
                ConsumersFor(entity, removed.OutputName);
            ApplyMaterialConsumers(entity, removed, consumers, false);
            UpdateSlots(
                entity,
                removed,
                consumers,
                SlotUpdate::PropertyBuffer);
            if (auto* bindings = Context.Scene->Raw()
                    .try_get<PropertyTextureConsumerBindings>(entity))
            {
                std::erase_if(
                    bindings->Records,
                    [outputName](
                        const PropertyTextureConsumerRecord& binding)
                    {
                        return binding.OutputName == outputName;
                    });
            }
            catalog->Records.erase(found);
            AdvanceGeneration(catalog->Generation);
            return {TextureBakeMutationStatus::Success, "baked texture removed"};
        }

        [[nodiscard]] TextureBakeMutationResult SetConsumers(
            const TextureBakeConsumerUpdateRequest& request)
        {
            if (Context.Scene == nullptr)
                return {TextureBakeMutationStatus::MissingScene, "no active scene"};
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return {TextureBakeMutationStatus::StaleEntity, "entity is stale"};
            PropertyTextureBakeRecord* record =
                FindRecord(entity, request.OutputName);
            if (record == nullptr)
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            std::vector<TextureBakeConsumerBinding> nextConsumers =
                request.Consumers;
            for (const TextureBakeConsumerBinding& consumer : nextConsumers)
            {
                if (!ConsumerCompatible(
                        consumer,
                        record->Source.ValueKind,
                        record->Storage,
                        record->Encoding))
                {
                    return {
                        TextureBakeMutationStatus::IncompatibleConsumer,
                        "consumer is incompatible with the baked representation",
                    };
                }
            }
            std::optional<Graphics::Colormap::Type> scalarColormap{};
            for (const TextureBakeConsumerBinding& consumer :
                 nextConsumers)
            {
                if (consumer.Semantic != GeometryPresentationSlotSemantic::Albedo &&
                    consumer.Semantic != GeometryPresentationSlotSemantic::ScalarField)
                {
                    continue;
                }
                if (scalarColormap.has_value() &&
                    *scalarColormap != consumer.Colormap)
                {
                    return {
                        TextureBakeMutationStatus::IncompatibleConsumer,
                        "one scalar texture cannot use different colormaps in the same surface material",
                    };
                }
                scalarColormap = consumer.Colormap;
            }
            std::string diagnostic{};
            if (!ValidateConsumerSlots(entity, nextConsumers, diagnostic) ||
                !ValidateConsumerOwnership(
                    entity,
                    record->OutputName,
                    nextConsumers,
                    diagnostic))
            {
                return {
                    TextureBakeMutationStatus::IncompatibleConsumer,
                    std::move(diagnostic),
                };
            }

            const std::vector<TextureBakeConsumerBinding> beforeConsumers =
                [&]()
                {
                    const auto current =
                        ConsumersFor(entity, record->OutputName);
                    return std::vector<TextureBakeConsumerBinding>{
                        current.begin(),
                        current.end()};
                }();
            ApplyMaterialConsumers(
                entity,
                *record,
                beforeConsumers,
                false);
            UpdateSlots(
                entity,
                *record,
                beforeConsumers,
                SlotUpdate::PropertyBuffer);
            auto& bindings = Context.Scene->Raw()
                .get_or_emplace<PropertyTextureConsumerBindings>(entity);
            PropertyTextureConsumerRecord* binding =
                FindConsumerRecord(entity, record->OutputName);
            if (binding == nullptr)
            {
                bindings.Records.push_back(PropertyTextureConsumerRecord{
                    .OutputName = record->OutputName,
                });
                binding = &bindings.Records.back();
            }
            binding->Consumers = std::move(nextConsumers);
            if (record->State == PropertyTextureBakeOutputState::Ready)
            {
                UpdateSlots(entity, *record, SlotUpdate::Ready);
                ApplyMaterialConsumers(entity, *record, true);
            }
            else if (record->State == PropertyTextureBakeOutputState::Pending)
            {
                UpdateSlots(entity, *record, SlotUpdate::Pending);
            }
            else
            {
                UpdateSlots(entity, *record, SlotUpdate::Failed);
            }
            return {TextureBakeMutationStatus::Success, "texture consumers updated"};
        }
    };

    TextureBakeService::TextureBakeService()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    TextureBakeService::~TextureBakeService() = default;

    bool TextureBakeService::Available() const noexcept
    {
        return m_Impl && m_Impl->Available();
    }

    PropertyTextureBakeResult TextureBakeService::Bake(
        const PropertyTextureBakeRequest& request)
    {
        if (!m_Impl)
            return UnavailableBakeResult();
        if (m_Impl->Stats != nullptr)
            ++m_Impl->Stats->BakeRequests;
        if (!m_Impl->Available())
        {
            if (m_Impl->Stats != nullptr)
                ++m_Impl->Stats->BakeRequestsRejected;
            return UnavailableBakeResult();
        }
        PropertyTextureBakeResult result = m_Impl->Schedule(request);
        if (m_Impl->Stats != nullptr)
        {
            if (result.Succeeded())
                ++m_Impl->Stats->BakeRequestsAccepted;
            else
                ++m_Impl->Stats->BakeRequestsRejected;
        }
        return result;
    }

    TextureBakeProducerContext
    TextureBakeService::ProducerContext() const noexcept
    {
        if (!m_Impl)
            return {};
        return TextureBakeProducerContext{
            .Queue = m_Impl->LegacyQueue,
            .World = m_Impl->Context.World,
            .BindingEpoch = m_Impl->Context.BindingEpoch,
            .Device = m_Impl->Device,
            .Lifetime = m_Impl->ProducerLifetime,
        };
    }

    TextureBakeModuleStats TextureBakeService::Stats() const noexcept
    {
        return m_Impl && m_Impl->Stats != nullptr
            ? *m_Impl->Stats
            : TextureBakeModuleStats{};
    }

    TextureBakeSnapshot TextureBakeService::Snapshot(
        const std::uint32_t stableEntityId) const
    {
        TextureBakeSnapshot snapshot{};
        snapshot.GpuOperational = Available();
        if (!m_Impl || m_Impl->Context.Scene == nullptr)
        {
            snapshot.Diagnostic = "texture-bake module has no active scene";
            return snapshot;
        }
        const ECS::EntityHandle entity = ResolveEntity(
            *m_Impl->Context.Scene,
            stableEntityId);
        if (entity == ECS::InvalidEntityHandle)
        {
            snapshot.Diagnostic = "selected entity is stale";
            return snapshot;
        }
        if (const auto* catalog = m_Impl->Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity))
        {
            snapshot.Textures = catalog->Records;
        }
        if (const auto* bindings = m_Impl->Context.Scene->Raw()
                .try_get<PropertyTextureConsumerBindings>(entity))
        {
            snapshot.ConsumerBindings.reserve(bindings->Records.size());
            for (const PropertyTextureConsumerRecord& binding :
                 bindings->Records)
            {
                snapshot.ConsumerBindings.push_back(
                    TextureBakeConsumerSnapshot{
                        .OutputName = binding.OutputName,
                        .Consumers = binding.Consumers,
                    });
            }
        }
        return snapshot;
    }

    TextureBakeMutationResult TextureBakeService::Rename(
        const std::uint32_t stableEntityId,
        const std::string_view currentName,
        const std::string_view newName)
    {
        return m_Impl
            ? m_Impl->Rename(stableEntityId, currentName, newName)
            : TextureBakeMutationResult{
                  TextureBakeMutationStatus::MissingScene,
                  "texture-bake module is unavailable"};
    }

    TextureBakeMutationResult TextureBakeService::Remove(
        const std::uint32_t stableEntityId,
        const std::string_view outputName)
    {
        return m_Impl
            ? m_Impl->Remove(stableEntityId, outputName)
            : TextureBakeMutationResult{
                  TextureBakeMutationStatus::MissingScene,
                  "texture-bake module is unavailable"};
    }

    TextureBakeMutationResult TextureBakeService::SetConsumers(
        const TextureBakeConsumerUpdateRequest& request)
    {
        return m_Impl
            ? m_Impl->SetConsumers(request)
            : TextureBakeMutationResult{
                  TextureBakeMutationStatus::MissingScene,
                  "texture-bake module is unavailable"};
    }

    void TextureBakeService::Bind(
        ECS::Scene::Registry* const scene,
        const WorldHandle world,
        const std::uint64_t bindingEpoch,
        Assets::AssetService* const assets,
        EditorCommandHistory* const history,
        JobService* const jobs,
        RuntimeObjectSpaceNormalBakeQueue* const queue,
        RHI::IDevice* const device,
        Graphics::GpuAssetCache* const gpuAssets,
        Graphics::IRenderer* const renderer,
        RenderExtractionCache* const extraction,
        TextureBakeModuleStats* const stats) noexcept
    {
        if (!m_Impl)
            return;
        m_Impl->Context = Impl::BoundContext{
            .Scene = scene,
            .World = world,
            .BindingEpoch = bindingEpoch,
            .AssetService = assets,
            .CommandHistory = history,
            .Jobs = jobs,
        };
        m_Impl->LegacyQueue = queue;
        m_Impl->Device = device;
        m_Impl->GpuAssets = gpuAssets;
        m_Impl->Renderer = renderer;
        m_Impl->Extraction = extraction;
        m_Impl->Stats = stats;
    }

    void TextureBakeService::SetTarget(
        const WorldHandle world,
        const std::uint64_t bindingEpoch,
        ECS::Scene::Registry* const scene) noexcept
    {
        if (!m_Impl)
            return;
        m_Impl->ProducerLifetime.reset();
        m_Impl->Context.World = world;
        m_Impl->Context.BindingEpoch = bindingEpoch;
        m_Impl->Context.Scene = scene;
        if (world.IsValid() &&
            bindingEpoch != 0u &&
            scene != nullptr &&
            m_Impl->LegacyQueue != nullptr &&
            m_Impl->Device != nullptr)
        {
            m_Impl->ProducerLifetime =
                std::make_shared<std::uint8_t>(0u);
        }
    }

    void TextureBakeService::SetCommandHistory(
        EditorCommandHistory* const history) noexcept
    {
        if (m_Impl)
            m_Impl->Context.CommandHistory = history;
    }

    void TextureBakeService::DetachTargets(
        const WorldHandle world,
        const std::uint64_t bindingEpoch,
        const bool destroyGeneratedAssets) noexcept
    {
        if (m_Impl)
        {
            m_Impl->DetachTargets(
                world,
                bindingEpoch,
                destroyGeneratedAssets);
        }
    }

    void TextureBakeService::RestoreReadyBindings() noexcept
    {
        if (m_Impl)
            m_Impl->RestoreReadyBindings();
    }

    void TextureBakeService::DestroySceneAssets(
        ECS::Scene::Registry& scene) noexcept
    {
        if (m_Impl)
            m_Impl->DestroySceneAssets(scene);
    }

    GpuQueueParticipantHandle
    TextureBakeService::RegisterGpuQueueParticipant(JobService& jobs)
    {
        if (!m_Impl || m_Impl->Participant.IsValid())
            return {};
        m_Impl->Participant = jobs.RegisterGpuQueueParticipant(
            m_Impl->MakeParticipantDesc());
        return m_Impl->Participant;
    }

    void TextureBakeService::Unbind() noexcept
    {
        if (!m_Impl)
            return;
        m_Impl->ShutdownAfterDeviceIdle();
        m_Impl->Context = {};
        m_Impl->ProducerLifetime.reset();
        m_Impl->LegacyQueue = nullptr;
        m_Impl->Device = nullptr;
        m_Impl->GpuAssets = nullptr;
        m_Impl->Renderer = nullptr;
        m_Impl->Extraction = nullptr;
        m_Impl->Stats = nullptr;
        m_Impl->Participant = {};
    }

    struct TextureBakeModule::Impl
    {
        struct State
        {
            ObjectSpaceNormalBakeService Bake{};
            TextureBakeService Service{};
            TextureBakeModuleStats Stats{};

            KernelEventBus* Events{};
            JobService* Jobs{};
            WorldRegistry* Worlds{};
            SceneDocumentModule* Documents{};
            EditorCommandHistory* History{};
            Assets::AssetService* Assets{};
            Graphics::GpuAssetCache* GpuAssets{};
            Graphics::IRenderer* Renderer{};
            RenderExtractionCache* Extraction{};
            RHI::IDevice* Device{};

            WorldHandle BoundWorld{};
            ECS::Scene::Registry* BoundRegistry{};
            std::uint64_t BindingEpoch{1u};
            SceneReplacementParticipantHandle DocumentParticipant{};
            GpuQueueParticipantHandle GpuParticipant{};
            GpuQueueParticipantHandle PropertyGpuParticipant{};
            bool AcceptingCallbacks{false};
            bool ShutdownAnnounced{false};
            std::weak_ptr<State> Self{};

            void AdvanceBindingEpoch() noexcept
            {
                ++BindingEpoch;
                if (BindingEpoch == 0u)
                    BindingEpoch = 1u;
            }

            [[nodiscard]] bool BindingIsCurrent() const noexcept
            {
                return AcceptingCallbacks &&
                       Worlds != nullptr &&
                       BoundWorld.IsValid() &&
                       BoundRegistry != nullptr &&
                       Worlds->ActiveWorld() == BoundWorld &&
                       Worlds->Get(BoundWorld) == BoundRegistry;
            }

            void PublishBindingChanged()
            {
                ++Stats.BindingChanges;
                if (Events != nullptr)
                {
                    Events->Publish(TextureBakeBindingChanged{
                        .World = BoundWorld,
                        .BindingEpoch = BindingEpoch,
                    });
                }
            }

            void ClearTarget(const bool destroyGeneratedAssets)
            {
                const WorldHandle outgoingWorld = BoundWorld;
                const std::uint64_t outgoingEpoch = BindingEpoch;
                if (outgoingWorld.IsValid() &&
                    outgoingEpoch != 0u)
                {
                    Bake.DetachTargets(outgoingWorld, outgoingEpoch);
                    Service.DetachTargets(
                        outgoingWorld,
                        outgoingEpoch,
                        destroyGeneratedAssets);
                }
                Bake.SetTargetScene({}, 0u, nullptr);
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceBindingEpoch();
                Service.SetTarget({}, BindingEpoch, nullptr);
                PublishBindingChanged();
            }

            void BindTo(const WorldHandle world,
                        ECS::Scene::Registry* const registry)
            {
                // OnRegister publishes the producer target early enough for
                // AssetWorkflowModule::OnResolve.  OnResolve then adds the
                // command-history and document-replacement participants, but
                // does not represent a scene replacement.  Keep that second
                // bind idempotent so consumers do not briefly retain an
                // otherwise-identical stale producer epoch.
                if (AcceptingCallbacks &&
                    world.IsValid() &&
                    registry != nullptr &&
                    world == BoundWorld &&
                    registry == BoundRegistry)
                {
                    return;
                }

                BoundWorld = world;
                BoundRegistry = registry;
                AdvanceBindingEpoch();
                if (!AcceptingCallbacks ||
                    !BoundWorld.IsValid() ||
                    BoundRegistry == nullptr)
                {
                    Bake.SetTargetScene({}, 0u, nullptr);
                    Service.SetTarget({}, BindingEpoch, nullptr);
                    PublishBindingChanged();
                    return;
                }

                Bake.SetTargetScene(
                    BoundWorld,
                    BindingEpoch,
                    BoundRegistry);
                Service.SetTarget(
                    BoundWorld,
                    BindingEpoch,
                    BoundRegistry);
                Service.RestoreReadyBindings();
                PublishBindingChanged();
            }

            [[nodiscard]] bool ValidateBinding()
            {
                if (!AcceptingCallbacks || Worlds == nullptr)
                    return false;
                const WorldHandle active = Worlds->ActiveWorld();
                ECS::Scene::Registry* const registry = Worlds->Get(active);
                if (active != BoundWorld || registry != BoundRegistry)
                {
                    // Inactive worlds retain completed generated assets and
                    // their catalogs.  Rebinding restores their material
                    // consumers when the world becomes active again.
                    ClearTarget(false);
                    BindTo(active, registry);
                }
                return BindingIsCurrent();
            }

            void BeforeDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                if (BoundWorld == context.World &&
                    BoundRegistry == &context.Registry)
                {
                    ClearTarget(true);
                }
            }

            void AfterDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (AcceptingCallbacks)
                    BindTo(context.World, &context.Registry);
            }

            void ReleaseDocumentParticipant() noexcept
            {
                if (Documents != nullptr &&
                    DocumentParticipant.IsValid())
                {
                    (void)Documents->UnregisterReplacementParticipant(
                        DocumentParticipant);
                }
                DocumentParticipant = {};
            }

            void AnnounceShutdown()
            {
                if (ShutdownAnnounced)
                    return;
                ShutdownAnnounced = true;
                AcceptingCallbacks = false;
                ClearTarget(true);
                ReleaseDocumentParticipant();
                Documents = nullptr;
                History = nullptr;
            }
        };

        std::shared_ptr<State> Shared{};
        KernelEventSubscription ActiveWorldChangedSubscription{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        bool ServicePublished{false};

        void Unsubscribe(KernelEventBus* const events) noexcept
        {
            if (events != nullptr)
            {
                if (ActiveWorldChangedSubscription.IsValid())
                    events->Unsubscribe(ActiveWorldChangedSubscription);
                if (WorldDestroyedSubscription.IsValid())
                    events->Unsubscribe(WorldDestroyedSubscription);
                if (ShutdownSubscription.IsValid())
                    events->Unsubscribe(ShutdownSubscription);
            }
            ActiveWorldChangedSubscription = {};
            WorldDestroyedSubscription = {};
            ShutdownSubscription = {};
        }

        void RollBack(KernelEventBus* const events,
                      JobService* const jobs,
                      ServiceRegistry* const services,
                      const bool waitForGpuIdle) noexcept
        {
            if (Shared != nullptr)
            {
                auto& state = *Shared;
                state.AcceptingCallbacks = false;
                state.ReleaseDocumentParticipant();
                if (jobs != nullptr &&
                    state.PropertyGpuParticipant.IsValid())
                {
                    RHI::IDevice* const device = state.Device;
                    jobs->UnregisterGpuQueueParticipant(
                        state.PropertyGpuParticipant,
                        waitForGpuIdle && device != nullptr
                            ? std::function<void()>{[device]
                              {
                                  device->WaitIdle();
                              }}
                            : std::function<void()>{});
                }
                state.PropertyGpuParticipant = {};
                if (jobs != nullptr && state.GpuParticipant.IsValid())
                {
                    RHI::IDevice* const device = state.Device;
                    jobs->UnregisterGpuQueueParticipant(
                        state.GpuParticipant,
                        waitForGpuIdle && device != nullptr
                            ? std::function<void()>{[device]
                              {
                                  device->WaitIdle();
                              }}
                            : std::function<void()>{});
                }
                state.GpuParticipant = {};
                state.Bake.ClearDependencies();
                state.Bake.Queue().Clear();
                state.Service.Unbind();
            }
            Unsubscribe(events);
            if (services != nullptr &&
                ServicePublished &&
                Shared != nullptr)
            {
                (void)services->Withdraw<TextureBakeService>(
                    Shared->Service);
            }
            ServicePublished = false;
            Shared.reset();
        }
    };

    TextureBakeModule::TextureBakeModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    TextureBakeModule::~TextureBakeModule() = default;

    std::string_view TextureBakeModule::Name() const noexcept
    {
        return "Runtime.TextureBakeModule";
    }

    Core::Result TextureBakeModule::OnRegister(EngineSetup& setup)
    {
        if (!m_Impl ||
            m_Impl->Shared ||
            m_Impl->ServicePublished ||
            setup.Services().Phase() != ServiceRegistryPhase::Registration ||
            setup.Services().Find<TextureBakeService>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto assets = setup.Services().Require<Assets::AssetService>(Name());
        auto gpuAssets =
            setup.Services().Require<Graphics::GpuAssetCache>(Name());
        auto renderer = setup.Services().Require<Graphics::IRenderer>(Name());
        auto extraction =
            setup.Services().Require<RenderExtractionCache>(Name());
        auto device = setup.Services().Require<RHI::IDevice>(Name());
        if (!assets.has_value() ||
            !gpuAssets.has_value() ||
            !renderer.has_value() ||
            !extraction.has_value() ||
            !device.has_value())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        m_Impl->Shared = std::make_shared<Impl::State>();
        auto& state = *m_Impl->Shared;
        state.Self = m_Impl->Shared;
        state.Events = &setup.Events();
        state.Jobs = &setup.Jobs();
        state.Worlds = &setup.Worlds();
        state.Assets = &assets->get();
        state.GpuAssets = &gpuAssets->get();
        state.Renderer = &renderer->get();
        state.Extraction = &extraction->get();
        state.Device = &device->get();
        state.Bake.SetDependencies(ObjectSpaceNormalBakeServiceDependencies{
            .Assets = state.Assets,
            .GpuAssets = state.GpuAssets,
            .Renderer = state.Renderer,
            .RenderExtraction = state.Extraction,
            .Device = state.Device,
        });
        state.Service.Bind(
            nullptr,
            {},
            0u,
            state.Assets,
            nullptr,
            state.Jobs,
            &state.Bake.Queue(),
            state.Device,
            state.GpuAssets,
            state.Renderer,
            state.Extraction,
            &state.Stats);

        if (Core::Result provided =
                setup.Services().Provide<TextureBakeService>(
                    state.Service, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->ServicePublished = true;

        const std::weak_ptr<Impl::State> weakState = m_Impl->Shared;
        m_Impl->ActiveWorldChangedSubscription =
            setup.Subscribe<ActiveWorldChanged>(
                [weakState](const ActiveWorldChanged&)
                {
                    if (const auto state = weakState.lock())
                        (void)state->ValidateBinding();
                });
        m_Impl->WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [weakState](const WorldWillBeDestroyed& event)
                {
                    if (const auto state = weakState.lock(); state)
                    {
                        if (event.World == state->BoundWorld)
                        {
                            state->ClearTarget(true);
                        }
                        else if (state->Worlds != nullptr)
                        {
                            if (ECS::Scene::Registry* const scene =
                                    state->Worlds->Get(event.World);
                                scene != nullptr)
                            {
                                state->Service.DestroySceneAssets(*scene);
                            }
                        }
                    }
                });
        m_Impl->ShutdownSubscription =
            setup.Subscribe<RuntimeShutdownAnnounced>(
                [weakState](const RuntimeShutdownAnnounced&)
                {
                    if (const auto state = weakState.lock())
                        state->AnnounceShutdown();
                });
        if (!m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            !m_Impl->WorldDestroyedSubscription.IsValid() ||
            !m_Impl->ShutdownSubscription.IsValid())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (Core::Result hook = setup.RegisterFrameHook(
                FramePhase::Maintenance,
                [weakState](RuntimeFrameHookContext&)
                {
                    if (const auto state = weakState.lock())
                    {
                        (void)state->ValidateBinding();
                        state->Bake.PrepareScheduledRequests();
                    }
                });
            !hook.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return hook;
        }

        // AssetWorkflowModule resolves before this module, but its import
        // producers need the bake queue and the current scene epoch while
        // wiring their dependencies. Publish that producer target during the
        // registration pass; command history is added during OnResolve.
        state.AcceptingCallbacks = true;
        state.BindTo(
            state.Worlds->ActiveWorld(),
            state.Worlds->Get(state.Worlds->ActiveWorld()));

        return Core::Ok();
    }

    Core::Result TextureBakeModule::OnResolve(EngineSetup& setup)
    {
        if (!m_Impl ||
            !m_Impl->Shared ||
            !m_Impl->ServicePublished ||
            setup.Services().Find<TextureBakeService>() !=
                &m_Impl->Shared->Service)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto documents =
            setup.Services().Require<SceneDocumentModule>(Name());
        auto history =
            setup.Services().Require<EditorCommandHistory>(Name());
        if (!documents.has_value() || !history.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        auto& state = *m_Impl->Shared;
        state.Documents = &documents->get();
        state.History = &history->get();
        state.AcceptingCallbacks = true;
        state.Service.SetCommandHistory(state.History);

        const std::weak_ptr<Impl::State> weakState = m_Impl->Shared;
        auto participant = state.Documents->RegisterReplacementParticipant(
            SceneReplacementParticipantDesc{
                // Scene-document participants are ordered by name.  The bake
                // target must bind before AssetWorkflow's AfterReplace reads
                // ProducerContext, while both still detach synchronously in
                // BeforeReplace.
                .Name = "Runtime.AssetTextureBakeModule",
                .BeforeReplace =
                    [weakState](const SceneReplacementContext& context)
                    {
                        if (const auto state = weakState.lock())
                            state->BeforeDocumentReplace(context);
                    },
                .AfterReplace =
                    [weakState](const SceneReplacementContext& context)
                    {
                        if (const auto state = weakState.lock())
                            state->AfterDocumentReplace(context);
                    },
            });
        if (!participant.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(participant.error());
        }
        state.DocumentParticipant = *participant;
        state.BindTo(
            state.Worlds->ActiveWorld(),
            state.Worlds->Get(state.Worlds->ActiveWorld()));
        if (!state.ValidateBinding())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        state.GpuParticipant =
            state.Bake.RegisterGpuQueueParticipant(setup.Jobs());
        if (!state.GpuParticipant.IsValid())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        state.PropertyGpuParticipant =
            state.Service.RegisterGpuQueueParticipant(setup.Jobs());
        if (!state.PropertyGpuParticipant.IsValid())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    void TextureBakeModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;
        if (m_Impl->Shared)
            m_Impl->Shared->AnnounceShutdown();
        m_Impl->RollBack(
            &context.Events,
            &context.Jobs,
            &context.Services,
            false);
    }
}
