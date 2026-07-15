module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

module Extrinsic.Runtime.SandboxEditorFacades;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.Parameterization;
import Geometry.Properties;

#include "Runtime.SandboxEditorFacades.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Config = Core::Config;
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
            const Config::ParameterizationUvConfig& value) noexcept
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
        ToGeometryStrategy(const Config::ParameterizationConfig& config)
        {
            switch (config.Strategy)
            {
            case Config::ParameterizationStrategyKind::Lscm:
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
            case Config::ParameterizationStrategyKind::HarmonicCotangent:
            case Config::ParameterizationStrategyKind::TutteUniform:
            {
                if (config.Harmonic.PinnedVertices.size() !=
                    config.Harmonic.PinnedUvs.size())
                {
                    return std::nullopt;
                }

                Parameterization::HarmonicParams params{};
                params.Weights =
                    config.Strategy ==
                            Config::ParameterizationStrategyKind::TutteUniform
                        ? Parameterization::HarmonicWeightType::Uniform
                        : Parameterization::HarmonicWeightType::Cotangent;
                switch (config.Harmonic.Boundary)
                {
                case Config::ParameterizationBoundaryPolicy::Circle:
                    params.Boundary =
                        Parameterization::HarmonicBoundaryPolicy::Circle;
                    break;
                case Config::ParameterizationBoundaryPolicy::Square:
                    params.Boundary =
                        Parameterization::HarmonicBoundaryPolicy::Square;
                    break;
                case Config::ParameterizationBoundaryPolicy::Custom:
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
                for (const Config::ParameterizationUvConfig& uv :
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
            case Config::ParameterizationStrategyKind::Bff:
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
                case Config::ParameterizationBffBoundaryMode::AutomaticConformal:
                    if (!config.Bff.BoundaryData.empty())
                        return std::nullopt;
                    params.Mode =
                        Parameterization::BffBoundaryMode::AutomaticConformal;
                    break;
                case Config::ParameterizationBffBoundaryMode::TargetLengths:
                    if (config.Bff.BoundaryData.empty())
                        return std::nullopt;
                    params.Mode =
                        Parameterization::BffBoundaryMode::TargetLengths;
                    break;
                case Config::ParameterizationBffBoundaryMode::TargetAngles:
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
            const Config::ParameterizationConfig& config)
        {
            if (StableTokenForSandboxEditorParameterizationStrategy(
                    config.Strategy).empty())
            {
                return false;
            }
            Config::ParameterizationConfig candidate = config;
            for (const Config::ParameterizationStrategyKind strategy : {
                     Config::ParameterizationStrategyKind::Lscm,
                     Config::ParameterizationStrategyKind::HarmonicCotangent,
                     Config::ParameterizationStrategyKind::Bff})
            {
                candidate.Strategy = strategy;
                if (!ToGeometryStrategy(candidate).has_value())
                    return false;
            }
            switch (config.View.RenderMode)
            {
            case Config::ParameterizationUvRenderMode::CpuLayout:
            case Config::ParameterizationUvRenderMode::GpuShaded:
                break;
            default:
                return false;
            }
            switch (config.View.BackgroundMode)
            {
            case Config::ParameterizationUvBackgroundMode::Grid:
            case Config::ParameterizationUvBackgroundMode::Checker:
            case Config::ParameterizationUvBackgroundMode::TexelDensity:
            case Config::ParameterizationUvBackgroundMode::Texture:
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
            const SandboxEditorParameterizationViewModel& model,
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

        [[nodiscard]] bool RestoreDeletedVertexSlots(
            Detail::SandboxEditorMeshSourceSnapshot& source,
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
                Detail::ResolveSandboxMethodStableEntity(raw, stableEntityId);
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

            Dirty::MarkVertexTexcoordsDirty(raw, *entity);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitUvState(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            ParameterizationUvState before,
            ParameterizationUvState after)
        {
            ECS::Scene::Registry* scene = context.Scene;
            if (context.CommandHistory == nullptr)
            {
                const SandboxEditorCommandStatus status =
                    Detail::ToSandboxMethodCommandStatus(
                        ApplyUvState(scene, stableEntityId, after));
                if (status == SandboxEditorCommandStatus::Applied)
                    Detail::InvalidateSandboxMethodSelectedModelCache(context);
                return status;
            }

            const EditorCommandHistoryResult history =
                context.CommandHistory->Execute(
                    EditorCommandRecord{
                        .Label = "Parameterize mesh UVs",
                        .Redo =
                            [scene, stableEntityId, after]()
                            {
                                return ApplyUvState(
                                    scene, stableEntityId, after);
                            },
                        .Undo =
                            [scene, stableEntityId, before]()
                            {
                                return ApplyUvState(
                                    scene, stableEntityId, before);
                            },
                        .Dirtying = true,
                    });
            const SandboxEditorCommandStatus status =
                Detail::ToSandboxMethodCommandStatus(history.Status);
            if (status == SandboxEditorCommandStatus::Applied)
                Detail::InvalidateSandboxMethodSelectedModelCache(context);
            return status;
        }

        [[nodiscard]] SandboxEditorParameterizationResult MakeResult(
            const SandboxEditorParameterizationCommand& command,
            const SandboxEditorCommandStatus status,
            const Parameterization::ParameterizationStatus parameterizationStatus,
            std::string message)
        {
            return SandboxEditorParameterizationResult{
                .Status = status,
                .StableEntityId = command.StableEntityId,
                .Strategy = command.Config.Strategy,
                .StrategyToken = std::string{
                    StableTokenForSandboxEditorParameterizationStrategy(
                        command.Config.Strategy)},
                .ParameterizationStatus = parameterizationStatus,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] SandboxEditorParameterizationResult PublishResult(
            const SandboxEditorContext& context,
            SandboxEditorParameterizationResult result)
        {
            if (context.MethodResultSinks.Parameterization)
                context.MethodResultSinks.Parameterization(result);
            return result;
        }
    }

    std::string_view StableTokenForSandboxEditorParameterizationStrategy(
        const SandboxEditorParameterizationStrategy strategy) noexcept
    {
        switch (strategy)
        {
        case SandboxEditorParameterizationStrategy::Lscm:
            return "lscm";
        case SandboxEditorParameterizationStrategy::HarmonicCotangent:
            return "harmonic_cotangent";
        case SandboxEditorParameterizationStrategy::TutteUniform:
            return "tutte_uniform";
        case SandboxEditorParameterizationStrategy::Bff:
            return "bff";
        }
        return {};
    }

    SandboxEditorParameterizationResult
    ApplySandboxEditorParameterizationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorParameterizationCommand& command)
    {
        const auto finish = [&context](SandboxEditorParameterizationResult result)
        {
            return PublishResult(context, std::move(result));
        };
        if (context.Scene == nullptr)
        {
            return finish(MakeResult(
                command,
                SandboxEditorCommandStatus::MissingScene,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization requires a scene registry."));
        }

        const auto strategy = ToGeometryStrategy(command.Config);
        if (!strategy.has_value() ||
            StableTokenForSandboxEditorParameterizationStrategy(
                command.Config.Strategy).empty())
        {
            return finish(MakeResult(
                command,
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Parameterization config is invalid or unsupported."));
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            Detail::ResolveSandboxMethodStableEntity(
                raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return finish(MakeResult(
                command,
                SandboxEditorCommandStatus::StaleEntity,
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
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                Parameterization::ParameterizationStatus::InvalidInput,
                std::move(topologyDiagnostic)));
        }

        const std::optional<ParameterizationUvState> before =
            CaptureUvState(view);
        if (!before.has_value())
        {
            return finish(MakeResult(
                command,
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                Parameterization::ParameterizationStatus::InvalidInput,
                "Existing v:texcoord has the wrong type, count, or non-finite values."));
        }

        Detail::SandboxEditorMeshSourceSnapshot source =
            Detail::BuildSandboxEditorMeshSourceSnapshot(view);
        if (source.Status != SandboxEditorCommandStatus::Applied)
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
                SandboxEditorCommandStatus::InvalidProcessingParameters,
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
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                Parameterization::ParameterizationStatus::SolverFailed,
                "Parameterization could not align face diagnostics with source storage."));
        }
        SandboxEditorParameterizationResult result = MakeResult(
            command,
            parameterized.Succeeded()
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
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
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.ParameterizationStatus =
                Parameterization::ParameterizationStatus::SolverFailed;
            result.Message =
                "Parameterization returned non-finite or count-mismatched UVs.";
            return finish(std::move(result));
        }

        std::vector<std::uint32_t> fingerprintSurfaceIndices{};
        std::vector<std::uint32_t> fingerprintTriangleFaces{};
        if (BuildSurfaceTriangleTopology(
                view,
                fingerprintSurfaceIndices,
                fingerprintTriangleFaces) == MeshPackStatus::Success)
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

        const SandboxEditorCommandStatus commitStatus = CommitUvState(
            context,
            command.StableEntityId,
            *before,
            ParameterizationUvState{
                .Present = true,
                .Values = std::move(parameterized.UVs),
            });
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Message =
                "Parameterization UV writeback failed during editor history commit.";
            return finish(std::move(result));
        }
        return finish(std::move(result));
    }

    SandboxEditorParameterizationResult
    ApplySandboxEditorConfiguredParameterizationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorConfiguredParameterizationCommand& command)
    {
        if (context.EngineConfigControlState == nullptr)
        {
            SandboxEditorParameterizationCommand direct{
                .StableEntityId = command.StableEntityId,
            };
            return PublishResult(
                context,
                MakeResult(
                    direct,
                    SandboxEditorCommandStatus::InvalidProcessingParameters,
                    Parameterization::ParameterizationStatus::InvalidInput,
                    "Configured parameterization requires engine config state."));
        }
        return ApplySandboxEditorParameterizationCommand(
            context,
            SandboxEditorParameterizationCommand{
                .StableEntityId = command.StableEntityId,
                .Config = context.EngineConfigControlState->ActiveConfig.Sandbox
                              .Parameterization,
            });
    }

    SandboxEditorParameterizationConfigResult
    ApplySandboxEditorParameterizationConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorParameterizationConfigCommand& command)
    {
        SandboxEditorParameterizationConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                SandboxEditorParameterizationConfigStatus::MissingConfigFacade;
            result.Message =
                "Parameterization config requires the engine config-control facade.";
            return result;
        }
        if (!IsSerializableParameterizationConfigValid(command.Config))
        {
            result.Status =
                SandboxEditorParameterizationConfigStatus::PreviewRejected;
            result.Message =
                "Parameterization config is invalid or unsupported.";
            return result;
        }

        Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        candidate.Sandbox.Parameterization = command.Config;
        result.Preview = context.PreviewEngineConfigDocument(
            Config::SerializeEngineConfig(candidate),
            command.SourceId.empty()
                ? std::string{"sandbox.parameterization"}
                : command.SourceId);
        if (!Config::IsConfigUsable(result.Preview))
        {
            result.Status =
                SandboxEditorParameterizationConfigStatus::PreviewRejected;
            result.Message = "Parameterization config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                SandboxEditorParameterizationConfigStatus::ApplyRejected;
            result.Message = "Parameterization config hot-apply was rejected.";
            return result;
        }
        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? SandboxEditorParameterizationConfigStatus::NoChange
                : SandboxEditorParameterizationConfigStatus::Applied;
        result.Message =
            result.Status == SandboxEditorParameterizationConfigStatus::NoChange
                ? "Parameterization config unchanged."
                : "Parameterization config applied.";
        return result;
    }

    std::optional<Config::ParameterizationConfig>
    GetSandboxEditorParameterizationConfig(
        const SandboxEditorContext& context) noexcept
    {
        if (context.EngineConfigControlState == nullptr)
            return std::nullopt;
        return context.EngineConfigControlState->ActiveConfig.Sandbox
            .Parameterization;
    }

    SandboxEditorParameterizationViewModel
    BuildSandboxEditorParameterizationViewModel(
        const SandboxEditorContext& context)
    {
        SandboxEditorParameterizationViewModel model{};
        if (context.EngineConfigControlState != nullptr)
        {
            const Config::ParameterizationConfig& active =
                context.EngineConfigControlState->ActiveConfig.Sandbox
                    .Parameterization;
            model.Strategy = active.Strategy;
            model.View = active.View;
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
            Detail::ResolveSandboxMethodStableEntity(
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
        const MeshPackStatus topologyStatus = BuildSurfaceTriangleTopology(
            view,
            surfaceIndices,
            triangleFaces);
        if (topologyStatus != MeshPackStatus::Success)
        {
            model.Message = "UV view topology is unavailable (";
            model.Message += DebugNameForMeshPackStatus(topologyStatus);
            model.Message += ").";
            return model;
        }
        model.Triangles.reserve(surfaceIndices.size() / 3u);
        const bool gpuRequested =
            model.View.RenderMode ==
            Config::ParameterizationUvRenderMode::GpuShaded;
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

    SandboxEditorParameterizationUvViewState
    SubmitSandboxEditorParameterizationUvView(
        const SandboxEditorContext& context,
        const SandboxEditorParameterizationViewModel& model,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        const bool gpuRequested =
            model.View.RenderMode ==
            Config::ParameterizationUvRenderMode::GpuShaded;
        SandboxEditorParameterizationUvViewState fallback{
            .Status = gpuRequested
                ? SandboxEditorParameterizationUvViewStatus::WaitingForGpuFrame
                : SandboxEditorParameterizationUvViewStatus::CpuLayout,
            .RequestedMode = model.View.RenderMode,
            .ActiveMode = Config::ParameterizationUvRenderMode::CpuLayout,
            .RequestedBackground = model.View.BackgroundMode,
            .ActiveBackground =
                model.View.BackgroundMode ==
                            Config::ParameterizationUvBackgroundMode::Grid ||
                        model.View.BackgroundMode ==
                            Config::ParameterizationUvBackgroundMode::Checker
                    ? model.View.BackgroundMode
                    : Config::ParameterizationUvBackgroundMode::Checker,
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
                    SandboxEditorParameterizationUvViewRequest{
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

        SandboxEditorParameterizationUvViewRequest request{
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
                SandboxEditorParameterizationUvViewStatus::InvalidRequest;
            fallback.Message = model.Message.empty()
                ? "GPU UV view requires a selected mesh with finite UVs and a non-empty pane."
                : model.Message;
        }

        if (!context.ParameterizationUvViewCommands.Available())
        {
            if (gpuRequested)
            {
                fallback.Status =
                    SandboxEditorParameterizationUvViewStatus::CpuFallbackNonOperational;
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

    void DisableSandboxEditorParameterizationUvView(
        const SandboxEditorContext& context)
    {
        if (!context.ParameterizationUvViewCommands.Available())
            return;
        (void)context.ParameterizationUvViewCommands.Submit(
            SandboxEditorParameterizationUvViewRequest{});
    }

    const char* DebugNameForSandboxEditorParameterizationUvViewStatus(
        const SandboxEditorParameterizationUvViewStatus status) noexcept
    {
        switch (status)
        {
        case SandboxEditorParameterizationUvViewStatus::Disabled:
            return "disabled";
        case SandboxEditorParameterizationUvViewStatus::CpuLayout:
            return "CPU layout";
        case SandboxEditorParameterizationUvViewStatus::CpuFallbackNonOperational:
            return "CPU fallback (GPU unavailable)";
        case SandboxEditorParameterizationUvViewStatus::WaitingForGeometry:
            return "CPU fallback (waiting for geometry)";
        case SandboxEditorParameterizationUvViewStatus::WaitingForGpuFrame:
            return "CPU fallback (waiting for GPU frame)";
        case SandboxEditorParameterizationUvViewStatus::InvalidRequest:
            return "CPU fallback (invalid request)";
        case SandboxEditorParameterizationUvViewStatus::ResourceCreationFailed:
            return "CPU fallback (GPU resource failure)";
        case SandboxEditorParameterizationUvViewStatus::Ready:
            return "GPU shaded";
        }
        return "unknown";
    }
}
