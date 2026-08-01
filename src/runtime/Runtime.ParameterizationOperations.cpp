module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.GeometryProcessingOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.Parameterization;
import Geometry.Properties;

#include "Runtime.EditorMutation.Internal.hpp"
#include "Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = ECS::Components::DirtyTags;
        namespace GS = ECS::Components::GeometrySources;
        namespace Parameterization = Geometry::Parameterization;

        constexpr std::string_view kTexcoordProperty{"v:texcoord"};
        constexpr std::uint32_t kInvalidIndex =
            std::numeric_limits<std::uint32_t>::max();

        struct ParameterizationUvState
        {
            bool Present{false};
            std::vector<glm::vec2> Values{};
        };

        [[nodiscard]] bool IsFiniteUv(const glm::vec2 uv) noexcept
        {
            return std::isfinite(uv.x) && std::isfinite(uv.y);
        }

        [[nodiscard]] bool AllFiniteUvs(
            const std::span<const glm::vec2> values) noexcept
        {
            return std::ranges::all_of(values, IsFiniteUv);
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3 position) noexcept
        {
            return std::isfinite(position.x) && std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] bool AllFinitePositions(
            const std::span<const glm::vec3> values) noexcept
        {
            return std::ranges::all_of(values, IsFinitePosition);
        }

        [[nodiscard]] std::uint64_t ComputeDiagnosticInputFingerprint(
            const std::span<const std::uint32_t> surfaceIndices,
            const std::span<const std::uint32_t> triangleFaces,
            const std::span<const glm::vec3> positions,
            const std::span<const glm::vec2> uvs) noexcept
        {
            constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
            constexpr std::uint64_t kPrime = 1099511628211ull;
            std::uint64_t hash = kOffsetBasis;
            const auto mix = [&hash](const std::uint32_t word) noexcept
            {
                hash ^= static_cast<std::uint64_t>(word);
                hash *= kPrime;
            };
            const auto mixSize = [&mix](const std::size_t size) noexcept
            {
                const std::uint64_t value = static_cast<std::uint64_t>(size);
                mix(static_cast<std::uint32_t>(value));
                mix(static_cast<std::uint32_t>(value >> 32u));
            };

            mixSize(surfaceIndices.size());
            for (const std::uint32_t index : surfaceIndices)
                mix(index);
            mixSize(triangleFaces.size());
            for (const std::uint32_t face : triangleFaces)
                mix(face);
            mixSize(positions.size());
            for (const glm::vec3 position : positions)
            {
                mix(std::bit_cast<std::uint32_t>(position.x));
                mix(std::bit_cast<std::uint32_t>(position.y));
                mix(std::bit_cast<std::uint32_t>(position.z));
            }
            mixSize(uvs.size());
            for (const glm::vec2 uv : uvs)
            {
                mix(std::bit_cast<std::uint32_t>(uv.x));
                mix(std::bit_cast<std::uint32_t>(uv.y));
            }
            return hash;
        }

        [[nodiscard]] bool AlignFaceDiagnosticsToSourceStorage(
            Parameterization::ParameterizationDiagnostics& diagnostics,
            const std::span<const std::uint32_t> sourceFaceForMeshFace,
            const std::size_t sourceFaceStorageCount)
        {
            if (diagnostics.FaceStorageCount !=
                    sourceFaceForMeshFace.size() ||
                diagnostics.FaceConformalDistortion.size() !=
                    sourceFaceForMeshFace.size())
            {
                return false;
            }

            std::vector<float> sourceDistortion(
                sourceFaceStorageCount,
                std::numeric_limits<float>::quiet_NaN());
            std::vector<bool> mapped(sourceFaceStorageCount, false);
            for (std::size_t meshFace = 0u;
                 meshFace < sourceFaceForMeshFace.size();
                 ++meshFace)
            {
                const std::uint32_t sourceFace =
                    sourceFaceForMeshFace[meshFace];
                if (sourceFace >= sourceFaceStorageCount || mapped[sourceFace])
                    return false;
                sourceDistortion[sourceFace] =
                    diagnostics.FaceConformalDistortion[meshFace];
                mapped[sourceFace] = true;
            }

            diagnostics.FaceStorageCount = sourceFaceStorageCount;
            diagnostics.DeletedFaceCount = static_cast<std::size_t>(
                std::ranges::count(mapped, false));
            diagnostics.FaceConformalDistortion =
                std::move(sourceDistortion);
            return true;
        }

        [[nodiscard]] bool CanRepresentAsFloat(const double value) noexcept
        {
            constexpr double limit =
                static_cast<double>(std::numeric_limits<float>::max());
            return std::isfinite(value) && value >= -limit && value <= limit;
        }

        [[nodiscard]] std::optional<glm::vec2> ToFiniteUv(
            const ParameterizationUvConfig& value) noexcept
        {
            if (!CanRepresentAsFloat(value.U) ||
                !CanRepresentAsFloat(value.V))
            {
                return std::nullopt;
            }
            return glm::vec2{static_cast<float>(value.U),
                             static_cast<float>(value.V)};
        }

        [[nodiscard]] std::optional<Parameterization::ParameterizationStrategy>
        ToGeometryStrategy(const ParameterizationConfig& config)
        {
            switch (config.Strategy)
            {
            case ParameterizationStrategyKind::Lscm:
            {
                const auto pinUv0 = ToFiniteUv(config.Lscm.PinUv0);
                const auto pinUv1 = ToFiniteUv(config.Lscm.PinUv1);
                if (!pinUv0.has_value() || !pinUv1.has_value() ||
                    !std::isfinite(config.Lscm.SolverTolerance) ||
                    config.Lscm.SolverTolerance <= 0.0 ||
                    config.Lscm.SolverTolerance > 1.0e30 ||
                    config.Lscm.MaxSolverIterations == 0u ||
                    (!config.Lscm.AutoPins &&
                     config.Lscm.PinVertex0 == config.Lscm.PinVertex1))
                {
                    return std::nullopt;
                }

                Parameterization::ParameterizationParams params{};
                if (!config.Lscm.AutoPins)
                {
                    params.PinVertex0 = config.Lscm.PinVertex0;
                    params.PinVertex1 = config.Lscm.PinVertex1;
                }
                params.PinUV0 = *pinUv0;
                params.PinUV1 = *pinUv1;
                params.SolverTolerance = config.Lscm.SolverTolerance;
                params.MaxSolverIterations =
                    config.Lscm.MaxSolverIterations;
                return Parameterization::ParameterizationStrategy{
                    std::move(params)};
            }
            case ParameterizationStrategyKind::HarmonicCotangent:
            case ParameterizationStrategyKind::TutteUniform:
            {
                if (config.Harmonic.PinnedVertices.size() !=
                    config.Harmonic.PinnedUvs.size())
                {
                    return std::nullopt;
                }

                Parameterization::HarmonicParams params{};
                params.Weights =
                    config.Strategy ==
                            ParameterizationStrategyKind::TutteUniform
                        ? Parameterization::HarmonicWeightType::Uniform
                        : Parameterization::HarmonicWeightType::Cotangent;
                switch (config.Harmonic.Boundary)
                {
                case ParameterizationBoundaryPolicy::Circle:
                    params.Boundary =
                        Parameterization::HarmonicBoundaryPolicy::Circle;
                    break;
                case ParameterizationBoundaryPolicy::Square:
                    params.Boundary =
                        Parameterization::HarmonicBoundaryPolicy::Square;
                    break;
                case ParameterizationBoundaryPolicy::Custom:
                    params.Boundary =
                        Parameterization::HarmonicBoundaryPolicy::Custom;
                    break;
                default:
                    return std::nullopt;
                }
                params.ArcLengthSpacing = config.Harmonic.ArcLengthSpacing;
                params.ClampNonConvexWeights =
                    config.Harmonic.ClampNonConvexWeights;
                params.PinnedVertices = config.Harmonic.PinnedVertices;
                params.PinnedUVs.reserve(config.Harmonic.PinnedUvs.size());
                for (const ParameterizationUvConfig& uv :
                     config.Harmonic.PinnedUvs)
                {
                    const auto converted = ToFiniteUv(uv);
                    if (!converted.has_value())
                        return std::nullopt;
                    params.PinnedUVs.push_back(*converted);
                }
                return Parameterization::ParameterizationStrategy{
                    std::move(params)};
            }
            case ParameterizationStrategyKind::Bff:
            {
                if (!std::isfinite(config.Bff.AngleSumTolerance) ||
                    config.Bff.AngleSumTolerance <= 0.0 ||
                    config.Bff.AngleSumTolerance > 1.0e30 ||
                    !std::isfinite(config.Bff.DegeneracyTolerance) ||
                    config.Bff.DegeneracyTolerance <= 0.0 ||
                    config.Bff.DegeneracyTolerance > 1.0e30)
                {
                    return std::nullopt;
                }

                Parameterization::BffParams params{};
                switch (config.Bff.Mode)
                {
                case ParameterizationBffBoundaryMode::AutomaticConformal:
                    if (!config.Bff.BoundaryData.empty())
                        return std::nullopt;
                    params.Mode =
                        Parameterization::BffBoundaryMode::AutomaticConformal;
                    break;
                case ParameterizationBffBoundaryMode::TargetLengths:
                    if (config.Bff.BoundaryData.empty())
                        return std::nullopt;
                    params.Mode =
                        Parameterization::BffBoundaryMode::TargetLengths;
                    break;
                case ParameterizationBffBoundaryMode::TargetAngles:
                    if (config.Bff.BoundaryData.empty())
                        return std::nullopt;
                    params.Mode =
                        Parameterization::BffBoundaryMode::TargetAngles;
                    break;
                default:
                    return std::nullopt;
                }
                for (const double value : config.Bff.BoundaryData)
                {
                    if (!std::isfinite(value) ||
                        (params.Mode ==
                             Parameterization::BffBoundaryMode::TargetLengths &&
                         value <= 0.0))
                    {
                        return std::nullopt;
                    }
                }
                if (params.Mode ==
                    Parameterization::BffBoundaryMode::TargetAngles)
                {
                    double angleSum = 0.0;
                    for (const double angle : config.Bff.BoundaryData)
                        angleSum += angle;
                    if (!std::isfinite(angleSum) ||
                        std::abs(angleSum -
                                 2.0 * std::numbers::pi_v<double>) >
                            config.Bff.AngleSumTolerance)
                    {
                        return std::nullopt;
                    }
                }
                params.BoundaryData = config.Bff.BoundaryData;
                params.AngleSumTolerance = config.Bff.AngleSumTolerance;
                params.DegeneracyTolerance = config.Bff.DegeneracyTolerance;
                return Parameterization::ParameterizationStrategy{
                    std::move(params)};
            }
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] bool IsSerializableParameterizationConfigValid(
            const ParameterizationConfig& config)
        {
            if (StableTokenForEditorParameterizationStrategy(
                    config.Strategy).empty())
            {
                return false;
            }
            ParameterizationConfig candidate = config;
            for (const ParameterizationStrategyKind strategy : {
                     ParameterizationStrategyKind::Lscm,
                     ParameterizationStrategyKind::HarmonicCotangent,
                     ParameterizationStrategyKind::Bff})
            {
                candidate.Strategy = strategy;
                if (!ToGeometryStrategy(candidate).has_value())
                    return false;
            }
            switch (config.View.RenderMode)
            {
            case ParameterizationUvRenderMode::CpuLayout:
            case ParameterizationUvRenderMode::GpuShaded:
                break;
            default:
                return false;
            }
            switch (config.View.BackgroundMode)
            {
            case ParameterizationUvBackgroundMode::Grid:
            case ParameterizationUvBackgroundMode::Checker:
            case ParameterizationUvBackgroundMode::TexelDensity:
            case ParameterizationUvBackgroundMode::Texture:
                break;
            default:
                return false;
            }
            return true;
        }

        [[nodiscard]] bool CollectTriangleIndices(
            const GS::ConstSourceView& view,
            std::vector<std::array<std::uint32_t, 3u>>& triangles,
            std::string& diagnostic)
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                diagnostic =
                    "Parameterization requires selected mesh GeometrySources.";
                return false;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            const auto deletedFaces =
                view.FaceSource->Properties.Get<bool>("f:deleted");
            const std::size_t vertexCount =
                view.VertexSource->Properties.Size();
            const std::size_t halfedgeCount =
                view.HalfedgeSource->Properties.Size();
            const std::size_t faceCount = view.FaceSource->Properties.Size();
            if (!toVertices || !nextHalfedges || !halfedgeFaces ||
                !faceHalfedges ||
                toVertices.Vector().size() != halfedgeCount ||
                nextHalfedges.Vector().size() != halfedgeCount ||
                halfedgeFaces.Vector().size() != halfedgeCount ||
                faceHalfedges.Vector().size() != faceCount ||
                (deletedFaces && deletedFaces.Vector().size() != faceCount))
            {
                diagnostic =
                    "Parameterization requires count-matched mesh topology properties.";
                return false;
            }

            triangles.clear();
            triangles.reserve(faceCount);
            for (std::size_t faceIndex = 0u; faceIndex < faceCount; ++faceIndex)
            {
                if (deletedFaces && deletedFaces.Vector()[faceIndex])
                    continue;
                const std::uint32_t first =
                    faceHalfedges.Vector()[faceIndex];
                if (first == kInvalidIndex || first >= halfedgeCount)
                {
                    diagnostic =
                        "Parameterization requires valid triangle face rings.";
                    return false;
                }

                std::array<std::uint32_t, 3u> triangle{};
                std::uint32_t current = first;
                std::size_t count = 0u;
                do
                {
                    if (current >= halfedgeCount || count >= triangle.size() ||
                        halfedgeFaces.Vector()[current] != faceIndex ||
                        toVertices.Vector()[current] >= vertexCount)
                    {
                        diagnostic =
                            "Parameterization accepts triangle faces only.";
                        return false;
                    }
                    triangle[count++] = toVertices.Vector()[current];
                    current = nextHalfedges.Vector()[current];
                    if (current == kInvalidIndex)
                    {
                        diagnostic =
                            "Parameterization requires closed triangle face rings.";
                        return false;
                    }
                } while (current != first);

                if (count != triangle.size())
                {
                    diagnostic =
                        "Parameterization accepts triangle faces only.";
                    return false;
                }
                triangles.push_back(triangle);
            }
            if (triangles.empty())
            {
                diagnostic = "Parameterization requires at least one triangle.";
                return false;
            }
            return true;
        }

        void HashUvViewTokenValue(
            std::uint64_t& token,
            const std::uint64_t value) noexcept
        {
            constexpr std::uint64_t prime = 1099511628211ull;
            for (std::uint32_t shift = 0u; shift < 64u; shift += 8u)
            {
                token ^= (value >> shift) & 0xFFu;
                token *= prime;
            }
        }

        [[nodiscard]] std::uint64_t BuildUvViewRequestToken(
            const EditorParameterizationViewModel& model,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            std::uint64_t token = 1469598103934665603ull;
            HashUvViewTokenValue(token, model.SelectedStableEntityId);
            HashUvViewTokenValue(token, width);
            HashUvViewTokenValue(token, height);
            HashUvViewTokenValue(
                token,
                static_cast<std::uint32_t>(model.View.RenderMode));
            HashUvViewTokenValue(
                token,
                static_cast<std::uint32_t>(model.View.BackgroundMode));
            HashUvViewTokenValue(
                token,
                model.View.ShowDistortionHeatmap ? 1u : 0u);
            for (const float bound : {
                     model.UvBoundsMin.x,
                     model.UvBoundsMin.y,
                     model.UvBoundsMax.x,
                     model.UvBoundsMax.y})
            {
                HashUvViewTokenValue(token, std::bit_cast<std::uint32_t>(bound));
            }
            for (const std::uint32_t index : model.LineIndices)
                HashUvViewTokenValue(token, index);
            for (const glm::vec2 uv : model.UVs)
            {
                HashUvViewTokenValue(
                    token,
                    std::bit_cast<std::uint32_t>(uv.x));
                HashUvViewTokenValue(
                    token,
                    std::bit_cast<std::uint32_t>(uv.y));
            }
            for (const float value : model.TriangleConformalDistortion)
            {
                HashUvViewTokenValue(
                    token,
                    std::bit_cast<std::uint32_t>(value));
            }
            return token;
        }

        [[nodiscard]] std::optional<ParameterizationUvState> CaptureUvState(
            const GS::ConstSourceView& view)
        {
            if (view.VertexSource == nullptr)
                return std::nullopt;
            const Geometry::PropertySet& properties =
                view.VertexSource->Properties;
            if (!properties.Exists(kTexcoordProperty))
                return ParameterizationUvState{};
            const auto uvs = properties.Get<glm::vec2>(kTexcoordProperty);
            if (!uvs || uvs.Vector().size() != properties.Size() ||
                !AllFiniteUvs(uvs.Vector()))
            {
                return std::nullopt;
            }
            return ParameterizationUvState{
                .Present = true,
                .Values = uvs.Vector(),
            };
        }

        struct ParameterizationSourceState
        {
            std::vector<std::uint32_t> SurfaceIndices{};
            std::vector<std::uint32_t> TriangleFaces{};
            std::vector<glm::vec3> Positions{};
            ParameterizationUvState Uv{};
        };

        using ParameterizationSourceSnapshot =
            std::shared_ptr<const ParameterizationSourceState>;
        using ParameterizationUvSnapshot =
            std::shared_ptr<const ParameterizationUvState>;

        struct ParameterizationMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        struct ParameterizationMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            ParameterizationSourceSnapshot Source{};
        };

        [[nodiscard]] bool SameUvState(
            const ParameterizationUvState& lhs,
            const ParameterizationUvState& rhs) noexcept
        {
            if (lhs.Present != rhs.Present ||
                lhs.Values.size() != rhs.Values.size())
            {
                return false;
            }
            for (std::size_t i = 0u; i < lhs.Values.size(); ++i)
            {
                if (std::bit_cast<std::uint32_t>(lhs.Values[i].x) !=
                        std::bit_cast<std::uint32_t>(rhs.Values[i].x) ||
                    std::bit_cast<std::uint32_t>(lhs.Values[i].y) !=
                        std::bit_cast<std::uint32_t>(rhs.Values[i].y))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SamePositions(
            const std::span<const glm::vec3> lhs,
            const std::span<const glm::vec3> rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (std::bit_cast<std::uint32_t>(lhs[i].x) !=
                        std::bit_cast<std::uint32_t>(rhs[i].x) ||
                    std::bit_cast<std::uint32_t>(lhs[i].y) !=
                        std::bit_cast<std::uint32_t>(rhs[i].y) ||
                    std::bit_cast<std::uint32_t>(lhs[i].z) !=
                        std::bit_cast<std::uint32_t>(rhs[i].z))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] ParameterizationSourceSnapshot
        CaptureParameterizationSourceState(
            const GS::ConstSourceView& view)
        {
            if (GS::BuildSourceAvailability(view).ProvenanceDomain !=
                    GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return nullptr;
            }

            std::vector<std::uint32_t> surfaceIndices{};
            std::vector<std::uint32_t> triangleFaces{};
            if (BuildMeshSurfaceTriangleTopology(
                    view,
                    surfaceIndices,
                    triangleFaces) != MeshSurfaceTopologyStatus::Success)
            {
                return nullptr;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            const std::optional<ParameterizationUvState> uv =
                CaptureUvState(view);
            if (!positions ||
                positions.Vector().size() !=
                    view.VertexSource->Properties.Size() ||
                !AllFinitePositions(positions.Vector()) ||
                !uv.has_value())
            {
                return nullptr;
            }

            return std::make_shared<ParameterizationSourceState>(
                ParameterizationSourceState{
                    .SurfaceIndices = std::move(surfaceIndices),
                    .TriangleFaces = std::move(triangleFaces),
                    .Positions = positions.Vector(),
                    .Uv = *uv,
                });
        }

        [[nodiscard]] bool SameParameterizationSourceState(
            const ParameterizationSourceState& lhs,
            const ParameterizationSourceState& rhs) noexcept
        {
            return lhs.SurfaceIndices == rhs.SurfaceIndices &&
                   lhs.TriangleFaces == rhs.TriangleFaces &&
                   SamePositions(lhs.Positions, rhs.Positions) &&
                   SameUvState(lhs.Uv, rhs.Uv);
        }

        [[nodiscard]] bool RestoreDeletedVertexSlots(
            GeometryProcessingDetail::EditorMeshSourceSnapshot& source,
            std::string& diagnostic)
        {
            if (source.DeletedVertices.size() != source.Mesh.VerticesSize())
            {
                diagnostic =
                    "Parameterization requires count-matched deleted-vertex state.";
                return false;
            }
            for (std::size_t i = 0u; i < source.DeletedVertices.size(); ++i)
            {
                if (!source.DeletedVertices[i])
                    continue;
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (!source.Mesh.IsIsolated(vertex))
                {
                    diagnostic =
                        "Parameterization rejects live triangles that reference a deleted vertex.";
                    return false;
                }
                source.Mesh.DeleteVertex(vertex);
            }
            return true;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyUvState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ParameterizationUvState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;
            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                GeometryProcessingDetail::ResolveEditorStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            if (GS::BuildSourceAvailability(view).ProvenanceDomain !=
                    GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            Geometry::PropertySet& properties = view.VertexSource->Properties;
            const auto existingId =
                properties.Registry().Find(kTexcoordProperty);
            if (!state.Present)
            {
                if (existingId.has_value() &&
                    !properties.Registry().Remove(*existingId))
                {
                    return EditorCommandHistoryStatus::CommandFailed;
                }
            }
            else
            {
                if (state.Values.size() != properties.Size() ||
                    !AllFiniteUvs(state.Values))
                {
                    return EditorCommandHistoryStatus::CommandFailed;
                }
                if (existingId.has_value() &&
                    !properties.Get<glm::vec2>(kTexcoordProperty))
                {
                    return EditorCommandHistoryStatus::CommandFailed;
                }
                auto target = properties.GetOrAdd<glm::vec2>(
                    std::string{kTexcoordProperty},
                    glm::vec2{0.0f});
                if (!target || target.Vector().size() != state.Values.size())
                    return EditorCommandHistoryStatus::CommandFailed;
                target.Vector() = state.Values;
            }

            return EditorCommandHistoryStatus::Applied;
        }

        void StampUvStateDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const std::optional<ECS::EntityHandle> entity =
                GeometryProcessingDetail::ResolveEditorStableEntity(
                    raw,
                    stableEntityId);
            if (!entity.has_value())
                return;
            Dirty::MarkVertexTexcoordsDirty(raw, *entity);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
        }

        [[nodiscard]] EditorCommandStatus CommitUvState(
            const EditorGeometryProcessingContext& context,
            const std::uint32_t stableEntityId,
            const std::uint64_t expectedGeometryMetadataSignature,
            ParameterizationSourceSnapshot expectedSource,
            ParameterizationUvState before,
            ParameterizationUvState after)
        {
            if (context.CommandHistory == nullptr)
            {
                const EditorCommandStatus status =
                    GeometryProcessingDetail::ToEditorMethodCommandStatus(
                        ApplyUvState(
                            context.Scene,
                            stableEntityId,
                            after));
                if (status == EditorCommandStatus::Applied)
                {
                    StampUvStateDirty(
                        *context.Scene,
                        stableEntityId);
                    if (context.InvalidateWorkspaceSnapshotCache)
                        context.InvalidateWorkspaceSnapshotCache();
                }
                return status;
            }

            const ParameterizationUvSnapshot beforeState =
                std::make_shared<ParameterizationUvState>(
                    std::move(before));
            const ParameterizationUvSnapshot afterState =
                std::make_shared<ParameterizationUvState>(
                    std::move(after));
            const EditorCommandHistoryResult history =
                Internal::ExecuteUndoableEntityMutation(
                    *context.CommandHistory,
                    "Parameterize mesh UVs",
                    ParameterizationMutationIdentity{
                        .Scene = context.Scene,
                        .World = context.World,
                        .StableEntityId = stableEntityId,
                    },
                    ParameterizationMutationGeneration{
                        .GeometryMetadataSignature =
                            expectedGeometryMetadataSignature,
                        .Source = std::move(expectedSource),
                    },
                    beforeState,
                    afterState,
                    [](
                        const ParameterizationMutationIdentity& identity,
                        const ParameterizationMutationGeneration& expected,
                        const ParameterizationUvSnapshot& target)
                    {
                        if (identity.Scene == nullptr ||
                            !identity.World.IsValid())
                        {
                            return EditorCommandHistoryStatus::MissingScene;
                        }

                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            GeometryProcessingDetail::ResolveEditorStableEntity(
                                raw,
                                identity.StableEntityId);
                        if (!entity.has_value())
                            return EditorCommandHistoryStatus::StaleEntity;

                        const GS::ConstSourceView view =
                            GS::BuildConstView(raw, *entity);
                        if (GS::BuildSourceAvailability(view).ProvenanceDomain !=
                            GS::Domain::Mesh)
                        {
                            return EditorCommandHistoryStatus::
                                UnsupportedOperation;
                        }
                        if (expected.Source == nullptr || target == nullptr)
                            return EditorCommandHistoryStatus::CommandFailed;
                        if (GeometryProcessingDetail::
                                EditorGeometryMetadataSignatureForEntity(
                                    raw,
                                    *entity) !=
                                expected.GeometryMetadataSignature)
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }

                        const ParameterizationSourceSnapshot current =
                            CaptureParameterizationSourceState(view);
                        if (current == nullptr ||
                            !SameParameterizationSourceState(
                                *current,
                                *expected.Source))
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }
                        return EditorCommandHistoryStatus::Applied;
                    },
                    [](
                        const ParameterizationMutationIdentity& identity,
                        const ParameterizationUvSnapshot& target)
                    {
                        if (target == nullptr)
                            return EditorCommandHistoryStatus::CommandFailed;
                        return ApplyUvState(
                            identity.Scene,
                            identity.StableEntityId,
                            *target);
                    },
                    [](
                        const ParameterizationMutationIdentity& identity,
                        const ParameterizationMutationGeneration&,
                        const ParameterizationUvSnapshot&)
                    {
                        StampUvStateDirty(
                            *identity.Scene,
                            identity.StableEntityId);
                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            GeometryProcessingDetail::ResolveEditorStableEntity(
                                raw,
                                identity.StableEntityId);
                        return ParameterizationMutationGeneration{
                            .GeometryMetadataSignature =
                                entity.has_value()
                                    ? GeometryProcessingDetail::
                                          EditorGeometryMetadataSignatureForEntity(
                                              raw,
                                              *entity)
                                    : 0u,
                            .Source =
                                entity.has_value()
                                    ? CaptureParameterizationSourceState(
                                          GS::BuildConstView(
                                              raw,
                                              *entity))
                                    : nullptr,
                        };
                    });
            const EditorCommandStatus status =
                GeometryProcessingDetail::ToEditorMethodCommandStatus(history.Status);
            if (status == EditorCommandStatus::Applied)
                if (context.InvalidateWorkspaceSnapshotCache)
                    context.InvalidateWorkspaceSnapshotCache();
            return status;
        }

        [[nodiscard]] EditorParameterizationResult MakeResult(
            const EditorParameterizationCommand& command,
            const EditorCommandStatus status,
            const Parameterization::ParameterizationStatus parameterizationStatus,
            std::string message)
        {
            return EditorParameterizationResult{
                .Status = status,
                .StableEntityId = command.StableEntityId,
                .Strategy = command.Config.Strategy,
                .StrategyToken = std::string{
                    StableTokenForEditorParameterizationStrategy(
                        command.Config.Strategy)},
                .ParameterizationStatus = parameterizationStatus,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] EditorParameterizationResult PublishResult(
            const EditorGeometryProcessingContext& context,
            EditorParameterizationResult result)
        {
            if (context.MethodResultSinks.Parameterization)
                context.MethodResultSinks.Parameterization(result);
            return result;
        }
    }

    std::string_view StableTokenForEditorParameterizationStrategy(
        const EditorParameterizationStrategy strategy) noexcept
    {
        switch (strategy)
        {
        case EditorParameterizationStrategy::Lscm:
            return "lscm";
        case EditorParameterizationStrategy::HarmonicCotangent:
            return "harmonic_cotangent";
        case EditorParameterizationStrategy::TutteUniform:
            return "tutte_uniform";
        case EditorParameterizationStrategy::Bff:
            return "bff";
        }
        return {};
    }

    EditorParameterizationResult
    ApplyEditorParameterizationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorParameterizationCommand& command)
    {
        const auto finish = [&context](EditorParameterizationResult result)
        {
            return PublishResult(context, std::move(result));
        };
        if (context.Scene == nullptr)
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::MissingScene,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization requires a scene registry."));
        }

        const auto strategy = ToGeometryStrategy(command.Config);
        if (!strategy.has_value() ||
            StableTokenForEditorParameterizationStrategy(
                command.Config.Strategy).empty())
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization config is invalid or unsupported."));
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            GeometryProcessingDetail::ResolveEditorStableEntity(
                raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::StaleEntity,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization target is stale or no longer live."));
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        std::vector<std::array<std::uint32_t, 3u>> triangles{};
        std::string topologyDiagnostic{};
        if (!CollectTriangleIndices(view, triangles, topologyDiagnostic))
        {
            return finish(MakeResult(
                command,
                GS::BuildSourceAvailability(view).ProvenanceDomain ==
                        GS::Domain::Mesh
                    ? EditorCommandStatus::InvalidProcessingParameters
                    : EditorCommandStatus::UnsupportedGeometryDomain,
                Parameterization::ParameterizationStatus::InvalidInput,
                std::move(topologyDiagnostic)));
        }

        const std::optional<ParameterizationUvState> before =
            CaptureUvState(view);
        if (!before.has_value())
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Existing v:texcoord has the wrong type, count, or non-finite values."));
        }
        const ParameterizationSourceSnapshot sourceGeneration =
            CaptureParameterizationSourceState(view);
        if (sourceGeneration == nullptr)
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization requires finite count-matched positions and "
                "valid triangle topology."));
        }
        const std::uint64_t geometryMetadataSignature =
            GeometryProcessingDetail::EditorGeometryMetadataSignatureForEntity(
                raw,
                *entity);

        GeometryProcessingDetail::EditorMeshSourceSnapshot source =
            GeometryProcessingDetail::BuildEditorMeshSourceSnapshot(view);
        if (source.Status != EditorCommandStatus::Applied)
        {
            return finish(MakeResult(
                command,
                source.Status,
                Parameterization::ParameterizationStatus::InvalidInput,
                source.Diagnostic.empty()
                    ? "Parameterization could not build the selected mesh."
                    : std::move(source.Diagnostic)));
        }
        if (!RestoreDeletedVertexSlots(source, topologyDiagnostic))
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                std::move(topologyDiagnostic)));
        }

        Parameterization::ParameterizeResult parameterized =
            Parameterization::ParameterizeMesh(source.Mesh, *strategy);
        if (parameterized.Succeeded() &&
            !AlignFaceDiagnosticsToSourceStorage(
                parameterized.Diagnostics,
                source.SourceFaceForMeshFace,
                view.FaceSource->Properties.Size()))
        {
            return finish(MakeResult(
                command,
                EditorCommandStatus::GeometryProcessingFailed,
                Parameterization::ParameterizationStatus::SolverFailed,
                "Parameterization could not align face diagnostics with source storage."));
        }
        EditorParameterizationResult result = MakeResult(
            command,
            parameterized.Succeeded()
                ? EditorCommandStatus::Applied
                : EditorCommandStatus::GeometryProcessingFailed,
            parameterized.Status,
            parameterized.Succeeded()
                ? "Mesh parameterization applied."
                : "Mesh parameterization solver rejected the selected mesh or config.");
        result.Diagnostics = parameterized.Diagnostics;
        result.VertexCount = parameterized.UVs.size();
        if (!parameterized.Succeeded())
            return finish(std::move(result));

        const std::size_t vertexCount = view.VertexSource->Properties.Size();
        if (parameterized.UVs.size() != vertexCount ||
            !AllFiniteUvs(parameterized.UVs))
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.ParameterizationStatus =
                Parameterization::ParameterizationStatus::SolverFailed;
            result.Message =
                "Parameterization returned non-finite or count-mismatched UVs.";
            return finish(std::move(result));
        }

        std::vector<std::uint32_t> fingerprintSurfaceIndices{};
        std::vector<std::uint32_t> fingerprintTriangleFaces{};
        if (BuildMeshSurfaceTriangleTopology(
                view,
                fingerprintSurfaceIndices,
                fingerprintTriangleFaces) == MeshSurfaceTopologyStatus::Success)
        {
            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(
                GS::PropertyNames::kPosition);
            if (positions &&
                positions.Vector().size() ==
                    view.VertexSource->Properties.Size() &&
                AllFinitePositions(positions.Vector()))
            {
                result.DiagnosticInputFingerprint =
                    ComputeDiagnosticInputFingerprint(
                        fingerprintSurfaceIndices,
                        fingerprintTriangleFaces,
                        positions.Vector(),
                        parameterized.UVs);
            }
        }

        const EditorCommandStatus commitStatus = CommitUvState(
            context,
            command.StableEntityId,
            geometryMetadataSignature,
            sourceGeneration,
            *before,
            ParameterizationUvState{
                .Present = true,
                .Values = std::move(parameterized.UVs),
            });
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Message =
                "Parameterization UV writeback failed during editor history commit.";
            return finish(std::move(result));
        }
        return finish(std::move(result));
    }

    EditorParameterizationResult
    ApplyEditorConfiguredParameterizationCommand(
        const EditorGeometryProcessingContext& context,
        const EditorConfiguredParameterizationCommand& command)
    {
        if (context.EngineConfigControlState == nullptr)
        {
            EditorParameterizationCommand direct{
                .StableEntityId = command.StableEntityId,
            };
            return PublishResult(
                context,
                MakeResult(
                    direct,
                    EditorCommandStatus::InvalidProcessingParameters,
                    Parameterization::ParameterizationStatus::InvalidInput,
                    "Configured parameterization requires engine config state."));
        }
        const std::optional<ParameterizationConfig> config =
            GetParameterizationConfig(
                context.EngineConfigControlState->ActiveConfig);
        if (!config.has_value())
        {
            EditorParameterizationCommand direct{
                .StableEntityId = command.StableEntityId,
            };
            return PublishResult(
                context,
                MakeResult(
                    direct,
                    EditorCommandStatus::InvalidProcessingParameters,
                    Parameterization::ParameterizationStatus::InvalidInput,
                    "Configured parameterization is missing its registered config section."));
        }
        return ApplyEditorParameterizationCommand(
            context,
            EditorParameterizationCommand{
                .StableEntityId = command.StableEntityId,
                .Config = *config,
            });
    }

    EditorParameterizationConfigResult
    ApplyEditorParameterizationConfigCommand(
        const EditorGeometryProcessingContext& context,
        const EditorParameterizationConfigCommand& command)
    {
        EditorParameterizationConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                EditorParameterizationConfigStatus::MissingConfigControl;
            result.Message =
                "Parameterization config requires the engine config-control module.";
            return result;
        }
        if (!IsSerializableParameterizationConfigValid(command.Config))
        {
            result.Status =
                EditorParameterizationConfigStatus::PreviewRejected;
            result.Message =
                "Parameterization config is invalid or unsupported.";
            return result;
        }

        Core::Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        SetParameterizationConfig(candidate, command.Config);
        result.Preview = context.PreviewEngineConfigDocument(
            Core::Config::SerializeEngineConfig(candidate),
            command.SourceId.empty()
                ? std::string{"sandbox.parameterization"}
                : command.SourceId);
        if (!Core::Config::IsConfigUsable(result.Preview))
        {
            result.Status =
                EditorParameterizationConfigStatus::PreviewRejected;
            result.Message = "Parameterization config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                EditorParameterizationConfigStatus::ApplyRejected;
            result.Message = "Parameterization config hot-apply was rejected.";
            return result;
        }
        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? EditorParameterizationConfigStatus::NoChange
                : EditorParameterizationConfigStatus::Applied;
        result.Message =
            result.Status == EditorParameterizationConfigStatus::NoChange
                ? "Parameterization config unchanged."
                : "Parameterization config applied.";
        return result;
    }

    std::optional<ParameterizationConfig>
    GetEditorParameterizationConfig(
        const EditorGeometryProcessingContext& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return GetParameterizationConfig(
            context.EngineConfigControlState->ActiveConfig);
    }

    EditorParameterizationViewModel
    BuildEditorParameterizationViewModel(
        const EditorGeometryProcessingContext& context)
    {
        EditorParameterizationViewModel model{};
        if (context.EngineConfigControlState != nullptr)
        {
            if (const auto active = GetParameterizationConfig(
                    context.EngineConfigControlState->ActiveConfig))
            {
                model.Strategy = active->Strategy;
                model.View = active->View;
            }
        }
        if (context.Scene == nullptr || context.Selection == nullptr)
        {
            model.Message =
                "Parameterization view requires scene and selection state.";
            return model;
        }

        const std::span<const std::uint32_t> selected =
            context.Selection->SelectedStableIds();
        if (selected.empty())
        {
            model.Message = "No selected entity is available for UV view.";
            return model;
        }
        model.SelectedStableEntityId = selected.front();

        const entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            GeometryProcessingDetail::ResolveEditorStableEntity(
                raw, model.SelectedStableEntityId);
        if (!entity.has_value())
        {
            model.Message = "Selected entity is stale or no longer live.";
            return model;
        }
        model.HasSelectedEntity = true;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        if (GS::BuildSourceAvailability(view).ProvenanceDomain !=
                GS::Domain::Mesh ||
            view.VertexSource == nullptr)
        {
            model.Message = "Selected entity is not a surface mesh.";
            return model;
        }
        model.SelectedEntityIsMesh = true;

        std::vector<std::uint32_t> surfaceIndices{};
        std::vector<std::uint32_t> triangleFaces{};
        const MeshSurfaceTopologyStatus topologyStatus = BuildMeshSurfaceTriangleTopology(
            view,
            surfaceIndices,
            triangleFaces);
        if (topologyStatus != MeshSurfaceTopologyStatus::Success)
        {
            model.Message = "UV view topology is unavailable (";
            model.Message += DebugNameForMeshSurfaceTopologyStatus(topologyStatus);
            model.Message += ").";
            return model;
        }
        model.Triangles.reserve(surfaceIndices.size() / 3u);
        const bool gpuRequested =
            model.View.RenderMode ==
            ParameterizationUvRenderMode::GpuShaded;
        if (gpuRequested)
            model.LineIndices.reserve(surfaceIndices.size() * 2u);
        for (std::size_t index = 0u;
             index + 2u < surfaceIndices.size();
             index += 3u)
        {
            const std::array triangle{
                surfaceIndices[index],
                surfaceIndices[index + 1u],
                surfaceIndices[index + 2u],
            };
            model.Triangles.push_back(triangle);
            if (gpuRequested)
            {
                model.LineIndices.insert(
                    model.LineIndices.end(),
                    {triangle[0u], triangle[1u],
                     triangle[1u], triangle[2u],
                     triangle[2u], triangle[0u]});
            }
        }

        const auto uvs = view.VertexSource->Properties.Get<glm::vec2>(
            kTexcoordProperty);
        if (uvs &&
            uvs.Vector().size() == view.VertexSource->Properties.Size() &&
            AllFiniteUvs(uvs.Vector()))
        {
            model.UVs = uvs.Vector();
            model.HasUvCoordinates = true;
            if (!model.UVs.empty())
            {
                model.UvBoundsMin = model.UVs.front();
                model.UvBoundsMax = model.UVs.front();
                for (const glm::vec2 uv : model.UVs)
                {
                    model.UvBoundsMin = glm::min(model.UvBoundsMin, uv);
                    model.UvBoundsMax = glm::max(model.UvBoundsMax, uv);
                }
                model.HasFiniteUvBounds =
                    IsFiniteUv(model.UvBoundsMin) &&
                    IsFiniteUv(model.UvBoundsMax);
            }
            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (positions &&
                positions.Vector().size() ==
                    view.VertexSource->Properties.Size() &&
                AllFinitePositions(positions.Vector()))
            {
                model.DiagnosticInputFingerprint =
                    ComputeDiagnosticInputFingerprint(
                        surfaceIndices,
                        triangleFaces,
                        positions.Vector(),
                        model.UVs);
            }
        }
        else if (view.VertexSource->Properties.Exists(kTexcoordProperty))
        {
            model.Message =
                "Selected mesh v:texcoord has the wrong type, count, or non-finite values.";
        }

        if (context.LastParameterizationResult != nullptr &&
            context.LastParameterizationResult->StableEntityId ==
                model.SelectedStableEntityId)
        {
            model.HasLastResult = true;
            model.Strategy = context.LastParameterizationResult->Strategy;
            model.LastStatus =
                context.LastParameterizationResult->ParameterizationStatus;
            model.LastDiagnostics =
                context.LastParameterizationResult->Diagnostics;
            const std::vector<float>& faceDistortion =
                model.LastDiagnostics->FaceConformalDistortion;
            const bool diagnosticsMatchCurrentUv =
                context.LastParameterizationResult->Succeeded() &&
                context.LastParameterizationResult->DiagnosticInputFingerprint
                    .has_value() &&
                model.DiagnosticInputFingerprint.has_value() &&
                context.LastParameterizationResult->DiagnosticInputFingerprint ==
                    model.DiagnosticInputFingerprint;
            if (gpuRequested && diagnosticsMatchCurrentUv &&
                !faceDistortion.empty())
            {
                model.TriangleConformalDistortion.reserve(
                    triangleFaces.size());
                for (const std::uint32_t faceIndex : triangleFaces)
                {
                    model.TriangleConformalDistortion.push_back(
                        faceIndex < faceDistortion.size()
                            ? faceDistortion[faceIndex]
                            : std::numeric_limits<float>::quiet_NaN());
                }
            }
            if (!context.LastParameterizationResult->Message.empty())
                model.Message = context.LastParameterizationResult->Message;
        }
        return model;
    }

    EditorParameterizationUvViewState
    SubmitEditorParameterizationUvView(
        const EditorGeometryProcessingContext& context,
        const EditorParameterizationViewModel& model,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        const bool gpuRequested =
            model.View.RenderMode ==
            ParameterizationUvRenderMode::GpuShaded;
        EditorParameterizationUvViewState fallback{
            .Status = gpuRequested
                ? EditorParameterizationUvViewStatus::WaitingForGpuFrame
                : EditorParameterizationUvViewStatus::CpuLayout,
            .RequestedMode = model.View.RenderMode,
            .ActiveMode = ParameterizationUvRenderMode::CpuLayout,
            .RequestedBackground = model.View.BackgroundMode,
            .ActiveBackground =
                model.View.BackgroundMode ==
                            ParameterizationUvBackgroundMode::Grid ||
                        model.View.BackgroundMode ==
                            ParameterizationUvBackgroundMode::Checker
                    ? model.View.BackgroundMode
                    : ParameterizationUvBackgroundMode::Checker,
            .Width = width,
            .Height = height,
            .Message = gpuRequested
                ? "GPU UV view is waiting for a rendered target."
                : "CPU UV layout is active.",
        };

        if (!gpuRequested)
        {
            if (context.ParameterizationUvViewCommands.Available())
            {
                (void)context.ParameterizationUvViewCommands.Submit(
                    EditorParameterizationUvViewRequest{
                        .Enabled = false,
                        .StableEntityId = model.SelectedStableEntityId,
                        .Width = width,
                        .Height = height,
                        .UvBoundsMin = model.UvBoundsMin,
                        .UvBoundsMax = model.UvBoundsMax,
                        .View = model.View,
                    });
            }
            return fallback;
        }

        EditorParameterizationUvViewRequest request{
            .Enabled = model.HasSelectedEntity &&
                       model.SelectedEntityIsMesh && model.HasUvCoordinates &&
                       model.HasFiniteUvBounds && width > 0u && height > 0u,
            .RequestToken = BuildUvViewRequestToken(model, width, height),
            .StableEntityId = model.SelectedStableEntityId,
            .Width = width,
            .Height = height,
            .UvBoundsMin = model.UvBoundsMin,
            .UvBoundsMax = model.UvBoundsMax,
            .View = model.View,
            .LineIndices = model.LineIndices,
            .TriangleConformalDistortion =
                model.TriangleConformalDistortion,
        };
        fallback.RequestToken = request.RequestToken;

        if (!request.Enabled)
        {
            fallback.Status =
                EditorParameterizationUvViewStatus::InvalidRequest;
            fallback.Message = model.Message.empty()
                ? "GPU UV view requires a selected mesh with finite UVs and a non-empty pane."
                : model.Message;
        }

        if (!context.ParameterizationUvViewCommands.Available())
        {
            if (gpuRequested)
            {
                fallback.Status =
                    EditorParameterizationUvViewStatus::CpuFallbackNonOperational;
                fallback.Message =
                    "GPU UV view command routing is unavailable; CPU layout is active.";
            }
            return fallback;
        }

        if (!request.Enabled)
        {
            (void)context.ParameterizationUvViewCommands.Submit(
                std::move(request));
            return fallback;
        }

        return context.ParameterizationUvViewCommands.Submit(
            std::move(request));
    }

    void DisableEditorParameterizationUvView(
        const EditorGeometryProcessingContext& context)
    {
        if (!context.ParameterizationUvViewCommands.Available())
            return;
        (void)context.ParameterizationUvViewCommands.Submit(
            EditorParameterizationUvViewRequest{});
    }

    const char* DebugNameForEditorParameterizationUvViewStatus(
        const EditorParameterizationUvViewStatus status) noexcept
    {
        switch (status)
        {
        case EditorParameterizationUvViewStatus::Disabled:
            return "disabled";
        case EditorParameterizationUvViewStatus::CpuLayout:
            return "CPU layout";
        case EditorParameterizationUvViewStatus::CpuFallbackNonOperational:
            return "CPU fallback (GPU unavailable)";
        case EditorParameterizationUvViewStatus::WaitingForGeometry:
            return "CPU fallback (waiting for geometry)";
        case EditorParameterizationUvViewStatus::WaitingForGpuFrame:
            return "CPU fallback (waiting for GPU frame)";
        case EditorParameterizationUvViewStatus::InvalidRequest:
            return "CPU fallback (invalid request)";
        case EditorParameterizationUvViewStatus::ResourceCreationFailed:
            return "CPU fallback (GPU resource failure)";
        case EditorParameterizationUvViewStatus::Ready:
            return "GPU shaded";
        }
        return "unknown";
    }
}
