module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.ProgressiveRenderData;

export import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    enum class ProgressiveEntityShape : std::uint8_t
    {
        Unknown,
        Composition,
        MeshLeaf,
        GraphLeaf,
        PointCloudLeaf,
    };

    enum class ProgressiveGeometryDomain : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        MeshSurface,
        GraphVertex,
        GraphEdge,
        Point,
    };

    enum class ProgressiveRenderLane : std::uint8_t
    {
        Surface,
        Edges,
        Points,
    };

    enum class ProgressivePropertyValueKind : std::uint8_t
    {
        Any,
        Unknown,
        ScalarFloat,
        ScalarDouble,
        UInt32,
        Vec2,
        Vec3,
        Vec4,
    };

    enum class ProgressivePresentationKind : std::uint8_t
    {
        SurfaceMaterial,
        PointPresentation,
        LinePresentation,
    };

    enum class ProgressiveSlotSemantic : std::uint8_t
    {
        Albedo,
        Normal,
        Roughness,
        Metallic,
        ScalarField,
        Displacement,
        PointColor,
        PointScalarField,
        PointSize,
        PointNormalOrientation,
        LineColor,
        LineScalarField,
        LineWidth,
    };

    enum class ProgressiveSlotSourceKind : std::uint8_t
    {
        UniformDefault,
        AuthoredTextureAsset,
        GeneratedTextureAsset,
        PropertyBake,
        PropertyBuffer,
    };

    enum class ProgressiveReadinessState : std::uint8_t
    {
        Unset,
        DefaultValue,
        Pending,
        Ready,
        Failed,
        Unsupported,
        Stale,
    };

    enum class ProgressiveGeneratedOutputPolicy : std::uint8_t
    {
        SessionCache,
        DeterministicChildAsset,
        PersistOnSave,
    };

    enum class ProgressiveGeneratedOutputProvenance : std::uint8_t
    {
        None,
        AuthoredAsset,
        GeneratedTextureAsset,
        PropertyBuffer,
        UniformDefault,
        PropertyBinding,
    };

    enum class ProgressiveJobDomain : std::uint8_t
    {
        Cpu,
        GpuCompute,
        GpuGraphics,
        Auto,
    };

    enum class ProgressivePropertyResolutionStatus : std::uint8_t
    {
        Compatible,
        MissingProperty,
        TypeMismatch,
        CountMismatch,
        DomainUnavailable,
        UnsupportedDomain,
        UnsupportedType,
        StaleGeneration,
    };

    struct ProgressiveDefaultValue
    {
        ProgressivePropertyValueKind Kind{ProgressivePropertyValueKind::Vec4};
        glm::vec4 Vector{1.0f, 1.0f, 1.0f, 1.0f};
        double Scalar{1.0};
        std::uint32_t UInt{0u};
    };

    struct ProgressivePropertyBindingDescriptor
    {
        ProgressiveGeometryDomain Domain{ProgressiveGeometryDomain::Unknown};
        std::string PropertyName{};
        ProgressivePropertyValueKind ExpectedValueKind{ProgressivePropertyValueKind::Any};
        std::size_t ExpectedElementCount{0u};
        std::uint64_t SourceGeneration{0u};
    };

    struct ProgressivePropertyResolution
    {
        ProgressivePropertyResolutionStatus Status{ProgressivePropertyResolutionStatus::DomainUnavailable};
        ProgressivePropertyValueKind ActualValueKind{ProgressivePropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
        std::uint64_t ObservedSourceGeneration{0u};
        std::string Diagnostic{};

        [[nodiscard]] bool Compatible() const noexcept
        {
            return Status == ProgressivePropertyResolutionStatus::Compatible;
        }
    };

    struct ProgressivePropertyOption
    {
        ProgressivePropertyBindingDescriptor Descriptor{};
        ProgressivePropertyValueKind ActualValueKind{ProgressivePropertyValueKind::Unknown};
        std::size_t ElementCount{0u};
        bool Compatible{false};
        std::string DisabledReason{};
    };

    struct ProgressiveSlotBinding
    {
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveSlotSourceKind SourceKind{ProgressiveSlotSourceKind::UniformDefault};
        ProgressiveDefaultValue UniformDefault{};
        ProgressivePropertyBindingDescriptor Property{};
        Assets::AssetId AuthoredTexture{};
        Assets::AssetId GeneratedTexture{};
        ProgressiveGeneratedOutputPolicy GeneratedPolicy{ProgressiveGeneratedOutputPolicy::DeterministicChildAsset};
        ProgressiveGeneratedOutputProvenance Provenance{ProgressiveGeneratedOutputProvenance::UniformDefault};
        ProgressiveReadinessState Readiness{ProgressiveReadinessState::DefaultValue};
        std::string LastDiagnostic{};
        bool Enabled{true};
    };

    struct ProgressivePresentationBinding
    {
        std::string Key{};
        ProgressivePresentationKind Kind{ProgressivePresentationKind::SurfaceMaterial};
        std::vector<ProgressiveSlotBinding> Slots{};
    };

    struct ProgressiveRenderLaneBinding
    {
        ProgressiveRenderLane Lane{ProgressiveRenderLane::Surface};
        std::string PresentationKey{};
    };

    struct ProgressivePresentationBindings
    {
        ProgressiveEntityShape Shape{ProgressiveEntityShape::Unknown};
        std::vector<ProgressiveRenderLaneBinding> Lanes{};
        std::vector<ProgressivePresentationBinding> Presentations{};
        std::uint64_t BindingGeneration{1u};
    };

    [[nodiscard]] std::string_view ToString(ProgressiveEntityShape value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveGeometryDomain value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveRenderLane value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressivePropertyValueKind value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressivePresentationKind value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveSlotSemantic value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveSlotSourceKind value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveReadinessState value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveGeneratedOutputPolicy value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveGeneratedOutputProvenance value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressiveJobDomain value) noexcept;
    [[nodiscard]] std::string_view ToString(ProgressivePropertyResolutionStatus value) noexcept;

    [[nodiscard]] bool TryParseProgressiveEntityShape(std::string_view value,
                                                      ProgressiveEntityShape& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveGeometryDomain(std::string_view value,
                                                         ProgressiveGeometryDomain& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveRenderLane(std::string_view value,
                                                     ProgressiveRenderLane& out) noexcept;
    [[nodiscard]] bool TryParseProgressivePropertyValueKind(std::string_view value,
                                                            ProgressivePropertyValueKind& out) noexcept;
    [[nodiscard]] bool TryParseProgressivePresentationKind(std::string_view value,
                                                           ProgressivePresentationKind& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveSlotSemantic(std::string_view value,
                                                       ProgressiveSlotSemantic& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveSlotSourceKind(std::string_view value,
                                                         ProgressiveSlotSourceKind& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveGeneratedOutputPolicy(std::string_view value,
                                                                ProgressiveGeneratedOutputPolicy& out) noexcept;
    [[nodiscard]] bool TryParseProgressiveGeneratedOutputProvenance(std::string_view value,
                                                                    ProgressiveGeneratedOutputProvenance& out) noexcept;

    [[nodiscard]] ProgressiveGeneratedOutputPolicy DefaultGeneratedOutputPolicyFor(
        ProgressiveSlotSourceKind sourceKind) noexcept;

    [[nodiscard]] const Geometry::PropertySet* ResolvePropertySet(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        ProgressiveGeometryDomain domain) noexcept;

    [[nodiscard]] std::size_t ResolvePropertyElementCount(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        ProgressiveGeometryDomain domain) noexcept;

    [[nodiscard]] ProgressivePropertyValueKind DetectPropertyValueKind(
        const Geometry::PropertySet& properties,
        std::string_view propertyName);

    [[nodiscard]] ProgressivePropertyResolution ResolvePropertyBinding(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const ProgressivePropertyBindingDescriptor& descriptor,
        std::uint64_t observedSourceGeneration = 0u);

    [[nodiscard]] std::vector<ProgressivePropertyOption> EnumeratePropertyOptions(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        ProgressiveGeometryDomain domain,
        ProgressivePropertyValueKind expectedValueKind = ProgressivePropertyValueKind::Any,
        std::size_t expectedElementCount = 0u,
        std::uint64_t observedSourceGeneration = 0u);

    [[nodiscard]] ProgressiveRenderLaneBinding* FindLaneBinding(
        ProgressivePresentationBindings& bindings,
        ProgressiveRenderLane lane) noexcept;
    [[nodiscard]] const ProgressiveRenderLaneBinding* FindLaneBinding(
        const ProgressivePresentationBindings& bindings,
        ProgressiveRenderLane lane) noexcept;

    [[nodiscard]] ProgressivePresentationBinding* FindPresentationBinding(
        ProgressivePresentationBindings& bindings,
        std::string_view key) noexcept;
    [[nodiscard]] const ProgressivePresentationBinding* FindPresentationBinding(
        const ProgressivePresentationBindings& bindings,
        std::string_view key) noexcept;

    [[nodiscard]] ProgressiveSlotBinding* FindSlotBinding(
        ProgressivePresentationBinding& presentation,
        ProgressiveSlotSemantic semantic) noexcept;
    [[nodiscard]] const ProgressiveSlotBinding* FindSlotBinding(
        const ProgressivePresentationBinding& presentation,
        ProgressiveSlotSemantic semantic) noexcept;
}
